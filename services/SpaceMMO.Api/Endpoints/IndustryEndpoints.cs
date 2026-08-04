using Microsoft.EntityFrameworkCore;
using SpaceMMO.Api.Auth;
using SpaceMMO.Data;
using SpaceMMO.Data.Entities;
using SpaceMMO.Data.Industry;
using SpaceMMO.Data.Inventories;
using SpaceMMO.Data.Market;
using SpaceMMO.Domain.Industry;

namespace SpaceMMO.Api.Endpoints;

public sealed record StartJobRequest(int CharacterId, int RecipeId, int StationId, int Runs);

public sealed record ClaimJobRequest(int CharacterId, long JobId);

public sealed record CancelJobRequest(int CharacterId, long JobId);

public sealed record RecipeInputResponse(int ItemDefId, string ItemKey, string Name, int Quantity);

/// <summary>
/// One recipe, with everything a client needs to decide whether it can be run.
/// </summary>
/// <remarks>
/// Carries the item and skill <em>keys</em> alongside their ids. Ids are assigned by the database
/// and differ between any two seeded environments, so a client that remembered one would break the
/// day the database was rebuilt in a different order — the same reason bodies are looked up by
/// content key.
/// </remarks>
public sealed record RecipeResponse(
    int Id,
    string Key,
    int OutputItemDefId,
    string OutputItemKey,
    string OutputName,
    int OutputQuantity,
    string SkillKey,
    string SkillName,
    int RequiredLevel,
    int JobSeconds,
    long XpPerRun,
    int? RequiredToolItemDefId,
    string? RequiredToolKey,
    string? RequiredToolName,
    IReadOnlyList<RecipeInputResponse> Inputs);

/// <summary>
/// A job in progress, as the server sees it.
/// </summary>
/// <remarks>
/// <strong><see cref="IsClaimable"/> and <see cref="SecondsRemaining"/> are computed here against
/// the server clock, never left to the client.</strong> A client that worked out for itself whether
/// a job was done would disagree with the server the moment the two clocks drifted — and it is the
/// server that decides, so the client would be showing a claim button that does not work.
/// </remarks>
public sealed record IndustryJobResponse(
    long Id,
    int RecipeId,
    string RecipeKey,
    string OutputName,
    int OutputQuantityTotal,
    int Runs,
    int StationId,
    IndustryJobState State,
    DateTimeOffset StartedAt,
    DateTimeOffset CompletesAt,
    bool IsClaimable,
    int SecondsRemaining);

/// <summary>
/// Time-gated manufacturing jobs.
/// </summary>
/// <remarks>
/// Inputs are consumed at start and outputs created at claim, and the server clock decides when a
/// job is done. The client cannot assert completion; it can only ask, and be told no.
/// </remarks>
public static class IndustryEndpoints
{
    public static void MapIndustryEndpoints(this IEndpointRouteBuilder routes)
    {
        RouteGroupBuilder group = routes.MapGroup("/industry").WithTags("Industry");

        // Unauthenticated, matching bodies and deposits: the recipe catalog is authored content,
        // identical for every player, and already sitting in data/recipes/. Requiring a token to
        // read it would mean the dedicated server needed credentials purely to know what can be
        // built.
        group.MapGet("/recipes", RecipesAsync);

        group.MapGet("/jobs", JobsAsync);
        group.MapPost("/jobs", StartAsync);
        group.MapPost("/jobs/claim", ClaimAsync);
        group.MapPost("/jobs/cancel", CancelAsync);
    }

    private static async Task<IResult> RecipesAsync(
        SpaceMmoDbContext database,
        CancellationToken cancellation)
    {
        List<Recipe> recipes = await database.Recipes
            .Include(r => r.OutputItemDef)
            .Include(r => r.Skill)
            .Include(r => r.RequiredToolItemDef)
            .Include(r => r.Inputs)
            .ThenInclude(i => i.ItemDef)
            .OrderBy(r => r.Key)
            .AsNoTracking()
            .ToListAsync(cancellation);

        return Results.Ok(recipes
            .Select(r => new RecipeResponse(
                r.Id,
                r.Key,
                r.OutputItemDefId,
                r.OutputItemDef!.Key,
                r.OutputItemDef.Name,
                r.OutputQuantity,
                r.Skill!.Key,
                r.Skill.Name,
                r.RequiredLevel,
                r.JobSeconds,
                r.XpPerRun,
                r.RequiredToolItemDefId,
                r.RequiredToolItemDef?.Key,
                r.RequiredToolItemDef?.Name,
                r.Inputs
                    .OrderBy(i => i.ItemDef!.Key)
                    .Select(i => new RecipeInputResponse(
                        i.ItemDefId, i.ItemDef!.Key, i.ItemDef.Name, i.Quantity))
                    .ToList()))
            .ToList());
    }

