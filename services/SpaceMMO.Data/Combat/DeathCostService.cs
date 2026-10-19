using Microsoft.EntityFrameworkCore;
using SpaceMMO.Data.Entities;
using SpaceMMO.Data.Inventories;
using SpaceMMO.Domain.Economy;
using SpaceMMO.Domain.Items;
using SpaceMMO.Domain.Loss;
using SpaceMMO.Domain.Universe;

namespace SpaceMMO.Data.Combat;

/// <summary>One material a death took: how many dropped where the body fell and how many were destroyed.</summary>
/// <param name="Style">The style the goods show: a raw material's own world, as the inventory lists it.</param>
public sealed record DeathLossLine(int ItemDefId, string ItemKey, string Name, ItemStyle Style, int Dropped, int Destroyed);

/// <summary>One worn item a death wore down.</summary>
public sealed record WornLine(long ItemInstanceId, string ItemKey, string Name, int Before, int After);

/// <summary>One material in a dropped container.</summary>
/// <param name="Style">The stack's own style, which is what taking it names.</param>
/// <param name="ShownStyle">The style the goods show: a raw material's own world, as the inventory lists it.</param>
public sealed record LootLine(
    int ItemDefId, string ItemKey, string Name, ItemStyle Style, ItemStyle ShownStyle, int Quantity, double VolumeM3);

/// <summary>A dropped container as the game server places it and the loot window lists it.</summary>
public sealed record LootContainerView(
    long Id,
    int DroppedByCharacterId,
    string DroppedBy,
    SystemPosition Position,
    DateTimeOffset ExpiresAt,
    IReadOnlyList<LootLine> Items);

/// <summary>What a death cost.</summary>
/// <param name="Applied">False when nothing was owed: no death waiting, or its costs already taken.</param>
/// <param name="AtStation">Died docked, so nothing was lost (ADR-0016).</param>
/// <param name="Container">Where the dropped half lies, or null when nothing dropped.</param>
public sealed record DeathCostResult(
    bool Applied,
    bool AtStation,
    IReadOnlyList<DeathLossLine> Lost,
    IReadOnlyList<WornLine> Worn,
    LootContainerView? Container)
{
    public static DeathCostResult NothingOwed { get; } = new(false, false, [], [], null);
}

/// <summary>What one go at a container took.</summary>
/// <param name="Refusal">
/// Null when something was taken; otherwise <c>gone</c> (expired, emptied or never there), <c>dead</c>,
/// <c>no_room</c> (nothing asked for fits in the pockets) or <c>not_there</c> (nothing asked for is in it).
/// </param>
/// <param name="Left">The container as it now stands, or null once it is empty.</param>
public sealed record LootResult(string? Refusal, IReadOnlyList<LootLine> Taken, LootContainerView? Left)
{
    public static LootResult Refused(string reason, LootContainerView? left = null) => new(reason, [], left);
}

/// <summary>One stack a looter asks for: the item and the stack's style.</summary>
public readonly record struct LootWanted(int ItemDefId, ItemStyle Style);

/// <summary>
/// What dying costs (ADR-0016, task 103), and taking what was dropped.
/// </summary>
/// <remarks>
/// <para>
/// <strong>Off the hit's lock.</strong> A kill only records the death (<see cref="RecordDeath"/>), inside the save
/// the hit makes anyway, so a killing hit holds the pair's lock no longer than any other (task 198). The game server
/// then says where the body fell (<see cref="ApplyAsync"/>), and the costs are taken in a transaction of their own:
/// the carried materials split into a container and the destroyed half, and what is worn worn down.
/// </para>
/// <para>
/// <strong>Owed until taken, and taken once.</strong> A death whose costs the game server never asked for -- it
/// crashed, or the call failed -- is costed when the character respawns, at their last recorded whereabouts, so no
/// death goes free. The dead character's row is locked while it is done, the lock gathering, industry and the market
/// take, so nothing reaches their pockets halfway through.
/// </para>
/// </remarks>
public sealed class DeathCostService(SpaceMmoDbContext database)
{
    private readonly SpaceMmoDbContext _database =
        database ?? throw new ArgumentNullException(nameof(database));

    /// <summary>
    /// Records a death, owing its costs: added to what <paramref name="database"/> will save next, and no round trip.
    /// </summary>
    /// <remarks>Called by the killing hit, under its lock; whether they were docked decides whether it costs anything.</remarks>
    public static DeathRecord RecordDeath(
        SpaceMmoDbContext database, Character dead, int? killerCharacterId, int starSystemId, DateTimeOffset now)
    {
        ArgumentNullException.ThrowIfNull(database);
        ArgumentNullException.ThrowIfNull(dead);

        var death = new DeathRecord
        {
            CharacterId = dead.Id,
            Cause = DeathCosts.OnFoot(docked: dead.DockedStationId is not null),
            StarSystemId = starSystemId,
            KillerCharacterId = killerCharacterId,
            OccurredAt = now,
        };

        database.DeathRecords.Add(death);

        return death;
    }

