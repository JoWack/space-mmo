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
