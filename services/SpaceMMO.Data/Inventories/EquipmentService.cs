using Microsoft.EntityFrameworkCore;
using SpaceMMO.Data.Entities;
using SpaceMMO.Domain.Items;

namespace SpaceMMO.Data.Inventories;

/// <summary>Thrown when something cannot be equipped or taken off, with a reason code for the client.</summary>
public sealed class EquipmentException(string message, string reason) : InvalidOperationException(message)
{
    /// <summary><c>not_yours</c>, <c>not_equippable</c>, <c>wrong_slot</c>, <c>not_here</c>, <c>empty_slot</c> or <c>no_room</c>.</summary>
    public string Reason { get; } = reason;
}

/// <summary>What equipping did: the slot used, and what came out of it to make room, if anything.</summary>
public readonly record struct EquipResult(EquipmentSlot Slot, long? SwappedOutInstanceId);

/// <summary>
/// The only way into and out of a character's slots (task 193).
/// </summary>
/// <remarks>
/// <para>
/// <strong>The transfer routes refuse the Equipped inventory</strong> (<c>InventoryService</c>), so every item
/// that is worn got there through here, with a slot it fits, from somewhere its owner could reach.
/// </para>
/// <para>
/// <strong>Reach, as Joe decided on 16 October:</strong> the pockets, a ship's hold, or the hangar of the
/// station the character is docked at; the same places the transfer rules already let goods move between.
/// Equipping is meant to be refused mid-fight too, "out of combat", and nothing fights yet: that rule waits
/// for the first weapon to fire.
/// </para>
/// </remarks>
public sealed class EquipmentService(SpaceMmoDbContext database)
{
    private readonly SpaceMmoDbContext _database =
        database ?? throw new ArgumentNullException(nameof(database));

    private readonly InventoryService _inventories = new(database);

    /// <summary>
    /// Puts an item in a slot: the one named, or the first empty one it fits, or else its first, swapping out
    /// what is there.
    /// </summary>
    /// <remarks>
    /// What comes out goes back where the new item came from, so a swap never strands anything. Moving a weapon
    /// from one weapon slot to the other swaps the two.
    /// </remarks>
    /// <exception cref="EquipmentException">If the item is not theirs, is not worn, does not fit, or is out of reach.</exception>
    public async Task<EquipResult> EquipAsync(
        int characterId,
        long itemInstanceId,
        EquipmentSlot? slot = null,
        CancellationToken cancellationToken = default)
    {
        await using var transaction = await _database.Database.BeginTransactionAsync(cancellationToken);

        Character character = await _database.Characters.SingleOrDefaultAsync(
            c => c.Id == characterId, cancellationToken)
            ?? throw new EquipmentException($"No character {characterId}.", "not_yours");

        ItemInstance? item = await _database.ItemInstances
            .Include(i => i.ItemDef)
            .Include(i => i.Inventory)
            .FirstOrDefaultAsync(i => i.Id == itemInstanceId, cancellationToken);

        if (item?.Inventory is null || item.DestroyedAt is not null || item.Inventory.CharacterId != characterId)
        {
            throw new EquipmentException($"Item {itemInstanceId} is not yours to equip.", "not_yours");
        }

        if (item.ItemDef!.EquipsAs is not EquipKind kind)
        {
            throw new EquipmentException($"A {item.ItemDef.Name} cannot be equipped.", "not_equippable");
        }

        Inventory equipped = await _inventories.GetOrCreateEquippedAsync(characterId, cancellationToken);

        bool wasEquipped = item.InventoryId == equipped.Id;

        if (!wasEquipped)
        {
            GuardReachable(character, item.Inventory);
        }

        Dictionary<EquipmentSlot, ItemInstance> worn = await WornAsync(equipped.Id, cancellationToken);

        EquipmentSlot target = slot ?? Equipment.Choose(
            kind, [.. worn.Keys.Where(s => s != item.EquippedSlot)]);

        if (!Equipment.Fits(kind, target))
        {
            throw new EquipmentException(
                $"A {item.ItemDef.Name} does not go in {Describe(target)}.", "wrong_slot");
        }

        if (wasEquipped && item.EquippedSlot == target)
        {
            await transaction.CommitAsync(cancellationToken);

            return new EquipResult(target, null);
        }

        worn.TryGetValue(target, out ItemInstance? occupant);

        if (occupant is not null && !wasEquipped)
        {
            await GuardRoomForSwapAsync(item.Inventory, item, occupant, cancellationToken);
        }

        long sourceInventoryId = item.InventoryId!.Value;
        EquipmentSlot? sourceSlot = item.EquippedSlot;

        // Out of both slots first, saved, then into the new ones. One slot to an item is a unique index, and
        // Postgres checks it row by row: written in one go, a swap would collide with itself halfway through.
        item.EquippedSlot = null;

        if (occupant is not null)
        {
            occupant.EquippedSlot = null;
        }

        await _database.SaveChangesAsync(cancellationToken);

        if (occupant is not null)
        {
            if (wasEquipped)
            {
                occupant.EquippedSlot = sourceSlot;
            }
            else
            {
                occupant.InventoryId = sourceInventoryId;
            }
        }

        item.InventoryId = equipped.Id;
        item.EquippedSlot = target;

        await _database.SaveChangesAsync(cancellationToken);
        await transaction.CommitAsync(cancellationToken);

        return new EquipResult(target, occupant?.Id);
    }

