using Microsoft.EntityFrameworkCore;
using SpaceMMO.Data.Entities;
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
    /// <exception cref="ArgumentOutOfRangeException">If quantity is not positive.</exception>
    /// <exception cref="InvalidOperationException">If the item is not stackable.</exception>
    public async Task AddAsync(
        long inventoryId, int itemDefId, int quantity, CancellationToken cancellationToken = default)
    {
        GuardQuantity(quantity);
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
            });

            return;
        }

        stack.Quantity += quantity;
    }

    /// <summary>
    /// Removes a quantity, deleting the stack row if it reaches zero.
    /// </summary>
    /// <remarks>
    /// The row is deleted rather than left at zero because a zero-quantity stack is forbidden by
    /// a check constraint — "does the player have any?" should never also need to ask "but is it
    /// more than none?".
    /// </remarks>
    /// <exception cref="ArgumentOutOfRangeException">If quantity is not positive.</exception>
    /// <exception cref="InsufficientItemsException">If the inventory holds too few.</exception>
    public async Task RemoveAsync(
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
            _database.InventoryItems.Remove(stack!);
            return;
        }

        stack!.Quantity -= quantity;
    }

    /// <summary>How many of an item an inventory holds. Zero if it holds none.</summary>
    public async Task<int> QuantityOfAsync(
        long inventoryId, int itemDefId, CancellationToken cancellationToken = default) =>
        await _database.InventoryItems
            .Where(i => i.InventoryId == inventoryId && i.ItemDefId == itemDefId)
            .Select(i => i.Quantity)
            .FirstOrDefaultAsync(cancellationToken);

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