    /// <summary>
    /// A character's jobs that have not reached a terminal state.
    /// </summary>
    /// <remarks>
    /// Running only. Claimed and cancelled jobs are history rather than something to act on, and a
    /// client that had to filter them out would be one bug away from offering a claim button for a
    /// job already collected.
    /// </remarks>
    private static async Task<IResult> JobsAsync(
        int characterId,
        HttpContext context,
        Caller caller,
        SpaceMmoDbContext database,
        CancellationToken cancellation)
    {
        OwnershipResult owned =
            await caller.OwnedCharacterAsync(context, characterId, cancellation);

        if (owned.Status != OwnershipStatus.Owned)
        {
            return owned.ToProblem();
        }

        List<IndustryJob> jobs = await database.IndustryJobs
            .Where(j => j.CharacterId == characterId && j.State == IndustryJobState.Running)
            .Include(j => j.Recipe)
            .ThenInclude(r => r!.OutputItemDef)
            .OrderBy(j => j.CompletesAt)
            .AsNoTracking()
            .ToListAsync(cancellation);

        DateTimeOffset now = DateTimeOffset.UtcNow;

        return Results.Ok(jobs
            .Select(j => new IndustryJobResponse(
                j.Id,
                j.RecipeId,
                j.Recipe!.Key,
                j.Recipe.OutputItemDef!.Name,
                j.Recipe.OutputQuantity * j.Runs,
                j.Runs,
                j.StationId,
                j.State,
                j.StartedAt,
                j.CompletesAt,
                now >= j.CompletesAt,

                // Floored at zero rather than allowed to go negative, and rounded up so a job with
                // 200 ms left reads as "1 s" instead of "0 s" while still refusing to be claimed.
                (int)Math.Max(0, Math.Ceiling((j.CompletesAt - now).TotalSeconds))))
            .ToList());
    }

    private static async Task<IResult> StartAsync(
        StartJobRequest request,
        HttpContext context,
        Caller caller,
        IndustryService industry,
        CancellationToken cancellation)
    {
        OwnershipResult owned =
            await caller.OwnedCharacterAsync(context, request.CharacterId, cancellation);

        if (owned.Status != OwnershipStatus.Owned)
        {
            return owned.ToProblem();
        }

        if (request.Runs <= 0)
        {
            return Results.ValidationProblem(new Dictionary<string, string[]>
            {
                ["runs"] = ["Runs must be positive."],
            });
        }

        try
        {
            StartJobResult result = await industry.StartJobAsync(
                request.CharacterId, request.RecipeId, request.StationId, request.Runs, cancellation);

            return Results.Ok(result);
        }
        catch (SkillTooLowException ex)
        {
            return Results.Conflict(new { error = ex.Message, reason = "skill_too_low" });
        }
        catch (NoFreeJobSlotException ex)
        {
            return Results.Conflict(new { error = ex.Message, reason = "no_free_slot" });
        }
        catch (MissingToolException ex)
        {
            return Results.Conflict(new { error = ex.Message, reason = "missing_tool" });
        }
        catch (InsufficientItemsException ex)
        {
            return Results.Conflict(new { error = ex.Message, reason = "insufficient_items" });
        }
        catch (InsufficientFundsException ex)
        {
            return Results.Conflict(new { error = ex.Message, reason = "insufficient_funds" });
        }
    }

    private static async Task<IResult> ClaimAsync(
        ClaimJobRequest request,
        HttpContext context,
        Caller caller,
        IndustryService industry,
        CancellationToken cancellation)
    {
        OwnershipResult owned =
            await caller.OwnedCharacterAsync(context, request.CharacterId, cancellation);

        if (owned.Status != OwnershipStatus.Owned)
        {
            return owned.ToProblem();
        }

        ClaimJobResult result =
            await industry.ClaimJobAsync(request.JobId, request.CharacterId, cancellation);

        return Results.Ok(result);
    }

    private static async Task<IResult> CancelAsync(
        CancelJobRequest request,
        HttpContext context,
        Caller caller,
        IndustryService industry,
        CancellationToken cancellation)
    {
        OwnershipResult owned =
            await caller.OwnedCharacterAsync(context, request.CharacterId, cancellation);

        if (owned.Status != OwnershipStatus.Owned)
        {
            return owned.ToProblem();
        }

        bool cancelled =
            await industry.CancelJobAsync(request.JobId, request.CharacterId, cancellation);

        return cancelled ? Results.Ok(new { cancelled = true }) : Results.NotFound();
    }
}
