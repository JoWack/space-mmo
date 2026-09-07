using SpaceMMO.Api.Auth;
using SpaceMMO.Data.Entities;
using SpaceMMO.Data.Ships;

namespace SpaceMMO.Api.Endpoints;

public sealed record SummonShipRequest(int CharacterId, long HullItemInstanceId);

/// <summary>What a character can reach of their ship right now.</summary>
/// <param name="HoldInventoryId">The hold, or null if their ship is not with them.</param>
/// <param name="CapacityM3">How much it carries. Zero when there is no hold to speak of.</param>
public sealed record ShipHoldResponse(long? HoldInventoryId, double CapacityM3);

public sealed record BoardShipRequest(int CharacterId, long HullItemInstanceId);

public sealed record DisembarkRequest(int CharacterId);

/// <summary>Which hull a character would fly, where it is parked, and whether they are in it.</summary>
/// <param name="StationId">The station it is parked at, or null if it is not in a hangar.</param>
public sealed record ActiveShipResponse(
    long? HullItemInstanceId, string? Name, int? StationId, bool Aboard);

/// <summary>
/// Summoning a hull you own, and finding the hold of the ship you have with you (ADR-0012).
/// </summary>
/// <remarks>
/// <para>
/// A player's own token rather than the service credential, unlike docking. Docking records where a
/// ship <em>is</em>, which only the simulation knows; summoning is a player asking for something
/// they own, and every fact it depends on — being docked, at what kind of station, owning the hull —
/// is already a row the server can check for itself. A client that lies gets a refusal, not a ship.
/// </para>
/// <para>
/// The service credential is still accepted, because the dedicated server is the thing that will
/// eventually offer summoning as a station action on the player's behalf.
/// </para>
/// </remarks>
public static class ShipEndpoints
{
    public static void MapShipEndpoints(this IEndpointRouteBuilder routes)
    {
        RouteGroupBuilder group = routes.MapGroup("/ships").WithTags("Ships");

        group.MapPost("/summon", SummonAsync);
        group.MapGet("/{characterId:int}/hold", HoldAsync);
        group.MapGet("/{characterId:int}/active", ActiveAsync);
        group.MapPost("/board", BoardAsync);
        group.MapPost("/disembark", DisembarkAsync);
    }

    /// <summary>
    /// Which hull is this character's, and where is it sitting.
    /// </summary>
    /// <remarks>
    /// What the game server asks so it can put a ship in the world where the player can walk to it.
    /// A player's own token works too, because every field is one they can already see in the Ships
    /// tab.
    /// </remarks>
    private static async Task<IResult> ActiveAsync(
        int characterId,
        HttpContext context,
        Caller caller,
        ShipService ships,
        CancellationToken cancellation)
    {
        OwnershipResult owned =
            await caller.ServiceOrOwnedCharacterAsync(context, characterId, cancellation);

        if (owned.Status != OwnershipStatus.Owned)
        {
            return owned.ToProblem();
        }

        ShipService.ActiveShip? active = await ships.ActiveShipAsync(characterId, cancellation);

        // A 200 with nulls rather than a 404, exactly as the hold answers. Owning no ship is the
        // ordinary state for most of the opening, and a missing-resource error would have callers
        // treating it as a fault.
        return Results.Ok(active is null
            ? new ActiveShipResponse(null, null, null, false)
            : new ActiveShipResponse(
                active.HullItemInstanceId, active.Name, active.StationId, active.Aboard));
    }

    /// <summary>
    /// Records that a character has climbed into one of their hulls.
    /// </summary>
    /// <remarks>
    /// <strong>Service credential only, unlike summoning, and the difference is the same one
    /// docking draws.</strong> Summoning is a request the server can check every fact of from its
    /// own rows. Being aboard is a fact about where a body is in the world, which only the
    /// simulation knows — and it opens a hold, so a client that could assert it could open its own
    /// cargo from anywhere in the game.
    /// </remarks>
    private static async Task<IResult> BoardAsync(
        BoardShipRequest request,
        HttpContext context,
        ServiceCredential service,
        ShipService ships,
        CancellationToken cancellation)
    {
        if (!service.IsServiceCaller(context))
        {
            return Results.Problem(
                title: "Who is sitting in what is decided by the game server.",
                detail: "Only the simulation knows who boarded anything.",
                statusCode: StatusCodes.Status401Unauthorized);
        }

        try
        {
            await ships.BoardAsync(request.CharacterId, request.HullItemInstanceId, cancellation);

            return Results.Ok(new ActiveShipResponse(
                request.HullItemInstanceId, null, null, true));
        }
        catch (ShipSummonException refusal)
        {
            // 409 like summoning's refusals: nothing about the request is malformed, and the game
            // server asking to board a hull that is not this character's is a fault worth reading
            // rather than a validation error.
            return Results.Problem(
                title: refusal.Message,
                detail: "cannot_board",
                statusCode: StatusCodes.Status409Conflict);
        }
    }

    private static async Task<IResult> DisembarkAsync(
        DisembarkRequest request,
        HttpContext context,
        ServiceCredential service,
        ShipService ships,
        CancellationToken cancellation)
    {
        if (!service.IsServiceCaller(context))
        {
            return Results.Problem(
                title: "Who is sitting in what is decided by the game server.",
                statusCode: StatusCodes.Status401Unauthorized);
        }

        try
        {
            await ships.DisembarkAsync(request.CharacterId, cancellation);

            return Results.Ok(new ActiveShipResponse(null, null, null, false));
        }
        catch (ShipSummonException refusal)
        {
            return Results.Problem(
                title: refusal.Message, statusCode: StatusCodes.Status404NotFound);
        }
    }

    private static async Task<IResult> SummonAsync(
        SummonShipRequest request,
        HttpContext context,
        Caller caller,
        ShipService ships,
        CancellationToken cancellation)
    {
        OwnershipResult owned =
            await caller.ServiceOrOwnedCharacterAsync(context, request.CharacterId, cancellation);

        if (owned.Status != OwnershipStatus.Owned)
        {
            return owned.ToProblem();
        }

        try
        {
            Inventory hold = await ships.SummonAsync(
                request.CharacterId, request.HullItemInstanceId, cancellation);

            return Results.Ok(new ShipHoldResponse(hold.Id, hold.CapacityM3));
        }
        catch (ShipSummonException refused)
        {
            // 409 rather than 400: nothing about the request is malformed, and every one of these
            // is a fact about the world that could be different in a minute -- walk to a spaceport,
            // dock, craft a hull. The message is written to be shown to a player as it stands.
            return Results.Conflict(new { error = refused.Message, reason = "cannot_summon" });
        }
    }

    private static async Task<IResult> HoldAsync(
        int characterId,
        HttpContext context,
        Caller caller,
        ShipService ships,
        CancellationToken cancellation)
    {
        OwnershipResult owned =
            await caller.ServiceOrOwnedCharacterAsync(context, characterId, cancellation);

        if (owned.Status != OwnershipStatus.Owned)
        {
            return owned.ToProblem();
        }

        Inventory? hold = await ships.ReachableHoldAsync(characterId, cancellation);

        // Not a 404. "You have no ship here" is an ordinary answer to an ordinary question, and the
        // client asks it every time an inventory screen opens; a missing-resource error would have
        // callers treating a normal state as a fault.
        return Results.Ok(new ShipHoldResponse(hold?.Id, hold?.CapacityM3 ?? 0.0));
    }
}
