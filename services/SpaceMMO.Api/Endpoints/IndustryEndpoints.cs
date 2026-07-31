using SpaceMMO.Api.Auth;
using SpaceMMO.Data.Industry;
using SpaceMMO.Data.Inventories;
using SpaceMMO.Data.Market;

namespace SpaceMMO.Api.Endpoints;

public sealed record StartJobRequest(int CharacterId, int RecipeId, int StationId, int Runs);

public sealed record ClaimJobRequest(int CharacterId, long JobId);

public sealed record CancelJobRequest(int CharacterId, long JobId);

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

        group.MapPost("/jobs", StartAsync);
        group.MapPost("/jobs/claim", ClaimAsync);
        group.MapPost("/jobs/cancel", CancelAsync);
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
