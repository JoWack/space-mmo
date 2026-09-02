using SpaceMMO.Api.Auth;
using SpaceMMO.Data.Docking;
using SpaceMMO.Data.Whereabouts;

namespace SpaceMMO.Api.Endpoints;

/// <param name="X">System coordinates, in kilometres.</param>
/// <param name="Flying">Whether they are in a ship rather than stood on something.</param>
public sealed record RecordWhereaboutsRequest(
    int CharacterId, double X, double Y, double Z, bool Flying);

/// <summary>Where a character was last seen, or nulls if they never have been.</summary>
public sealed record WhereaboutsResponse(double? X, double? Y, double? Z, bool Flying);

/// <summary>
/// Where a character was when the world last saw them, so signing back in puts them there.
/// </summary>
/// <remarks>
/// <para>
/// Writing presents the service credential, exactly as docking does and for the same reason: the
/// fact being recorded is where something is in the world, and the only party that knows is the
/// simulation that moved it. A player who could write this could write any position (ADR-0003) —
/// which is the whole of "no free teleports".
/// </para>
/// <para>
/// <strong>Reading is here for the tools, not for signing in.</strong> The position a returning
/// player is put back at rides along on <c>/accounts/resolve-character</c>, because that is the one
/// request a connecting server already makes and a second round trip in the middle of a sign-in
/// buys nothing.
/// </para>
/// </remarks>
public static class WhereaboutsEndpoints
{
    public static void MapWhereaboutsEndpoints(this IEndpointRouteBuilder routes)
    {
        RouteGroupBuilder group = routes.MapGroup("/whereabouts").WithTags("Whereabouts");

        group.MapPost("/", RecordAsync);
        group.MapGet("/{characterId:int}", LastKnownAsync);
    }

    private static async Task<IResult> RecordAsync(
        RecordWhereaboutsRequest request,
        HttpContext context,
        ServiceCredential service,
        WhereaboutsService whereabouts,
        CancellationToken cancellation)
    {
        if (!service.IsServiceCaller(context))
        {
            return Results.Problem(
                title: "Where a player is, is decided by the game server.",
                detail: "Only the simulation knows where anybody is.",
                statusCode: StatusCodes.Status401Unauthorized);
        }

        try
        {
            await whereabouts.RecordAsync(
                request.CharacterId,
                new CharacterWhereabouts(request.X, request.Y, request.Z, request.Flying),
                cancellation);

            return Results.Ok(
                new WhereaboutsResponse(request.X, request.Y, request.Z, request.Flying));
        }
        catch (UnknownCharacterException)
        {
            return Results.Problem(
                title: "No such character.", statusCode: StatusCodes.Status404NotFound);
        }
        catch (ImpossiblePositionException)
        {
            // A bad request rather than a swallowed one. A game server sending NaN has a fault in
            // it, and answering 200 would let it keep sending them until somebody signed in.
            return Results.Problem(
                title: "That is not a position.",
                detail: "Coordinates must be finite.",
                statusCode: StatusCodes.Status400BadRequest);
        }
    }

    private static async Task<IResult> LastKnownAsync(
        int characterId,
        HttpContext context,
        Caller caller,
        WhereaboutsService whereabouts,
        CancellationToken cancellation)
    {
        OwnershipResult owned =
            await caller.ServiceOrOwnedCharacterAsync(context, characterId, cancellation);

        if (owned.Status != OwnershipStatus.Owned)
        {
            return owned.ToProblem();
        }

        CharacterWhereabouts? where = await whereabouts.LastKnownAsync(characterId, cancellation);

        return Results.Ok(where is null
            ? new WhereaboutsResponse(null, null, null, false)
            : new WhereaboutsResponse(where.X, where.Y, where.Z, where.Flying));
    }
}
