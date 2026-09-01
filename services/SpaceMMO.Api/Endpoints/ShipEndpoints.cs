using SpaceMMO.Api.Auth;
using SpaceMMO.Data.Entities;
using SpaceMMO.Data.Ships;

namespace SpaceMMO.Api.Endpoints;

public sealed record SummonShipRequest(int CharacterId, long HullItemInstanceId);

/// <summary>What a character can reach of their ship right now.</summary>
/// <param name="HoldInventoryId">The hold, or null if their ship is not with them.</param>
/// <param name="CapacityM3">How much it carries. Zero when there is no hold to speak of.</param>
public sealed record ShipHoldResponse(long? HoldInventoryId, double CapacityM3);

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
