using Microsoft.EntityFrameworkCore;
using SpaceMMO.Data.Entities;
using SpaceMMO.Domain.Economy;
using SpaceMMO.Domain.Items;

namespace SpaceMMO.Data.Inventories;

/// <summary>
/// Thrown when an inventory does not hold enough of an item to satisfy a removal.
/// </summary>
public sealed class InsufficientItemsException(
    int itemDefId, int requested, int available)
    : InvalidOperationException(
        $"Inventory holds {available} of item {itemDefId} but {requested} were requested.")
{
    public int ItemDefId { get; } = itemDefId;

    public int Requested { get; } = requested;

    public int Available { get; } = available;
}

/// <summary>
/// Thrown when a transfer is asked to move something between inventories that cannot be paired.
/// </summary>
/// <remarks>
/// Both sides must belong to the same character. Ownership is the whole of the rule today, because
/// there is nowhere else for goods to go: everything gathered or crafted lands in a station hangar,
/// so a transfer is always between two of one person's own containers.
/// </remarks>
public sealed class InventoryTransferException(string message) : InvalidOperationException(message);

/// <summary>
/// Thrown when something will not fit in the container it is being put into.
/// </summary>
/// <remarks>
/// <para>
/// The whole delivery is refused, not part of it (ADR-0014). Mining half a swing of ore into a full
/// backpack and losing the rest is a silent loss of a player's property, and a partial market
/// purchase is a silent partial refund. Nothing moves, and this says what did not fit and how much
/// room there was.
/// </para>
/// <para>
/// Carrying the numbers rather than only a sentence, so a caller can word it for a player without
/// parsing a message: the deposit prompt wants "your pack is full", the market wants "you can afford
/// forty and can carry twelve".
/// </para>
/// </remarks>
public sealed class InventoryFullException(
    long inventoryId, double capacityM3, double usedM3, double wantedM3)
    : InvalidOperationException(
        $"Inventory {inventoryId} holds {usedM3:0.##} of {capacityM3:0.##} m3 and cannot take "
        + $"another {wantedM3:0.##} m3.")
{
    public long InventoryId { get; } = inventoryId;

    public double CapacityM3 { get; } = capacityM3;

    public double UsedM3 { get; } = usedM3;

    public double WantedM3 { get; } = wantedM3;

    /// <summary>How much room is left, which is what a player actually wants told.</summary>
    public double FreeM3 { get; } = Math.Max(0.0, capacityM3 - usedM3);
}

/// <summary>
/// Moves stackable items in and out of inventories.
/// </summary>
/// <remarks>
/// <para>
/// Stackable items only. Non-stackable categories are tracked as <see cref="ItemInstance"/>
/// rows because they carry condition and acquisition value, so moving them is a different
/// operation with different rules (ADR-0006).
/// </para>
/// <para>
/// Every method here participates in the caller's transaction and never commits on its own.
/// Market settlement has to move goods and credits atomically, so a service that committed
/// independently could leave items delivered against a payment that later rolled back.
/// </para>
/// </remarks>
public sealed class InventoryService(SpaceMmoDbContext database)
{
    private readonly SpaceMmoDbContext _database =
        database ?? throw new ArgumentNullException(nameof(database));

    /// <summary>
    /// What a character carries on their person, in cubic metres (ADR-0014).
    /// </summary>
    /// <remarks>
    /// <para>
    /// Fifteen units of ferrite ore, sixty of scrap, or three mining lasers. A resource node holds
    /// two hundred ore, so clearing one on foot is fourteen trips — which is the pressure that makes
    /// a ship worth crafting, without making the on-foot loop a punishment before there is anything
    /// to fly.
    /// </para>
    /// <para>
    /// Volume rather than the 50 kg of the original direction, because there is no mass anywhere in
    /// the schema and adding one would be two capacity systems that can disagree. It is not a figure
    /// that sounds like a backpack, and it was never going to be: the authored volumes are on a ship
    /// scale, where a single unit of ore is four hundred litres.
    /// </para>
    /// <para>
    /// A flat number. `stamina` raises it and a backpack raises it again, and neither exists —
    /// stamina is blocked behind tasks 101 and 102, and both are later changes to this one figure
    /// rather than to the rule around it.
    /// </para>
    /// </remarks>
    public const double CarriedCapacityM3 = 6.0;