    /// <summary>
    /// Takes what a character's latest death costs, if it has not been taken: half of every material they carried
    /// dropped in a container at <paramref name="where"/> and half destroyed, and ten condition off what they wear.
    /// </summary>
    /// <param name="where">
    /// Where the container goes, in system kilometres: beside the body, as the game server placed it. Null to use
    /// the character's last recorded whereabouts, for a death the game server never costed.
    /// </param>
    public async Task<DeathCostResult> ApplyAsync(
        int characterId, SystemPosition? where, TimeProvider clock, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(clock);

        await using var transaction = await _database.Database.BeginTransactionAsync(cancellationToken);

        Character? character = (await _database.Characters
            .FromSqlInterpolated($"SELECT * FROM characters WHERE id = {characterId} FOR UPDATE")
            .ToListAsync(cancellationToken)).SingleOrDefault();

        if (character is null)
        {
            return DeathCostResult.NothingOwed;
        }

        // Oldest first. Only one can be owed -- the dead cannot die again, and a respawn settles what is owed -- but if
        // two ever were, each is its own death and costs its own.
        DeathRecord? death = await _database.DeathRecords
            .Where(d => d.CharacterId == characterId && d.CostsAppliedAt == null)
            .OrderBy(d => d.OccurredAt)
            .ThenBy(d => d.Id)
            .FirstOrDefaultAsync(cancellationToken);

        if (death is null)
        {
            return DeathCostResult.NothingOwed;
        }

        DateTimeOffset now = clock.GetUtcNow();

        death.CostsAppliedAt = now;

        if (!DeathCosts.CostsAnything(death.Cause))
        {
            await _database.SaveChangesAsync(cancellationToken);
            await transaction.CommitAsync(cancellationToken);

            return new DeathCostResult(true, true, [], [], null);
        }

        List<InventoryItem> carried = await _database.InventoryItems
            .Include(i => i.ItemDef)
            .Where(i => i.Inventory!.CharacterId == characterId && i.Inventory.Kind == InventoryKind.CharacterCarried)
            .ToListAsync(cancellationToken);

        // In slot order, which is the enum's: the column holds the slot's name, and would sort the alphabet.
        List<ItemInstance> worn = [.. (await _database.ItemInstances
            .Include(i => i.ItemDef)
            .Where(i => i.Inventory!.CharacterId == characterId
                && i.Inventory.Kind == InventoryKind.Equipped
                && i.DestroyedAt == null)
            .ToListAsync(cancellationToken))
            .OrderBy(i => i.EquippedSlot)];

        // By item and style: one stack each in one set of pockets, and summed should there ever be two.
        var goodsCarried = carried
            .GroupBy(s => (s.ItemDefId, s.Style))
            .Select(g => new
            {
                Goods = new CarriedGoods(
                    g.Key.ItemDefId, g.Key.Style, g.First().ItemDef!.Category, g.Sum(s => s.Quantity),
                    g.Aggregate(Credits.Zero, (sum, s) => sum + s.CostBasis)),
                Def = g.First().ItemDef!,
                Stacks = g.ToList(),
            })
            .ToList();

        DeathCostPlan plan = DeathCosts.Plan(
            death.Cause,
            [.. goodsCarried.Select(g => g.Goods)],
            [.. worn.Select(w => new WornItem(w.Id, w.Condition))]);

        where ??= character.LastSystemX is double x && character.LastSystemY is double y
            && character.LastSystemZ is double z
                ? new SystemPosition(x, y, z)
                : null;

        // Nowhere to put it, which only a character the world never placed could be: what would have dropped is
        // destroyed with the rest, and the loss lines say so.
        LootContainer? container = plan.DropsAnything && where is { } at
            ? new LootContainer
            {
                DeathRecordId = death.Id,
                DroppedByCharacterId = characterId,
                StarSystemId = death.StarSystemId,
                SystemX = at.XKilometres,
                SystemY = at.YKilometres,
                SystemZ = at.ZKilometres,
                CreatedAt = now,
                ExpiresAt = now + DeathCosts.LootableFor,
            }
            : null;

        var lost = new List<DeathLossLine>();

        foreach (LostGoods goods in plan.Lost)
        {
            var held = goodsCarried.Single(g => g.Goods.ItemDefId == goods.ItemDefId && g.Goods.Style == goods.Style);

            int dropped = container is null ? 0 : goods.Dropped;
            int destroyed = goods.Dropped + goods.Destroyed - dropped;

            // The whole stack leaves the pockets: what drops goes to the container, the rest is gone.
            _database.InventoryItems.RemoveRange(held.Stacks);

            if (dropped > 0)
            {
                container!.Items.Add(new LootContainerItem
                {
                    ItemDefId = goods.ItemDefId,
                    Style = goods.Style,
                    Quantity = dropped,
                    CostBasis = goods.DroppedCostBasis,
                });
            }

            _database.DeathLosses.Add(new DeathLoss
            {
                DeathRecordId = death.Id,
                ItemDefId = goods.ItemDefId,
                Style = goods.Style,
                Dropped = dropped,
                Destroyed = destroyed,
            });

            lost.Add(new DeathLossLine(
                goods.ItemDefId, held.Def.Key, held.Def.Name, ShownStyle(held.Def, goods.Style), dropped, destroyed));
        }

        var wornLines = new List<WornLine>();

        foreach (WornDown down in plan.Worn)
        {
            ItemInstance item = worn.Single(w => w.Id == down.ItemInstanceId);

            item.Condition = down.After;

            wornLines.Add(new WornLine(item.Id, item.ItemDef!.Key, item.ItemDef.Name, down.Before, down.After));
        }

        if (container is not null)
        {
            _database.LootContainers.Add(container);
        }

        await _database.SaveChangesAsync(cancellationToken);
        await transaction.CommitAsync(cancellationToken);

        return new DeathCostResult(
            true,
            false,
            lost,
            wornLines,
            container is null ? null : await ViewAsync(container.Id, cancellationToken));
    }

