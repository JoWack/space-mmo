using Microsoft.EntityFrameworkCore;
using SpaceMMO.Api.Auth;
using SpaceMMO.Data;
using SpaceMMO.Data.Content;
using SpaceMMO.Data.Entities;
using SpaceMMO.Data.Industry;
using SpaceMMO.Data.Inventories;
using SpaceMMO.Data.Market;
using SpaceMMO.Domain.Industry;
using SpaceMMO.Domain.Items;

namespace SpaceMMO.Api.Endpoints;

/// <param name="MainInputStyle">
/// Which style of the recipe's main input to make it from (task 187), as the industry panel's picker
/// chose. Null takes the style held most of.
/// </param>
public sealed record StartJobRequest(
    int CharacterId, int RecipeId, int StationId, int Runs, ItemStyle? MainInputStyle = null);

public sealed record ClaimJobRequest(int CharacterId, long JobId);

public sealed record CancelJobRequest(int CharacterId, long JobId);

/// <param name="Style">
/// A raw material's world (task 187), so the industry panel can tag a refine's ore before any is
/// held. Common for anything crafted, whose style is the stack's and not the item's.
/// </param>
public sealed record RecipeInputResponse(
    int ItemDefId, string ItemKey, string Name, int Quantity, ItemStyle Style = ItemStyle.Common);

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
    IReadOnlyList<RecipeInputResponse> Inputs,

    /// <summary>
    /// The input whose style the output takes, or null for a recipe that makes Common goods
    /// (task 187). The one input the industry panel offers a style picker on.
    /// </summary>
    string? MainInputKey = null);

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
    int SecondsRemaining,

    /// <summary>The style the output will have, decided when the job started (task 187).</summary>
    ItemStyle OutputStyle = ItemStyle.Common);

public sealed record RepairRequest(int CharacterId, int StationId, long ItemInstanceId);

/// <summary>One material a repair takes, against what the hangar and pockets hold together.</summary>
/// <param name="Units">Whole units this repair charges.</param>
/// <param name="OwedAfterTenThousandths">What stays on the item's tab after, in ten-thousandths of a unit.</param>
public sealed record RepairMaterialResponse(
    int ItemDefId, string ItemKey, string Name, int Units, int Held, int OwedAfterTenThousandths);

/// <summary>
/// One row of the REPAIRS section (task 196): a damaged item, what repairing it does, and what it costs.
/// </summary>
/// <remarks>
/// <strong>Every number is the server's.</strong> The maximum's fall, the tab's carried fractions and the rounding of
/// the credits are rules a client would have to copy exactly to show the right price, and a copy that drifted would
/// show a button that charges something else.
/// </remarks>
/// <param name="Kind">Where it is: Equipped (worn), CharacterCarried (pockets) or StationHangar (this station's).</param>
/// <param name="WornOut">This repair destroys it instead, and charges nothing: what the button warns of.</param>
/// <param name="Priced">False when nothing makes the item, so nothing prices its repair and it is refused.</param>
public sealed record RepairQuoteResponse(
    long ItemInstanceId,
    int ItemDefId,
    string ItemKey,
    string Name,
    ItemCategory Category,
    ItemStyle Style,
    InventoryKind Kind,
    int Condition,
    int MaxConditionHundredths,
    int ConditionAfter,
    int MaxConditionAfterHundredths,
    bool WornOut,
    bool Priced,
    IReadOnlyList<RepairMaterialResponse> Materials,
    long CreditsMinorUnits);

/// <summary>Whole units of one material a repair took. The quote it answers has the names.</summary>
public sealed record RepairChargedResponse(int ItemDefId, int Units);