    /// <summary>
    /// Takes an item out of a slot: into the container named, or else the pockets, or the docked hangar when
    /// the pockets have no room.
    /// </summary>
    /// <param name="toInventoryId">
    /// Where it was dragged to, if anywhere. Held to the rules equipping is: the character's own, within
    /// reach, and with room. Double-clicking a slot names nowhere, and gets the pockets.
    /// </param>
    /// <returns>The inventory it went to.</returns>
    /// <exception cref="EquipmentException">If the slot is empty, or there is nowhere for it to go.</exception>
    public async Task<long> UnequipAsync(
        int characterId,
        EquipmentSlot slot,
        long? toInventoryId = null,
        CancellationToken cancellationToken = default)
    {
        await using var transaction = await _database.Database.BeginTransactionAsync(cancellationToken);

        Character character = await _database.Characters.SingleOrDefaultAsync(
            c => c.Id == characterId, cancellationToken)
            ?? throw new EquipmentException($"No character {characterId}.", "not_yours");

        Inventory equipped = await _inventories.GetOrCreateEquippedAsync(characterId, cancellationToken);

        ItemInstance item = (await WornAsync(equipped.Id, cancellationToken)).GetValueOrDefault(slot)
            ?? throw new EquipmentException($"Nothing is in {Describe(slot)}.", "empty_slot");

        if (toInventoryId is long named)
        {
            Inventory into = await _database.Inventories.SingleOrDefaultAsync(
                i => i.Id == named && i.CharacterId == characterId, cancellationToken)
                ?? throw new EquipmentException($"Inventory {named} is not yours.", "not_yours");

            if (into.Kind == InventoryKind.Equipped)
            {
                throw new EquipmentException("It is already equipped.", "wrong_slot");
            }

            GuardReachable(character, into);

            double usedThere = await _inventories.UsedVolumeAsync(into.Id, cancellationToken);

            if (into.CapacityM3 > 0.0 && usedThere + item.ItemDef!.VolumeM3 > into.CapacityM3 + 1e-6)
            {
                throw new EquipmentException($"There is no room there for a {item.ItemDef.Name}.", "no_room");
            }

            item.EquippedSlot = null;
            item.InventoryId = into.Id;

            await _database.SaveChangesAsync(cancellationToken);
            await transaction.CommitAsync(cancellationToken);

            return into.Id;
        }

        Inventory pockets = await _inventories.GetOrCreateCarriedAsync(characterId, cancellationToken);

        double used = await _inventories.UsedVolumeAsync(pockets.Id, cancellationToken);
        bool fits = pockets.CapacityM3 <= 0.0 || used + item.ItemDef!.VolumeM3 <= pockets.CapacityM3 + 1e-6;

        Inventory destination;

        if (fits)
        {
            destination = pockets;
        }
        else if (character.DockedStationId is int stationId)
        {
            destination = await _inventories.GetOrCreateStationHangarAsync(characterId, stationId, cancellationToken);
        }
        else
        {
            throw new EquipmentException(
                $"There is no room in your pockets for a {item.ItemDef!.Name}, and no hangar here to put it in.",
                "no_room");
        }

        item.EquippedSlot = null;
        item.InventoryId = destination.Id;

        await _database.SaveChangesAsync(cancellationToken);
        await transaction.CommitAsync(cancellationToken);

        return destination.Id;
    }

    /// <summary>
    /// Puts a newly made item straight into its slot, if one is empty (Joe, 16 October).
    /// </summary>
    /// <returns>True if it went in; false if every slot it fits is taken, and it stays where it is.</returns>
    /// <remarks>
    /// Called inside the claim's own transaction, so it opens none and saves nothing. Only an empty slot: a
    /// second laser never takes the first one off.
    /// </remarks>
    public async Task<bool> EquipIfSlotEmptyAsync(
        int characterId, ItemInstance item, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(item);

        EquipKind? kind = item.ItemDef?.EquipsAs ?? await _database.ItemDefs
            .Where(d => d.Id == item.ItemDefId)
            .Select(d => d.EquipsAs)
            .SingleAsync(cancellationToken);

        if (kind is not { } equipsAs)
        {
            return false;
        }

        Inventory equipped = await _inventories.GetOrCreateEquippedAsync(characterId, cancellationToken);

        EquipmentSlot? empty = Equipment.FirstEmpty(
            equipsAs, [.. (await WornAsync(equipped.Id, cancellationToken)).Keys]);

        if (empty is not { } slot)
        {
            return false;
        }

        item.InventoryId = equipped.Id;
        item.EquippedSlot = slot;

        return true;
    }