    /// <summary>
    /// Adds a quantity, creating the stack row if the inventory does not already hold the item.
    /// </summary>
    /// <param name="cost">
    /// What these units cost their new owner. Zero for gathered material, which costs only labour;
    /// the price paid for bought material. Feeds the acquisition value of anything crafted from
    /// them (ADR-0006).
    /// </param>
    /// <exception cref="ArgumentOutOfRangeException">If quantity is not positive or cost is negative.</exception>
    /// <exception cref="InvalidOperationException">If the item is not stackable.</exception>
    public async Task AddAsync(
        long inventoryId,
        int itemDefId,
        int quantity,
        Credits cost,
        CancellationToken cancellationToken = default)
    {
        GuardQuantity(quantity);

        if (cost.IsNegative)
        {
            throw new ArgumentOutOfRangeException(nameof(cost), cost, "Cost cannot be negative.");
        }

        await GuardStackableAsync(itemDefId, cancellationToken);
        await GuardRoomAsync(inventoryId, itemDefId, quantity, cancellationToken);

        InventoryItem? stack = await _database.InventoryItems
            .FirstOrDefaultAsync(
                i => i.InventoryId == inventoryId && i.ItemDefId == itemDefId, cancellationToken);

        if (stack is null)
        {
            _database.InventoryItems.Add(new InventoryItem
            {
                InventoryId = inventoryId,
                ItemDefId = itemDefId,
                Quantity = quantity,
                CostBasis = cost,
            });

            return;
        }

        stack.Quantity += quantity;
        stack.CostBasis += cost;
    }

    /// <summary>
    /// Removes a quantity, deleting the stack row if it reaches zero.
    /// </summary>
    /// <returns>
    /// The share of the stack's cost basis that left with those units, so a caller consuming
    /// materials knows what they were worth.
    /// </returns>
    /// <remarks>
    /// <para>
    /// The row is deleted rather than left at zero because a zero-quantity stack is forbidden by a
    /// check constraint — "does the player have any?" should never also need to ask "but is it more
    /// than none?".
    /// </para>
    /// <para>
    /// The cost share is floored and the remainder stays with the stack, so removed plus remaining
    /// always sums back to the original exactly. Cost basis is never created or lost by splitting a
    /// stack.
    /// </para>
    /// </remarks>
    /// <exception cref="ArgumentOutOfRangeException">If quantity is not positive.</exception>
    /// <exception cref="InsufficientItemsException">If the inventory holds too few.</exception>
    public async Task<Credits> RemoveAsync(
        long inventoryId, int itemDefId, int quantity, CancellationToken cancellationToken = default)
    {
        GuardQuantity(quantity);

        InventoryItem? stack = await _database.InventoryItems
            .FirstOrDefaultAsync(
                i => i.InventoryId == inventoryId && i.ItemDefId == itemDefId, cancellationToken);

        int available = stack?.Quantity ?? 0;

        if (available < quantity)
        {
            throw new InsufficientItemsException(itemDefId, quantity, available);
        }

        if (available == quantity)
        {
            Credits whole = stack!.CostBasis;
            _database.InventoryItems.Remove(stack);

            return whole;
        }

        // Int128 keeps the intermediate exact; a large stack with a large basis would otherwise
        // overflow int64 on the multiply.
        Credits removed = Credits.FromMinorUnits(
            (long)((Int128)stack!.CostBasis.MinorUnits * quantity / available));

        stack.Quantity -= quantity;
        stack.CostBasis -= removed;

        return removed;
    }

    /// <summary>How many of an item an inventory holds. Zero if it holds none.</summary>
    public async Task<int> QuantityOfAsync(
        long inventoryId, int itemDefId, CancellationToken cancellationToken = default) =>
        await _database.InventoryItems
            .Where(i => i.InventoryId == inventoryId && i.ItemDefId == itemDefId)
            .Select(i => i.Quantity)
            .FirstOrDefaultAsync(cancellationToken);