/// <param name="Destroyed">Worn out: the item is gone and nothing was charged.</param>
public sealed record RepairResultResponse(
    long ItemInstanceId,
    bool Destroyed,
    int Condition,
    int MaxConditionHundredths,
    IReadOnlyList<RepairChargedResponse> Charged,
    long CreditsMinorUnits);

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

        // Task 196: what the REPAIRS section lists, and the instant repair its button does.
        group.MapGet("/repairs", RepairsAsync);
        group.MapPost("/repairs", RepairAsync);
    }

    /// <summary>Every damaged item the character could repair at this station, priced.</summary>
    /// <remarks>409 <c>not_docked</c> unless docked there: the bay is inside the station.</remarks>
    private static async Task<IResult> RepairsAsync(
        int characterId,
        int stationId,
        HttpContext context,
        Caller caller,
        RepairService repairs,
        CancellationToken cancellation)
    {
        OwnershipResult owned = await caller.OwnedCharacterAsync(context, characterId, cancellation);

        if (owned.Status != OwnershipStatus.Owned)
        {
            return owned.ToProblem();
        }

        try
        {
            IReadOnlyList<RepairQuote> quotes = await repairs.QuoteAsync(characterId, stationId, cancellation);

            return Results.Ok(quotes
                .Select(q => new RepairQuoteResponse(
                    q.ItemInstanceId,
                    q.ItemDefId,
                    q.ItemKey,
                    q.Name,
                    q.Category,
                    q.Style,
                    q.Kind,
                    q.Condition,
                    q.MaxConditionHundredths,
                    q.ConditionAfter,
                    q.MaxAfterHundredths,
                    q.WornOut,
                    q.Priced,
                    [.. q.Materials.Select(m => new RepairMaterialResponse(
                        m.ItemDefId, m.ItemKey, m.Name, m.Units, m.Held, m.OwedAfterTenThousandths))],
                    q.Credits.MinorUnits))
                .ToList());
        }
        catch (RepairRefusedException ex)
        {
            return Results.Conflict(new { error = ex.Message, reason = ex.Reason });
        }
    }

    /// <summary>Repairs one item at once, or destroys it when worn out.</summary>
    /// <remarks>
    /// 409 with a reason a client can act on: <c>not_docked</c>, <c>not_here</c>, <c>not_damaged</c>, <c>no_recipe</c>,
    /// <c>insufficient_items</c> or <c>insufficient_funds</c>, matching a job's start.
    /// </remarks>
    private static async Task<IResult> RepairAsync(
        RepairRequest request,
        HttpContext context,
        Caller caller,
        RepairService repairs,
        CancellationToken cancellation)
    {
        OwnershipResult owned = await caller.OwnedCharacterAsync(context, request.CharacterId, cancellation);

        if (owned.Status != OwnershipStatus.Owned)
        {
            return owned.ToProblem();
        }

        try
        {
            RepairResult result = await repairs.RepairAsync(
                request.CharacterId, request.StationId, request.ItemInstanceId, cancellation);

            return Results.Ok(new RepairResultResponse(
                result.ItemInstanceId,
                result.Destroyed,
                result.Condition,
                result.MaxConditionHundredths,
                [.. result.Charged.Select(c => new RepairChargedResponse(c.ItemDefId, c.Units))],
                result.Credits.MinorUnits));
        }
        catch (RepairRefusedException ex)
        {
            return Results.Conflict(new { error = ex.Message, reason = ex.Reason });
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

    private static async Task<IResult> RecipesAsync(
        SpaceMmoDbContext database,
        CancellationToken cancellation)
    {
        // Retired recipes are not offered (task 188); a job already running from one still shows in
        // /industry/jobs, which reads the job and not this catalogue.
        List<Recipe> recipes = await database.Recipes
            .Where(r => r.RetiredAt == null)
            .Include(r => r.OutputItemDef)
            .Include(r => r.Skill)
            .Include(r => r.RequiredToolItemDef)
            .Include(r => r.MainInputItemDef)
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
                        i.ItemDefId, i.ItemDef!.Key, i.ItemDef.Name, i.Quantity, i.ItemDef.Style))
                    .ToList(),
                r.MainInputItemDef?.Key))
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
                (int)Math.Max(0, Math.Ceiling((j.CompletesAt - now).TotalSeconds)),
                j.OutputStyle))
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
                request.CharacterId,
                request.RecipeId,
                request.StationId,
                request.Runs,
                request.MainInputStyle,
                cancellation);

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
        catch (ContentRetiredException ex)
        {
            return Results.Conflict(new { error = ex.Message, reason = "retired" });
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
