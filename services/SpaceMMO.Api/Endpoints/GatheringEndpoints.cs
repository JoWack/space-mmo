using SpaceMMO.Api.Auth;
using SpaceMMO.Data.Gathering;

namespace SpaceMMO.Api.Endpoints;

public sealed record GatherRequest(int CharacterId, long ResourceNodeId, int StationId);

/// <summary>
/// Resource extraction — the only place material enters the economy.
/// </summary>
/// <remarks>
/// The request carries no quantity, and that is the whole design. The client asks to gather; the
/// server works out how much wall-clock time has elapsed since this character last did and grants
/// only what that entitles them to. A client calling in a tight loop extracts exactly as much as
/// one calling at the tick interval.
/// </remarks>
public static class GatheringEndpoints
{
    public static void MapGatheringEndpoints(this IEndpointRouteBuilder routes)
    {
        RouteGroupBuilder group = routes.MapGroup("/gathering").WithTags("Gathering");

        group.MapPost("/gather", GatherAsync);
    }

    private static async Task<IResult> GatherAsync(
        GatherRequest request,
        HttpContext context,
        Caller caller,
        GatheringService gathering,
        CancellationToken cancellation)
    {
        OwnershipResult owned =
            await caller.OwnedCharacterAsync(context, request.CharacterId, cancellation);

        if (owned.Status != OwnershipStatus.Owned)
        {
            return owned.ToProblem();
        }

        GatherResult result = await gathering.GatherAsync(
            request.CharacterId, request.ResourceNodeId, request.StationId, cancellation);

        // An empty result is a success, not an error: it means not enough time has passed, or the
        // node is spent. Both are ordinary states a client renders rather than faults it reports.
        return Results.Ok(result);
    }
}