    /// <summary>
    /// Equips a tool for everyone who owns one and has none in its slot (task 193). Run by <c>--seed</c>.
    /// </summary>
    /// <returns>Who was given what, for the seed's output.</returns>
    /// <remarks>
    /// <para>
    /// <strong>Why it exists.</strong> Tools must be equipped to work from the day this lands, and every mining
    /// laser made before it is sitting in a hangar or a pocket. Without this, every existing character would
    /// find mining refused the next time they played, for want of a step the game had never asked of them.
    /// </para>
    /// <para>
    /// From anywhere they own one, ignoring reach: this is the server tidying its own change, not a player
    /// equipping. The best condition first, so a broken spare is not the one chosen. Idempotent: a slot
    /// already filled is left alone.
    /// </para>
    /// </remarks>
    public async Task<IReadOnlyList<string>> EquipOwnedToolsAsync(CancellationToken cancellationToken = default)
    {
        EquipKind[] toolKinds = [.. Enum.GetValues<EquipKind>().Where(k => Equipment.CategoryMayBe(ItemCategory.Tool, k))];

        List<ItemInstance> unworn = await _database.ItemInstances
            .Include(i => i.ItemDef)
            .Include(i => i.Inventory)
            .Where(i => i.DestroyedAt == null
                && i.Inventory != null
                && i.Inventory.Kind != InventoryKind.Equipped
                && i.ItemDef!.EquipsAs != null
                && toolKinds.Contains(i.ItemDef.EquipsAs!.Value))
            .ToListAsync(cancellationToken);

        var given = new List<string>();

        foreach (IGrouping<int, ItemInstance> owned in unworn.GroupBy(i => i.Inventory!.CharacterId))
        {
            Inventory equipped = await _inventories.GetOrCreateEquippedAsync(owned.Key, cancellationToken);
            HashSet<EquipmentSlot> worn = [.. (await WornAsync(equipped.Id, cancellationToken)).Keys];

            foreach (IGrouping<EquipKind, ItemInstance> ofKind in owned.GroupBy(i => i.ItemDef!.EquipsAs!.Value))
            {
                if (Equipment.FirstEmpty(ofKind.Key, worn) is not { } slot)
                {
                    continue;
                }

                ItemInstance best = ofKind.OrderByDescending(i => i.Condition).ThenBy(i => i.Id).First();

                best.InventoryId = equipped.Id;
                best.EquippedSlot = slot;
                worn.Add(slot);

                string name = await _database.Characters
                    .Where(c => c.Id == owned.Key)
                    .Select(c => c.Name)
                    .SingleAsync(cancellationToken);

                given.Add($"{name}: {best.ItemDef!.Name} into {Describe(slot)}");
            }
        }

        await _database.SaveChangesAsync(cancellationToken);

        return given;
    }

    /// <summary>What is in each slot, by slot.</summary>
    public async Task<Dictionary<EquipmentSlot, ItemInstance>> WornAsync(
        long equippedInventoryId, CancellationToken cancellationToken = default) =>
        await _database.ItemInstances
            .Include(i => i.ItemDef)
            .Where(i => i.InventoryId == equippedInventoryId && i.EquippedSlot != null && i.DestroyedAt == null)
            .ToDictionaryAsync(i => i.EquippedSlot!.Value, cancellationToken);

    /// <summary>
    /// Refuses a swap when what comes out would not fit where the new item came from: a bulky rifle traded
    /// for a slim one out of full pockets.
    /// </summary>
    /// <remarks>
    /// Refused rather than spilled into a hangar, because a swap the player watches go one way should not
    /// quietly send half of it somewhere else. Taking the old one off first says where it goes.
    /// </remarks>
    private async Task GuardRoomForSwapAsync(
        Inventory source, ItemInstance incoming, ItemInstance outgoing, CancellationToken cancellationToken)
    {
        if (source.CapacityM3 <= 0.0)
        {
            return;
        }

        double used = await _inventories.UsedVolumeAsync(source.Id, cancellationToken);
        double after = used - incoming.ItemDef!.VolumeM3 + outgoing.ItemDef!.VolumeM3;

        if (after > source.CapacityM3 + 1e-6)
        {
            throw new EquipmentException(
                $"There is no room for the {outgoing.ItemDef.Name} this would take off. Take it off first.",
                "no_room");
        }
    }

    /// <summary>The pockets, a ship's hold, or the hangar of the station they are docked at.</summary>
    private static void GuardReachable(Character character, Inventory from)
    {
        bool reachable = from.Kind switch
        {
            InventoryKind.CharacterCarried => true,
            InventoryKind.ShipHold => true,
            InventoryKind.StationHangar => from.StationId is int at && character.DockedStationId == at,
            _ => false,
        };

        if (!reachable)
        {
            throw new EquipmentException(
                "That hangar is at a station you are not docked at.", "not_here");
        }
    }

    private static string Describe(EquipmentSlot slot) => slot switch
    {
        EquipmentSlot.Weapon1 => "Weapon 1",
        EquipmentSlot.Weapon2 => "Weapon 2",
        EquipmentSlot.MiningTool => "the mining tool slot",
        _ => $"the {slot.ToString().ToLowerInvariant()} slot",
    };
}