    /// <summary>Every container that can still be looted: not past its time, and not emptied.</summary>
    /// <remarks>What the game server puts in the world when it starts, and again after a respawn that costed a death.</remarks>
    public async Task<IReadOnlyList<LootContainerView>> LiveContainersAsync(
        TimeProvider clock, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(clock);

        DateTimeOffset now = clock.GetUtcNow();

        return await ViewsAsync(
            _database.LootContainers.Where(c => c.ExpiresAt > now && c.Items.Any()).OrderBy(c => c.Id), cancellationToken);
    }

    /// <summary>
    /// Takes from a container into the looter's pockets: the stacks asked for, or everything when none are named, as
    /// much of each as fits.
    /// </summary>
    /// <remarks>
    /// <para>
    /// <strong>The game server decides who is in reach</strong>, as it does for a deposit: the looter names a container
    /// through it, never directly, so nobody takes from a crate on another planet.
    /// </para>
    /// <para>
    /// <strong>As much as fits, not all or nothing.</strong> The goods are lying on the ground; what does not fit stays
    /// there for the next go or the next looter, which is ADR-0014's rule for a source that keeps what it cannot hand
    /// over, as gathering is.
    /// </para>
    /// <para>
    /// <strong>The cost basis comes back only to its owner.</strong> Somebody recovering their own goods paid for them
    /// once; anyone else paid nothing, and takes them at nothing, as gathered material is.
    /// </para>
    /// </remarks>
    /// <param name="wanted">The stacks to take; null or empty for all of them.</param>
    public async Task<LootResult> LootAsync(
        long containerId,
        int looterCharacterId,
        IReadOnlyList<LootWanted>? wanted,
        TimeProvider clock,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(clock);

        await using var transaction = await _database.Database.BeginTransactionAsync(cancellationToken);

        // The looter first, as everything that fills pockets locks them; then the container, so two looters take
        // in turn and never the same ore twice.
        Character? looter = (await _database.Characters
            .FromSqlInterpolated($"SELECT * FROM characters WHERE id = {looterCharacterId} FOR UPDATE")
            .ToListAsync(cancellationToken)).SingleOrDefault();

        LootContainer? container = (await _database.LootContainers
            .FromSqlInterpolated($"SELECT * FROM loot_containers WHERE id = {containerId} FOR UPDATE")
            .ToListAsync(cancellationToken)).SingleOrDefault();

        DateTimeOffset now = clock.GetUtcNow();

        if (looter is null || container is null || container.ExpiresAt <= now)
        {
            return LootResult.Refused("gone");
        }

        // Stored health of nothing is the dead: health is kept as of the last fight, and nothing brings it back
        // from zero but a respawn, which clears it.
        if (looter.Health is 0)
        {
            return LootResult.Refused("dead", await ViewAsync(containerId, cancellationToken));
        }

        List<LootContainerItem> items = await _database.LootContainerItems
            .Include(i => i.ItemDef)
            .Where(i => i.LootContainerId == containerId)
            .OrderBy(i => i.ItemDefId)
            .ThenBy(i => i.Style)
            .ToListAsync(cancellationToken);

        if (items.Count == 0)
        {
            return LootResult.Refused("gone");
        }

        List<LootContainerItem> asked = wanted is null || wanted.Count == 0
            ? items
            : [.. items.Where(i => wanted.Any(w => w.ItemDefId == i.ItemDefId && w.Style == i.Style))];

        if (asked.Count == 0)
        {
            return LootResult.Refused("not_there", await ViewAsync(containerId, cancellationToken));
        }

        var inventories = new InventoryService(_database);
        Inventory pockets = await inventories.GetOrCreateCarriedAsync(looterCharacterId, cancellationToken);

        // Counted here once rather than asked of the database per stack: what this go adds is not saved until the
        // end, so the database would answer as if nothing had been taken yet.
        double free = pockets.CapacityM3 <= 0.0
            ? double.PositiveInfinity
            : pockets.CapacityM3 - await inventories.UsedVolumeAsync(pockets.Id, cancellationToken);

        bool owner = looterCharacterId == container.DroppedByCharacterId;
        var taken = new List<LootLine>();

        foreach (LootContainerItem item in asked)
        {
            double each = item.ItemDef!.VolumeM3;

            int fits = each <= 0.0
                ? item.Quantity
                : (int)Math.Min(item.Quantity, Math.Floor((free + RoomTolerance) / each));

            if (fits <= 0)
            {
                continue;
            }

            Credits basis = DeathCosts.ShareOf(item.CostBasis, fits, item.Quantity);

            await inventories.AddAsync(
                pockets.Id, item.ItemDefId, fits, owner ? basis : Credits.Zero, item.Style, cancellationToken);

            item.Quantity -= fits;
            item.CostBasis -= basis;
            free -= fits * each;

            if (item.Quantity == 0)
            {
                _database.LootContainerItems.Remove(item);
            }

            taken.Add(new LootLine(
                item.ItemDefId, item.ItemDef.Key, item.ItemDef.Name, item.Style, ShownStyle(item.ItemDef, item.Style), fits,
                each));
        }

        if (taken.Count == 0)
        {
            return LootResult.Refused("no_room", await ViewAsync(containerId, cancellationToken));
        }

        await _database.SaveChangesAsync(cancellationToken);
        await transaction.CommitAsync(cancellationToken);

        LootContainerView? left = await ViewAsync(containerId, cancellationToken);

        return new LootResult(null, taken, left is { Items.Count: > 0 } ? left : null);
    }