    /// <summary>
    /// Moves a quantity of a stackable item from one of a character's inventories to another.
    /// </summary>
    /// <remarks>
    /// <para>
    /// <strong>Cost basis moves with the goods.</strong> Insurance is pegged to acquisition value
    /// (ADR-0006), so material that arrives somewhere having apparently cost nothing is material
    /// that pays out nothing when it is lost. <see cref="RemoveAsync"/> already returns the share
    /// that left, and this hands exactly that share to the destination — so a stack split across
    /// two containers still sums to what it originally cost, and moving goods can neither create
    /// nor destroy value.
    /// </para>
    /// <para>
    /// Volume is not checked. <c>CapacityM3</c> exists on the row and hangars are created at zero,
    /// and nothing anywhere enforces it yet; a transfer is the wrong place to invent that rule,
    /// because it would apply to one route into a hold and not to the others.
    /// </para>
    /// </remarks>
    /// <exception cref="InventoryTransferException">
    /// If the two inventories are the same, either is unknown, or they belong to different people.
    /// </exception>
    /// <exception cref="InsufficientItemsException">If the source holds too few.</exception>
    public async Task TransferAsync(
        long fromInventoryId,
        long toInventoryId,
        int itemDefId,
        int quantity,
        CancellationToken cancellationToken = default)
    {
        GuardQuantity(quantity);

        await GuardSameOwnerAsync(fromInventoryId, toInventoryId, cancellationToken);

        Credits moved = await RemoveAsync(fromInventoryId, itemDefId, quantity, cancellationToken);

        await AddAsync(toInventoryId, itemDefId, quantity, moved, cancellationToken);
    }

    /// <summary>
    /// Moves a single non-stackable item — a tool, a weapon, a ship — between a character's
    /// inventories.
    /// </summary>
    /// <remarks>
    /// A different operation from <see cref="TransferAsync"/> rather than a special case of it.
    /// An instance carries its own condition and acquisition value, so it moves as itself: the row
    /// changes container and nothing is split, summed or recreated.
    /// </remarks>
    /// <exception cref="InventoryTransferException">
    /// If the instance is unknown, has been destroyed, is already there, or the two inventories do
    /// not belong to the same character.
    /// </exception>
    public async Task TransferInstanceAsync(
        long itemInstanceId,
        long toInventoryId,
        CancellationToken cancellationToken = default)
    {
        ItemInstance? instance = await _database.ItemInstances
            .FirstOrDefaultAsync(i => i.Id == itemInstanceId, cancellationToken);

        if (instance is null)
        {
            throw new InventoryTransferException($"Item instance {itemInstanceId} does not exist.");
        }

        // A destroyed instance keeps its row so history survives it, and must not be recoverable by
        // moving it somewhere (ADR-0006).
        if (instance.DestroyedAt is not null || instance.InventoryId is not long fromInventoryId)
        {
            throw new InventoryTransferException(
                $"Item instance {itemInstanceId} has been destroyed.");
        }

        await GuardSameOwnerAsync(fromInventoryId, toInventoryId, cancellationToken);

        instance.InventoryId = toInventoryId;
    }

    /// <summary>
    /// Both inventories must exist and belong to one character.
    /// </summary>
    /// <remarks>
    /// Checked here rather than at the endpoint so no caller can move goods between two people by
    /// forgetting to. Giving items away is a trade, and a trade is the market's job — it has fees,
    /// an order book and a settlement path, none of which a silent transfer would go through.
    /// </remarks>
    private async Task GuardSameOwnerAsync(
        long fromInventoryId, long toInventoryId, CancellationToken cancellationToken)
    {
        if (fromInventoryId == toInventoryId)
        {
            throw new InventoryTransferException(
                "Source and destination are the same inventory.");
        }

        Dictionary<long, int> owners = await _database.Inventories
            .Where(i => i.Id == fromInventoryId || i.Id == toInventoryId)
            .ToDictionaryAsync(i => i.Id, i => i.CharacterId, cancellationToken);

        if (!owners.TryGetValue(fromInventoryId, out int fromOwner))
        {
            throw new InventoryTransferException($"Inventory {fromInventoryId} does not exist.");
        }

        if (!owners.TryGetValue(toInventoryId, out int toOwner))
        {
            throw new InventoryTransferException($"Inventory {toInventoryId} does not exist.");
        }

        if (fromOwner != toOwner)
        {
            throw new InventoryTransferException(
                $"Inventories {fromInventoryId} and {toInventoryId} belong to different characters.");
        }
    }

