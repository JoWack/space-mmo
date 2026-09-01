using Microsoft.EntityFrameworkCore;
using SpaceMMO.Data.Entities;
using SpaceMMO.Data.Inventories;
using SpaceMMO.Domain.Items;
using SpaceMMO.Domain.Universe;

namespace SpaceMMO.Data.Ships;

/// <summary>
/// Thrown when a ship cannot be summoned, and why.
/// </summary>
/// <remarks>
/// Its own type rather than a bare <see cref="InvalidOperationException"/>, because every one of
/// these reaches a player as a sentence: standing at the wrong kind of station, asking for somebody
/// else's hull, or asking for something that is not a hull at all are three different mistakes and
/// only one of them is worth walking somewhere to fix.
/// </remarks>
public sealed class ShipSummonException(string message) : InvalidOperationException(message);

/// <summary>
/// Turning an owned hull into the ship a player flies (ADR-0012).
/// </summary>
/// <remarks>
/// <para>
/// <strong>Where a ship is needs no column.</strong> An owned hull is an <see cref="ItemInstance"/>
/// sitting in an inventory, and for a parked ship that inventory is the station hangar it was left
/// in. Summoning moves the instance to the hangar of the station the player is standing in, which is
/// what "summoning elsewhere moves it" means in rows — and it means a ship can always be found,
/// because it is somewhere by construction rather than by a coordinate somebody has to maintain.
/// </para>
/// <para>
/// Free, instant, and with no fuel or repair gate, all settled by Joe on 31 August against the open
/// questions ADR-0012 left. Each is a later change to this one method rather than to the shape
/// around it.
/// </para>
/// </remarks>
public sealed class ShipService(SpaceMmoDbContext database)
{
    private readonly SpaceMmoDbContext _database =
        database ?? throw new ArgumentNullException(nameof(database));

    private readonly InventoryService _inventories = new(database);

    /// <summary>
    /// Makes an owned hull the character's active ship, bringing it to the station they are at.
    /// </summary>
    /// <exception cref="ShipSummonException">
    /// If the character is not docked, the station is not one ships come to, the instance is not
    /// theirs, or it is not a hull.
    /// </exception>
    public async Task<Inventory> SummonAsync(
        int characterId, long hullInstanceId, CancellationToken cancellationToken = default)
    {
        Character? character = await _database.Characters
            .FirstOrDefaultAsync(c => c.Id == characterId, cancellationToken);

        if (character is null)
        {
            throw new ShipSummonException($"No character {characterId}.");
        }

        if (character.DockedStationId is not int stationId)
        {
            // Checked before anything else, because it is the one a player can act on without
            // knowing anything about their fleet.
            throw new ShipSummonException("You have to be docked to summon a ship.");
        }

        Station? station = await _database.Stations
            .FirstOrDefaultAsync(s => s.Id == stationId, cancellationToken);

        if (station is null)
        {
            throw new ShipSummonException($"No station {stationId}.");
        }

        if (!station.Kind.AllowsShipSummoning())
        {
            throw new ShipSummonException(
                $"{station.Name} is a {station.Kind} and ships are not summoned there.");
        }

        ItemInstance? hull = await _database.ItemInstances
            .Include(i => i.ItemDef)
            .Include(i => i.Inventory)
            .FirstOrDefaultAsync(i => i.Id == hullInstanceId, cancellationToken);

        if (hull is null || hull.Inventory is null)
        {
            throw new ShipSummonException($"No hull {hullInstanceId} that anybody owns.");
        }

        // Ownership through the inventory the instance sits in, which is the same way the gathering
        // tool gate reads it. A hull in somebody else's hangar is somebody else's hull however the
        // request was addressed, and this is the check a hostile client is testing.
        if (hull.Inventory.CharacterId != characterId)
        {
            throw new ShipSummonException($"Hull {hullInstanceId} belongs to somebody else.");
        }

        if (hull.ItemDef!.Category != ItemCategory.Hull)
        {
            throw new ShipSummonException(
                $"A {hull.ItemDef.Name} is a {hull.ItemDef.Category}, not something you can fly.");
        }

        // Brought to where the player is standing. A hull left at another station is not summoned
        // from a distance -- it moves, and afterwards it is parked here rather than there.
        Inventory hangar = await _inventories.GetOrCreateStationHangarAsync(
            characterId, stationId, cancellationToken);

        hull.InventoryId = hangar.Id;

        character.ActiveShipItemInstanceId = hull.Id;

        // The hold comes with it. Created here rather than at craft time because a hull that has
        // never been flown has nowhere to put anything, and a container nobody can reach is a row
        // that exists to be confusing.
        Inventory hold = await _inventories.GetOrCreateShipHoldAsync(hull.Id, cancellationToken);

        await _database.SaveChangesAsync(cancellationToken);

        return hold;
    }

    /// <summary>
    /// The hold of the ship this character has with them, or null if they have none to hand.
    /// </summary>
    /// <remarks>
    /// <para>
    /// <strong>Reachability is a rule about being present, like station stock (ADR-0012 point 4).</strong>
    /// Goods are somewhere, and being elsewhere means not having them; a hold that opened from
    /// anywhere would be a bank account you can fly, and ADR-0008's planet-locked materials would go
    /// back to being a shopping list rather than a journey.
    /// </para>
    /// <para>
    /// <strong>Half the rule, and the half the server can prove.</strong> ADR-0012 says a hold is
    /// reachable "docked at a station with their active ship, <em>or sitting in that ship</em>", and
    /// only the first half is checked here: nothing on the server knows whether a character is
    /// aboard. Being undocked cannot stand in for it either, because a character walking around a
    /// planet is undocked too, and that would open the hold from a rock. The second half wants the
    /// server to be told when somebody boards, which is a change to the pawn rather than to this.
    /// </para>
    /// </remarks>
    public async Task<Inventory?> ReachableHoldAsync(
        int characterId, CancellationToken cancellationToken = default)
    {
        Character? character = await _database.Characters
            .FirstOrDefaultAsync(c => c.Id == characterId, cancellationToken);

        if (character?.ActiveShipItemInstanceId is not long hullId
            || character.DockedStationId is not int stationId)
        {
            return null;
        }

        // The active hull has to be parked where the player is standing. Somebody who flew home and
        // left their freighter at the capital has an active ship they are nowhere near, and its hold
        // is exactly as out of reach as the hangar beside it.
        ItemInstance? hull = await _database.ItemInstances
            .Include(i => i.Inventory)
            .FirstOrDefaultAsync(i => i.Id == hullId, cancellationToken);

        if (hull?.Inventory is null || hull.Inventory.StationId != stationId)
        {
            return null;
        }

        return await _database.Inventories.FirstOrDefaultAsync(
            i => i.ShipItemInstanceId == hullId && i.Kind == InventoryKind.ShipHold,
            cancellationToken);
    }
}
