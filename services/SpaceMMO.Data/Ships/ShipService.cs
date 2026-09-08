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
    /// If the character is not docked, the instance is not theirs, it is not a hull, or it would
    /// have to be <em>brought</em> to a station ships do not come to. A hull already parked in this
    /// station's own hangar is fetched back wherever that station is (task 153).
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

        // <strong>Fetching back what you parked here is not the same act as having one brought.</strong>
        // The kind gate is about a hull *arriving* somewhere -- "a market hub, a house and a bar are
        // not places a hull arrives" -- and a ship already sitting in this station's hangar has
        // arrived, under its own power, with a pilot who flew it.
        //
        // It became load-bearing on 7 September, when docking started putting ships away (task 153).
        // Docking at Terra Outpost parks the hull in Terra's hangar, and a gate that refused to open
        // it would leave a player on foot on Terra with their only ship locked in the building in
        // front of them and no way to reach a spaceport to ask for it.
        bool alreadyHere = hull.Inventory.StationId == stationId;

        if (!alreadyHere && !station.Kind.AllowsShipSummoning())
        {
            throw new ShipSummonException(
                $"{station.Name} is a {station.Kind} and ships are not summoned there.");
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
    /// Parks a hull in a station's hangar, and steps its pilot out of it.
    /// </summary>
    /// <remarks>
    /// <para>
    /// <strong>The other half of the sentence <see cref="SummonAsync"/> starts (task 153).</strong>
    /// ADR-0012 says a parked ship's inventory <em>is</em> the station hangar it was left in, and
    /// summoning is how it comes out. Nothing put it back, so a ship anybody docked stood on the
    /// apron while its row said it was inside — the world and the record disagreeing about one of
    /// the few things ADR-0012 made unambiguous.
    /// </para>
    /// <para>
    /// <strong>The hull is named rather than read off <see cref="Character.AboardShipItemInstanceId"/>.</strong>
    /// Docking a ship possesses a character pawn, and possession is what reports boarding — so the
    /// game server sends a disembark for the same keypress, and a stow that asked what somebody was
    /// aboard would race it and find nothing. Naming the hull makes the two independent, and it is
    /// what the caller knows anyway: it is the pawn it just removed from the world.
    /// </para>
    /// <para>
    /// <strong>Called only when the simulation actually removed the pawn</strong>, not from
    /// <see cref="Docking.DockingService.DockAsync"/>. Docking on foot has nothing to do with
    /// hulls, and a station with nowhere to step out at — Deepdock, which orbits nothing — leaves
    /// the ship alongside. Moving the hull there anyway would recreate exactly the disagreement
    /// this exists to remove, one station further out.
    /// </para>
    /// </remarks>
    /// <exception cref="ShipSummonException">
    /// If the character or station does not exist, or the hull is not this character's.
    /// </exception>
    public async Task StowAsync(
        int characterId,
        long hullInstanceId,
        int stationId,
        CancellationToken cancellationToken = default)
    {
        Character character = await _database.Characters
            .FirstOrDefaultAsync(c => c.Id == characterId, cancellationToken)
            ?? throw new ShipSummonException($"No character {characterId}.");

        Station station = await _database.Stations
            .FirstOrDefaultAsync(s => s.Id == stationId, cancellationToken)
            ?? throw new ShipSummonException($"No station {stationId}.");

        ItemInstance? hull = await _database.ItemInstances
            .Include(i => i.ItemDef)
            .Include(i => i.Inventory)
            .FirstOrDefaultAsync(i => i.Id == hullInstanceId, cancellationToken);

        // Ownership through the inventory the instance sits in, the same way summoning and boarding
        // read it. Parking is a write to somebody's hangar, so an id that arrived wrong would move
        // another player's ship across the system.
        if (hull?.Inventory is null || hull.Inventory.CharacterId != characterId)
        {
            throw new ShipSummonException($"Hull {hullInstanceId} is not yours to park.");
        }

        if (hull.ItemDef!.Category != ItemCategory.Hull)
        {
            throw new ShipSummonException(
                $"A {hull.ItemDef.Name} is a {hull.ItemDef.Category}, not something you can park.");
        }

        Inventory hangar = await _inventories.GetOrCreateStationHangarAsync(
            characterId, station.Id, cancellationToken);

        hull.InventoryId = hangar.Id;

        // <strong>And it is no longer standing anywhere.</strong> Null is what tells a hangar from
        // an apron (task 155), and without clearing it the next sign-in would put a pawn back at
        // the spot the ship was docked from -- which is the fault this whole pair of tasks is
        // about, moved one restart later.
        hull.DeployedSystemX = null;
        hull.DeployedSystemY = null;
        hull.DeployedSystemZ = null;

        // Out of the ship as well as into the hangar, and only if it was this one. Being aboard is
        // what opens a hold from anywhere (ADR-0012 point 4), and a character left aboard a hull
        // that is now inside a building could fly away and still reach its cargo.
        if (character.AboardShipItemInstanceId == hull.Id)
        {
            character.AboardShipItemInstanceId = null;
        }

        // Still their ship. Stepping out of your shuttle does not stop it being your shuttle, and
        // ActiveShipItemInstanceId is what SummonAsync brings back out.
        await _database.SaveChangesAsync(cancellationToken);
    }

    /// <summary>Where a character's active hull is, and what it is called.</summary>
    /// <param name="StationId">The station whose hangar owns it, or null if no station does.</param>
    /// <param name="Deployed">
    /// Whether it is standing in the world rather than put away.
    /// <para>
    /// <strong>Not implied by <paramref name="StationId"/>, and that is the whole point of task
    /// 155.</strong> A hull summoned to a station and a hull docked at one are the same row —
    /// both sit in that station's hangar inventory — and only this says whether there is a pawn
    /// outside. Reading the station alone is what put a stowed ship back in the world on sign-in.
    /// </para>
    /// </param>
    /// <param name="Position">Where it stands, in system kilometres. Null unless deployed.</param>
    public sealed record ActiveShip(
        long HullItemInstanceId,
        string Name,
        int? StationId,
        bool Aboard,
        bool Deployed,
        (double X, double Y, double Z)? Position);

    /// <summary>
    /// Which hull this character would fly and where it is sitting, or null if they have none.
    /// </summary>
    /// <remarks>
    /// <strong>What the game server asks so it can put a ship in the world.</strong> Summoning
    /// records which hull is yours and moves it into a hangar; nothing about that puts a pawn
    /// anywhere, and the party that can is the simulation. It needs the station to place it at and
    /// the name to say out loud, and both are one row away from the answer this already computes.
    /// </remarks>
    public async Task<ActiveShip?> ActiveShipAsync(
        int characterId, CancellationToken cancellationToken = default)
    {
        Character? character = await _database.Characters
            .FirstOrDefaultAsync(c => c.Id == characterId, cancellationToken);

        if (character?.ActiveShipItemInstanceId is not long hullId)
        {
            return null;
        }

        ItemInstance? hull = await _database.ItemInstances
            .Include(i => i.ItemDef)
            .Include(i => i.Inventory)
            .FirstOrDefaultAsync(i => i.Id == hullId, cancellationToken);

        if (hull is null)
        {
            return null;
        }

        return new ActiveShip(
            hull.Id,
            hull.ItemDef?.Name ?? "Ship",
            hull.Inventory?.StationId,
            character.AboardShipItemInstanceId == hull.Id,
            hull.IsDeployed,
            hull.IsDeployed
                ? (hull.DeployedSystemX!.Value, hull.DeployedSystemY ?? 0.0, hull.DeployedSystemZ ?? 0.0)
                : null);
    }

    /// <summary>
    /// Records that this character has climbed into one of their hulls.
    /// </summary>
    /// <remarks>
    /// <para>
    /// <strong>Ownership is checked here rather than trusted</strong>, even though only the game
    /// server may call it. Being aboard is what opens a hold, so an id that arrived wrong — a stale
    /// pawn, a mistaken cast, a hull sold while somebody sat in it — would open somebody else's
    /// cargo. The check costs one row.
    /// </para>
    /// <para>
    /// Boarding also makes that hull the active one, because the ship you are flying is the ship
    /// you are flying. A player who owns two and climbs into the other has changed their mind, and
    /// making them summon it again to say so would be ceremony.
    /// </para>
    /// </remarks>
    /// <exception cref="ShipSummonException">If the hull is not this character's, or is not a hull.</exception>
    public async Task BoardAsync(
        int characterId, long hullInstanceId, CancellationToken cancellationToken = default)
    {
        Character character = await _database.Characters
            .FirstOrDefaultAsync(c => c.Id == characterId, cancellationToken)
            ?? throw new ShipSummonException($"No character {characterId}.");

        ItemInstance? hull = await _database.ItemInstances
            .Include(i => i.ItemDef)
            .Include(i => i.Inventory)
            .FirstOrDefaultAsync(i => i.Id == hullInstanceId, cancellationToken);

        if (hull?.Inventory is null || hull.Inventory.CharacterId != characterId)
        {
            throw new ShipSummonException($"Hull {hullInstanceId} is not yours to board.");
        }

        if (hull.ItemDef!.Category != ItemCategory.Hull)
        {
            throw new ShipSummonException(
                $"A {hull.ItemDef.Name} is a {hull.ItemDef.Category}, not something you can sit in.");
        }

        character.AboardShipItemInstanceId = hull.Id;
        character.ActiveShipItemInstanceId = hull.Id;

        await _database.SaveChangesAsync(cancellationToken);
    }

    /// <summary>
    /// Records where one of this character's hulls is standing in the world (task 155).
    /// </summary>
    /// <remarks>
    /// <para>
    /// <strong>The hull's half of task 147.</strong> A character comes back where the world last saw
    /// them; a ship did not, because nothing wrote down where it was — so landing on a hillside,
    /// stepping out and quitting put the shuttle back at the hangar it was summoned to. This is the
    /// same periodic write, for the thing the player was sitting in.
    /// </para>
    /// <para>
    /// <strong>It also makes a ship visible at all.</strong> A hull with no position is inside a
    /// hangar and gets no pawn, so this is what the game server calls the moment it puts one in the
    /// world — the record following the world, exactly as <see cref="StowAsync"/> does going the
    /// other way.
    /// </para>
    /// <para>
    /// Ownership is checked, as everywhere else here: this writes a position onto an item, and an id
    /// that arrived wrong would move somebody else's ship.
    /// </para>
    /// </remarks>
    /// <exception cref="ShipSummonException">If the character or hull does not exist, or is not theirs.</exception>
    public async Task RecordShipWhereaboutsAsync(
        int characterId,
        long hullInstanceId,
        double x,
        double y,
        double z,
        CancellationToken cancellationToken = default)
    {
        ItemInstance? hull = await _database.ItemInstances
            .Include(i => i.Inventory)
            .FirstOrDefaultAsync(i => i.Id == hullInstanceId, cancellationToken);

        if (hull?.Inventory is null || hull.Inventory.CharacterId != characterId)
        {
            throw new ShipSummonException($"Hull {hullInstanceId} is not yours to place.");
        }

        hull.DeployedSystemX = x;
        hull.DeployedSystemY = y;
        hull.DeployedSystemZ = z;

        await _database.SaveChangesAsync(cancellationToken);
    }

    /// <summary>Records that this character has stepped out of whatever they were in.</summary>
    /// <remarks>
    /// Idempotent, exactly as undocking is, and for the same reason: a ship is left in ways nobody
    /// sends a message about. The active ship is deliberately left alone — stepping out of your
    /// shuttle does not stop it being your shuttle.
    /// </remarks>
    public async Task DisembarkAsync(int characterId, CancellationToken cancellationToken = default)
    {
        Character character = await _database.Characters
            .FirstOrDefaultAsync(c => c.Id == characterId, cancellationToken)
            ?? throw new ShipSummonException($"No character {characterId}.");

        character.AboardShipItemInstanceId = null;

        await _database.SaveChangesAsync(cancellationToken);
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
    /// <strong>Both halves of the rule now.</strong> ADR-0012 says a hold is reachable "docked at a
    /// station with their active ship, <em>or sitting in that ship</em>". Sitting in it is
    /// <see cref="Character.AboardShipItemInstanceId"/>, written when the pawn is possessed — not
    /// inferred from being undocked, which a character walking around a planet also is, and which
    /// would open the hold from a rock.
    /// </para>
    /// <para>
    /// <strong>Aboard is checked against the active hull rather than believed on its own.</strong>
    /// The active hull is the one ownership was proved for, at summoning and at boarding. Reading
    /// the aboard id straight through would make the hold reachable for whatever the game server
    /// last said somebody was sitting in, which is one stale pawn away from somebody else's cargo.
    /// </para>
    /// </remarks>
    public async Task<Inventory?> ReachableHoldAsync(
        int characterId, CancellationToken cancellationToken = default)
    {
        Character? character = await _database.Characters
            .FirstOrDefaultAsync(c => c.Id == characterId, cancellationToken);

        if (character?.ActiveShipItemInstanceId is not long hullId)
        {
            return null;
        }

        // Sitting in it. Nothing else has to be true -- a ship in flight is not docked anywhere,
        // and that is the whole point of the second half of the rule.
        bool aboard = character.AboardShipItemInstanceId == hullId;

        if (!aboard)
        {
            if (character.DockedStationId is not int stationId)
            {
                return null;
            }

            // The active hull has to be parked where the player is standing. Somebody who flew home
            // and left their freighter at the capital has an active ship they are nowhere near, and
            // its hold is exactly as out of reach as the hangar beside it.
            ItemInstance? hull = await _database.ItemInstances
                .Include(i => i.Inventory)
                .FirstOrDefaultAsync(i => i.Id == hullId, cancellationToken);

            if (hull?.Inventory is null || hull.Inventory.StationId != stationId)
            {
                return null;
            }
        }

        return await _database.Inventories.FirstOrDefaultAsync(
            i => i.ShipItemInstanceId == hullId && i.Kind == InventoryKind.ShipHold,
            cancellationToken);
    }
}