    /// <summary>
    /// Finds a character's storage at a station, creating it on first use.
    /// </summary>
    /// <remarks>
    /// Auto-created because requiring an explicit "rent storage" step before a player can trade
    /// would be friction with no gameplay behind it. Station rent, when implemented, will accrue
    /// on what a hangar actually holds rather than on its existence.
    /// </remarks>
    public async Task<Inventory> GetOrCreateStationHangarAsync(
        int characterId, int stationId, CancellationToken cancellationToken = default)
    {
        Inventory? existing = await _database.Inventories.FirstOrDefaultAsync(
            i => i.CharacterId == characterId
                && i.StationId == stationId
                && i.Kind == InventoryKind.StationHangar,
            cancellationToken);

        if (existing is not null)
        {
            return existing;
        }

        var hangar = new Inventory
        {
            CharacterId = characterId,
            StationId = stationId,
            Kind = InventoryKind.StationHangar,

            // Zero means unlimited. Station storage is bounded by rent rather than by volume,
            // which is what makes hoarding expensive instead of impossible.
            CapacityM3 = 0,
        };

        _database.Inventories.Add(hangar);
        await _database.SaveChangesAsync(cancellationToken);

        return hangar;
    }

    /// <summary>
    /// The inventory a character carries on their person, creating it if it does not exist.
    /// </summary>
    /// <remarks>
    /// <para>
    /// Keyed on the character alone: there is exactly one, it travels with them, and unlike a
    /// hangar it is not somewhere they have to be.
    /// </para>
    /// <para>
    /// Auto-created for the same reason a hangar is — a "find your pockets" step before a player
    /// can pick anything up would be friction with no gameplay behind it.
    /// </para>
    /// <para>
    /// Capacity is left unlimited for now and is the piece task 112 changes: carrying is meant to
    /// be bounded by weight, which is what turns planet-locked materials (ADR-0008) into flights
    /// rather than paperwork. Nothing enforces any capacity today, so a limit here would be the
    /// only one in the game and would read as arbitrary.
    /// </para>
    /// </remarks>
    public async Task<Inventory> GetOrCreateCarriedAsync(
        int characterId, CancellationToken cancellationToken = default)
    {
        Inventory? existing = await _database.Inventories.FirstOrDefaultAsync(
            i => i.CharacterId == characterId && i.Kind == InventoryKind.CharacterCarried,
            cancellationToken);

        if (existing is not null)
        {
            return existing;
        }

        var carried = new Inventory
        {
            CharacterId = characterId,
            Kind = InventoryKind.CharacterCarried,
            CapacityM3 = CarriedCapacityM3,
        };

        _database.Inventories.Add(carried);
        await _database.SaveChangesAsync(cancellationToken);

        return carried;
    }

    /// <summary>
    /// How many of an item will fit in an inventory, up to the number asked for.
    /// </summary>
    /// <remarks>
    /// <para>
    /// <strong>For sources that keep what they cannot hand over (ADR-0014).</strong> Ore that will
    /// not fit in a pack is still in the ground, so refusing the whole swing would be refusing
    /// something nobody had yet. That is the opposite of a purchase or a transfer, where the goods
    /// already exist and refusing part of the delivery would destroy the rest — and those keep
    /// <see cref="AddAsync"/>, which refuses outright.
    /// </para>
    /// <para>
    /// Returns zero for a container with no room, which callers must handle as "nothing happened"
    /// rather than as an error: a swing that yields nothing must not draw the node down or spend
    /// the cooldown.
    /// </para>
    /// </remarks>
    public async Task<int> RoomForAsync(
        long inventoryId, int itemDefId, int wanted, CancellationToken cancellationToken = default)
    {
        GuardQuantity(wanted);

        Inventory? inventory = await _database.Inventories
            .FirstOrDefaultAsync(i => i.Id == inventoryId, cancellationToken);

        if (inventory is null || inventory.CapacityM3 <= 0.0)
        {
            return wanted;
        }

        double each = await _database.ItemDefs
            .Where(d => d.Id == itemDefId)
            .Select(d => d.VolumeM3)
            .FirstOrDefaultAsync(cancellationToken);

        if (each <= 0.0)
        {
            return wanted;
        }

        double free = inventory.CapacityM3 + Tolerance
            - await UsedVolumeAsync(inventoryId, cancellationToken);

        if (free <= 0.0)
        {
            return 0;
        }

        return Math.Min(wanted, (int)Math.Floor(free / each));
    }

