using SpaceMMO.Api.Auth;
using SpaceMMO.Data.Docking;

namespace SpaceMMO.Api.Endpoints;

public sealed record DockRequest(int CharacterId, int StationId);

public sealed record UndockRequest(int CharacterId);

public sealed record DockedResponse(int? StationId);

/// <summary>
/// Where a character is docked.
/// </summary>
/// <remarks>
/// <para>
/// Docking and undocking present the service credential, because the fact being recorded is a
/// fact about where a ship is in the world, and the only party that knows that is the game server
/// which moved it. A player asking to be docked is a player asserting a position, and positions
/// are the client's to predict and the server's to decide (ADR-0003).
/// </para>
/// <para>
/// Reading where you are docked is an ordinary owned-character question, so that half takes a
/// player's own token like the rest of the character endpoints.
/// </para>
/// </remarks>
public static class DockingEndpoints
{
    public static void MapDockingEndpoints(this IEndpointRouteBuilder routes)
    {
        RouteGroupBuilder group = routes.MapGroup("/docking").WithTags("Docking");

        group.MapPost("/dock", DockAsync);
        group.MapPost("/undock", UndockAsync);
        group.MapGet("/{characterId:int}", WhereAsync);
    }

    private static async Task<IResult> DockAsync(
        DockRequest request,
        HttpContext context,
        ServiceCredential service,
        DockingService docking,
        CancellationToken cancellation)
    {
        // Service only. Unlike gathering, there is no owned-character fallback here: gathering at
        // least fails on its own merits if the caller lies, because the node's rate limit and
        // level gate still apply. A false docking claim has nothing else standing behind it.
        if (!service.IsServiceCaller(context))
        {
            return Results.Problem(
                title: "Docking is decided by the game server.",
                detail: "Only the simulation knows where a ship is.",
                statusCode: StatusCodes.Status401Unauthorized);
        }

        try
        {
            await docking.DockAsync(request.CharacterId, request.StationId, cancellation);

            return Results.Ok(new DockedResponse(request.StationId));
        }
        catch (UnknownCharacterException)
        {
            return Results.Problem(
                title: "No such character.", statusCode: StatusCodes.Status404NotFound);
        }
        catch (UnknownStationException)
        {
            // Also the answer for a station with no position, deliberately. From outside, "there
            // is no such station" and "that station is nowhere" are the same fact: there is
            // nothing there to dock at.
            return Results.Problem(
                title: "No such station, or it has nowhere to dock.",
                statusCode: StatusCodes.Status404NotFound);
        }
    }

    private static async Task<IResult> UndockAsync(
        UndockRequest request,
        HttpContext context,
        ServiceCredential service,
        DockingService docking,
        CancellationToken cancellation)
    {
        if (!service.IsServiceCaller(context))
        {
            return Results.Problem(
                title: "Undocking is decided by the game server.",
                statusCode: StatusCodes.Status401Unauthorized);
        }

        try
        {
            await docking.UndockAsync(request.CharacterId, cancellation);

            return Results.Ok(new DockedResponse(null));
        }
        catch (UnknownCharacterException)
        {
            return Results.Problem(
                title: "No such character.", statusCode: StatusCodes.Status404NotFound);
        }
    }

    private static async Task<IResult> WhereAsync(
        int characterId,
        HttpContext context,
        Caller caller,
        DockingService docking,
        CancellationToken cancellation)
    {
        OwnershipResult owned =
            await caller.ServiceOrOwnedCharacterAsync(context, characterId, cancellation);

        if (owned.Status != OwnershipStatus.Owned)
        {
            return owned.ToProblem();
        }

        return Results.Ok(
            new DockedResponse(await docking.DockedStationIdAsync(characterId, cancellation)));
    }
}