    /// <summary>A container as it stands, or null if there is no such container.</summary>
    private async Task<LootContainerView?> ViewAsync(long containerId, CancellationToken cancellationToken) =>
        (await ViewsAsync(_database.LootContainers.Where(c => c.Id == containerId), cancellationToken)).SingleOrDefault();

    /// <summary>Containers as they stand, with their contents and whose they were, in one round trip.</summary>
    private static async Task<List<LootContainerView>> ViewsAsync(
        IQueryable<LootContainer> containers, CancellationToken cancellationToken)
    {
        var found = await containers
            .Select(c => new
            {
                Container = c,
                DroppedBy = c.DroppedByCharacter!.Name,
                Items = c.Items
                    .OrderBy(i => i.ItemDef!.Name)
                    .ThenBy(i => i.Style)
                    .Select(i => new { Item = i, i.ItemDef })
                    .ToList(),
            })
            .AsNoTracking()
            .AsSingleQuery()
            .ToListAsync(cancellationToken);

        return [.. found.Select(f => new LootContainerView(
            f.Container.Id,
            f.Container.DroppedByCharacterId,
            f.DroppedBy,
            new SystemPosition(f.Container.SystemX, f.Container.SystemY, f.Container.SystemZ),
            f.Container.ExpiresAt,
            [.. f.Items.Select(i => new LootLine(
                i.Item.ItemDefId, i.ItemDef!.Key, i.ItemDef.Name, i.Item.Style, ShownStyle(i.ItemDef, i.Item.Style),
                i.Item.Quantity, i.ItemDef.VolumeM3))]))];
    }

    /// <summary>A raw material shows its own world; anything else its stack's (task 187), as the inventory lists it.</summary>
    private static ItemStyle ShownStyle(ItemDef def, ItemStyle style) =>
        def.Category == ItemCategory.Raw ? def.Style : style;

    /// <summary>The capacity check's slack, as <c>InventoryService</c> allows: a pack sized to the unit is not one short.</summary>
    private const double RoomTolerance = 1e-6;
}