    /// <summary>
    /// How much of an inventory's capacity is already spoken for, in cubic metres.
    /// </summary>
    /// <remarks>
    /// Stacks and instances both. A hold with a hull in it is a hold with less room, and counting
    /// only the stackable half would let anybody carry an unlimited number of ships.
    /// </remarks>
    public async Task<double> UsedVolumeAsync(
        long inventoryId, CancellationToken cancellationToken = default)
    {
        double stacked = await _database.InventoryItems
            .Where(i => i.InventoryId == inventoryId)
            .SumAsync(i => i.Quantity * i.ItemDef!.VolumeM3, cancellationToken);

        double instances = await _database.ItemInstances
            .Where(i => i.InventoryId == inventoryId)
            .SumAsync(i => i.ItemDef!.VolumeM3, cancellationToken);

        return stacked + instances;
    }

    /// <summary>
    /// Refuses a delivery that will not fit.
    /// </summary>
    /// <remarks>
    /// <para>
    /// <strong>Here rather than on transfers, because every route in goes through AddAsync</strong>
    /// — gathering, crafting output, a market purchase, a quest reward, a transfer. The comment this
    /// replaced said so: a rule invented on transfer alone gives a hold you cannot fill by dragging
    /// and can fill by mining into it, which is worse than no rule because it looks like one.
    /// </para>
    /// <para>
    /// <strong>Zero capacity is unlimited, and station hangars keep it (ADR-0014).</strong> A hangar
    /// is rented storage with rent as its sink; capping it too would charge for a thing that also
    /// refuses goods.
    /// </para>
    /// <para>
    /// An over-full container is a legal state rather than an error. Limits arriving after goods
    /// exist, or a hull being moved into a hold that then has no room, both leave one — and the
    /// answer is that it takes nothing more and drains normally, never that it destroys what is
    /// already there.
    /// </para>
    /// </remarks>
    private async Task GuardRoomAsync(
        long inventoryId, int itemDefId, int quantity, CancellationToken cancellationToken)
    {
        Inventory? inventory = await _database.Inventories
            .FirstOrDefaultAsync(i => i.Id == inventoryId, cancellationToken);

        if (inventory is null || inventory.CapacityM3 <= 0.0)
        {
            return;
        }

        double each = await _database.ItemDefs
            .Where(d => d.Id == itemDefId)
            .Select(d => d.VolumeM3)
            .FirstOrDefaultAsync(cancellationToken);

        double wanted = each * quantity;

        // Nothing to check for something that takes up no room. Not an error: a few things are
        // deliberately weightless, and refusing them on a full container would be a rule about
        // nothing.
        if (wanted <= 0.0)
        {
            return;
        }

        double used = await UsedVolumeAsync(inventoryId, cancellationToken);

        if (used + wanted > inventory.CapacityM3 + Tolerance)
        {
            throw new InventoryFullException(inventoryId, inventory.CapacityM3, used, wanted);
        }
    }

    /// <summary>
    /// Slack on the capacity comparison, in cubic metres.
    /// </summary>
    /// <remarks>
    /// Volumes are doubles read from authored content and multiplied by quantities, so a hold sized
    /// to take exactly two hundred ore can come out a billionth of a cubic metre over and refuse the
    /// last one. A container that is full one short of its stated capacity is a bug report nobody
    /// can reproduce.
    /// </remarks>
    private const double Tolerance = 1e-6;

    private static void GuardQuantity(int quantity)
    {
        if (quantity <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(quantity), quantity, "Quantity must be positive.");
        }
    }

    private async Task GuardStackableAsync(int itemDefId, CancellationToken cancellationToken)
    {
        ItemCategory category = await _database.ItemDefs
            .Where(d => d.Id == itemDefId)
            .Select(d => d.Category)
            .SingleAsync(cancellationToken);

        if (!category.IsStackable())
        {
            throw new InvalidOperationException(
                $"Item {itemDefId} is {category}, which is tracked per instance rather than as a "
                + "stack. Use item instances instead.");
        }
    }
}
