using Microsoft.EntityFrameworkCore;
using SpaceMMO.Data.Entities;
using SpaceMMO.Data.Inventories;
using SpaceMMO.Data.Market;
using SpaceMMO.Domain.Economy;
using SpaceMMO.Domain.Industry;
using SpaceMMO.Domain.Items;

namespace SpaceMMO.Data.Industry;

/// <summary>Thrown when a repair is refused for a reason that is not the price.</summary>
/// <param name="reason">
/// For code: <c>not_docked</c> (not docked at the station asked about), <c>not_here</c> (the item is not worn, carried
/// or put away in this station's hangar), <c>not_damaged</c>, or <c>no_recipe</c> (nothing prices its repair).
/// </param>
public sealed class RepairRefusedException(string reason, string message) : InvalidOperationException(message)
{
    public string Reason { get; } = reason;
}

/// <summary>One material a repair charges, against what is here to pay it with.</summary>
/// <param name="Units">Whole units this repair takes: what the item's tab reaches.</param>
/// <param name="Held">How many are in this station's hangar and the character's pockets together, in every style.</param>
/// <param name="OwedAfterTenThousandths">What stays on the item's tab after, under a unit.</param>
public sealed record RepairMaterial(
    int ItemDefId, string ItemKey, string Name, int Units, int Held, int OwedAfterTenThousandths);

/// <summary>What repairing one damaged item would do and cost: a row of the REPAIRS section (task 196).</summary>
/// <param name="Kind">Where it is: worn (Equipped), in the pockets (CharacterCarried), or in this station's hangar.</param>
/// <param name="Priced">False when no recipe makes the item, so nothing prices its repair and it cannot be had.</param>
/// <param name="WornOut">Whether the repair destroys it instead, charging nothing.</param>
public sealed record RepairQuote(
    long ItemInstanceId,
    int ItemDefId,
    string ItemKey,
    string Name,
    ItemCategory Category,
    ItemStyle Style,
    InventoryKind Kind,
    int Condition,
    int MaxConditionHundredths,
    int Restored,
    int ConditionAfter,
    int MaxAfterHundredths,
    bool WornOut,
    bool Priced,
    IReadOnlyList<RepairMaterial> Materials,
    Credits Credits);

/// <summary>What a repair did.</summary>
/// <param name="Destroyed">Worn out: the item is gone and nothing was charged.</param>
/// <param name="Charged">Whole units taken of each material; a material owing under a unit is not listed.</param>
public sealed record RepairResult(
    long ItemInstanceId,
    bool Destroyed,
    int Condition,
    int MaxConditionHundredths,
    IReadOnlyList<(int ItemDefId, int Units)> Charged,
    Credits Credits);

/// <summary>
/// Repairs at a station's industry bay: instant, for materials and credits (ADR-0016 as amended, task 196).
/// </summary>
/// <remarks>
/// <para>
/// <strong>Only while docked at the station</strong>, and only what the character wears, carries, or has put away in
/// that station's hangar (Joe, 24 October). A ship standing outside is not in the bay.
/// </para>
/// <para>
/// <strong>Materials come from the hangar first and then the pockets</strong>, in any style, the item's own first.
/// Crafting draws on the same two, which is what a player standing in the station means by "what I have"; a repair
/// takes them where they are rather than moving them, since nothing is left behind to put anywhere.
/// </para>
/// <para>
/// <strong>What the materials cost is spent, not carried into the item.</strong> A repair is upkeep, and the item's
/// acquisition value stays what it cost to make or buy.
/// </para>
/// </remarks>
public sealed class RepairService(SpaceMmoDbContext database)
{
    private readonly SpaceMmoDbContext _database =
        database ?? throw new ArgumentNullException(nameof(database));

    private readonly InventoryService _inventories = new(database);

    /// <summary>Every damaged item the character could repair here, worn first, then carried, then in the hangar.</summary>
    /// <exception cref="RepairRefusedException"><c>not_docked</c> unless docked at the station.</exception>
    public async Task<IReadOnlyList<RepairQuote>> QuoteAsync(
        int characterId, int stationId, CancellationToken cancellationToken = default)
    {
        Character character = await _database.Characters
            .AsNoTracking()
            .SingleOrDefaultAsync(c => c.Id == characterId, cancellationToken)
            ?? throw new InvalidOperationException($"Character {characterId} does not exist.");

        GuardDocked(character, stationId);

        List<ItemInstance> items = await RepairableHere(characterId, stationId)
            .Include(i => i.ItemDef)
            .Include(i => i.Inventory)
            .AsNoTracking()
            .ToListAsync(cancellationToken);

        List<ItemInstance> damaged = [.. items
            .Where(i => Repairs.IsDamaged(i.Condition, i.MaxConditionHundredths))
            .OrderBy(i => KindOrder(i.Inventory!.Kind))
            .ThenBy(i => i.ItemDef!.Name, StringComparer.Ordinal)
            .ThenBy(i => i.Id)];

        if (damaged.Count == 0)
        {
            return [];
        }

        Dictionary<int, Recipe> recipes = await RecipesMakingAsync(
            damaged.Select(i => i.ItemDefId).Distinct(), cancellationToken);

        long[] ids = [.. damaged.Select(i => i.Id)];

        Dictionary<(long, int), int> tabs = await _database.RepairTabs
            .Where(t => ids.Contains(t.ItemInstanceId))
            .ToDictionaryAsync(t => (t.ItemInstanceId, t.ItemDefId), t => t.OwedTenThousandths, cancellationToken);

        Dictionary<int, int> held = await HeldAsync(
            characterId, stationId, recipes.Values.SelectMany(r => r.Inputs).Select(i => i.ItemDefId).Distinct(),
            cancellationToken);

        var quotes = new List<RepairQuote>(damaged.Count);

        foreach (ItemInstance item in damaged)
        {
            RepairPlan plan = Repairs.Plan(item.Condition, item.MaxConditionHundredths);
            bool priced = recipes.TryGetValue(item.ItemDefId, out Recipe? recipe);

            // A worn-out item's repair charges nothing, and one nothing prices cannot be had.
            List<RepairMaterial> materials = plan.WornOut || recipe is null
                ? []
                : [.. recipe.Inputs
                    .OrderBy(i => i.ItemDef!.Key, StringComparer.Ordinal)
                    .Select(input =>
                    {
                        (int units, int tabAfter) = Repairs.Charge(
                            tabs.GetValueOrDefault((item.Id, input.ItemDefId)),
                            Repairs.OwedPerPoint(input.Quantity, recipe.OutputQuantity),
                            plan.Restored);

                        return new RepairMaterial(
                            input.ItemDefId, input.ItemDef!.Key, input.ItemDef.Name, units,
                            held.GetValueOrDefault(input.ItemDefId), tabAfter);
                    })];

            quotes.Add(new RepairQuote(
                item.Id,
                item.ItemDefId,
                item.ItemDef!.Key,
                item.ItemDef.Name,
                item.ItemDef.Category,
                item.Style,
                item.Inventory!.Kind,
                item.Condition,
                item.MaxConditionHundredths,
                plan.Restored,
                plan.ConditionAfter,
                plan.MaxAfterHundredths,
                plan.WornOut,
                priced,
                materials,
                plan.WornOut || !priced ? Credits.Zero : Repairs.CreditsFor(plan.Restored)));
        }

        return quotes;
    }

    /// <summary>
    /// Repairs one item, at once: condition to the new maximum, materials and credits paid. Or, when worn out,
    /// destroys it and charges nothing.
    /// </summary>
    /// <exception cref="RepairRefusedException">If not docked here, the item is not here, or there is nothing to repair.</exception>
    /// <exception cref="InsufficientItemsException">If the hangar and pockets together hold too few of a material.</exception>
    /// <exception cref="InsufficientFundsException">If the credits cannot be paid.</exception>
    public async Task<RepairResult> RepairAsync(
        int characterId, int stationId, long itemInstanceId, CancellationToken cancellationToken = default)
    {
        await using var transaction = await _database.Database.BeginTransactionAsync(cancellationToken);

        // Character first, as industry and the market lock, so none of them can deadlock against another.
        List<Character> lockedCharacter = await _database.Characters
            .FromSqlInterpolated($"SELECT * FROM characters WHERE id = {characterId} FOR UPDATE")
            .ToListAsync(cancellationToken);

        Character character = lockedCharacter.Count == 1
            ? lockedCharacter[0]
            : throw new InvalidOperationException($"Character {characterId} does not exist.");

        GuardDocked(character, stationId);

        // The item too: a death's wear or a transfer could otherwise change it between the plan and the write.
        await _database.ItemInstances
            .FromSqlInterpolated($"SELECT * FROM item_instances WHERE id = {itemInstanceId} FOR UPDATE")
            .ToListAsync(cancellationToken);

        ItemInstance item = await RepairableHere(characterId, stationId)
            .Include(i => i.ItemDef)
            .SingleOrDefaultAsync(i => i.Id == itemInstanceId, cancellationToken)
            ?? throw new RepairRefusedException(
                "not_here", $"Item {itemInstanceId} is not worn, carried or in this station's hangar.");

        if (!Repairs.IsDamaged(item.Condition, item.MaxConditionHundredths))
        {
            throw new RepairRefusedException(
                "not_damaged", $"{item.ItemDef!.Name} is at {item.Condition}, its maximum: nothing to repair.");
        }

        RepairPlan plan = Repairs.Plan(item.Condition, item.MaxConditionHundredths);
        DateTimeOffset now = DateTimeOffset.UtcNow;

        if (plan.WornOut)
        {
            return await WearOutAsync(item, plan, now, transaction, cancellationToken);
        }

        Dictionary<int, Recipe> recipes = await RecipesMakingAsync([item.ItemDefId], cancellationToken);

        if (!recipes.TryGetValue(item.ItemDefId, out Recipe? recipe))
        {
            throw new RepairRefusedException(
                "no_recipe", $"Nothing makes {item.ItemDef!.Name}, so nothing prices its repair.");
        }

        Credits credits = Repairs.CreditsFor(plan.Restored);

        if (character.Balance < credits)
        {
            throw new InsufficientFundsException(characterId, credits, character.Balance);
        }

        Dictionary<int, RepairTab> tabs = await _database.RepairTabs
            .Where(t => t.ItemInstanceId == item.Id)
            .ToDictionaryAsync(t => t.ItemDefId, cancellationToken);

        long? hangarId = await InventoryIdAsync(characterId, InventoryKind.StationHangar, stationId, cancellationToken);
        long? carriedId = await InventoryIdAsync(characterId, InventoryKind.CharacterCarried, null, cancellationToken);

        var charged = new List<(int ItemDefId, int Units)>();

        foreach (RecipeInput input in recipe.Inputs.OrderBy(i => i.ItemDefId))
        {
            if (!tabs.TryGetValue(input.ItemDefId, out RepairTab? tab))
            {
                tab = new RepairTab { ItemInstanceId = item.Id, ItemDefId = input.ItemDefId };
                _database.RepairTabs.Add(tab);
            }

            (int units, int tabAfter) = Repairs.Charge(
                tab.OwedTenThousandths, Repairs.OwedPerPoint(input.Quantity, recipe.OutputQuantity), plan.Restored);

            tab.OwedTenThousandths = tabAfter;

            if (units > 0)
            {
                await TakeAsync(hangarId, carriedId, input.ItemDefId, units, item.Style, cancellationToken);
                charged.Add((input.ItemDefId, units));
            }
        }

        if (!credits.IsZero)
        {
            await _database.Database.ExecuteSqlInterpolatedAsync(
                $"UPDATE characters SET balance = balance - {credits.MinorUnits} WHERE id = {characterId}",
                cancellationToken);

            _database.LedgerEntries.Add(new LedgerEntry
            {
                CharacterId = characterId,
                DeltaCredits = -credits,
                Reason = LedgerReason.RepairCost,
                ReferenceId = item.Id,
                CreatedAt = now,
            });
        }

        item.Condition = plan.ConditionAfter;
        item.MaxConditionHundredths = plan.MaxAfterHundredths;

        await _database.SaveChangesAsync(cancellationToken);
        await transaction.CommitAsync(cancellationToken);

        return new RepairResult(item.Id, false, item.Condition, item.MaxConditionHundredths, charged, credits);
    }

    /// <summary>
    /// The repair that uses an item up: it is destroyed, its tab with it, and nothing is charged, since nothing comes
    /// back (Joe's "gone, with a warning"; the warning is the quote's).
    /// </summary>
    private async Task<RepairResult> WearOutAsync(
        ItemInstance item,
        RepairPlan plan,
        DateTimeOffset now,
        Microsoft.EntityFrameworkCore.Storage.IDbContextTransaction transaction,
        CancellationToken cancellationToken)
    {
        // Out of its slot and its inventory as well as marked, as the entity says a destroyed one is: a destroyed
        // item still holding its slot would stop anything else being equipped there.
        item.DestroyedAt = now;
        item.InventoryId = null;
        item.EquippedSlot = null;
        item.MaxConditionHundredths = plan.MaxAfterHundredths;

        await _database.RepairTabs.Where(t => t.ItemInstanceId == item.Id).ExecuteDeleteAsync(cancellationToken);

        await _database.SaveChangesAsync(cancellationToken);
        await transaction.CommitAsync(cancellationToken);

        return new RepairResult(item.Id, true, item.Condition, item.MaxConditionHundredths, [], Credits.Zero);
    }

    /// <summary>
    /// Takes whole units of a material from the hangar first and then the pockets, in any style, the item's own first.
    /// </summary>
    /// <exception cref="InsufficientItemsException">Naming both together, before anything is taken.</exception>
    private async Task TakeAsync(
        long? hangarId, long? carriedId, int itemDefId, int units, ItemStyle preferred,
        CancellationToken cancellationToken)
    {
        int inHangar = hangarId is long h ? await _inventories.QuantityOfAsync(h, itemDefId, cancellationToken) : 0;
        int carried = carriedId is long c ? await _inventories.QuantityOfAsync(c, itemDefId, cancellationToken) : 0;

        if (inHangar + carried < units)
        {
            throw new InsufficientItemsException(itemDefId, units, inHangar + carried);
        }

        int fromHangar = Math.Min(units, inHangar);

        if (fromHangar > 0)
        {
            await _inventories.RemoveAnyStyleAsync(hangarId!.Value, itemDefId, fromHangar, preferred, cancellationToken);
        }

        if (units > fromHangar)
        {
            await _inventories.RemoveAnyStyleAsync(
                carriedId!.Value, itemDefId, units - fromHangar, preferred, cancellationToken);
        }
    }

    private static void GuardDocked(Character character, int stationId)
    {
        if (character.DockedStationId != stationId)
        {
            throw new RepairRefusedException(
                "not_docked", $"Character {character.Id} is not docked at station {stationId}.");
        }
    }

    /// <summary>
    /// What a character may repair at a station: worn, carried, or put away in its hangar, and not destroyed.
    /// </summary>
    private IQueryable<ItemInstance> RepairableHere(int characterId, int stationId) =>
        _database.ItemInstances.Where(i =>
            i.DestroyedAt == null
            && i.Inventory!.CharacterId == characterId
            && (i.Inventory.Kind == InventoryKind.Equipped
                || i.Inventory.Kind == InventoryKind.CharacterCarried
                || (i.Inventory.Kind == InventoryKind.StationHangar
                    && i.Inventory.StationId == stationId
                    && i.DeployedSystemX == null)));

    /// <summary>
    /// The recipe that prices each item's repair: the one making it, a current one before a retired one, then the
    /// first made. Retired still counts: the item is still made of what it was made of.
    /// </summary>
    private async Task<Dictionary<int, Recipe>> RecipesMakingAsync(
        IEnumerable<int> itemDefIds, CancellationToken cancellationToken)
    {
        int[] ids = [.. itemDefIds];

        List<Recipe> recipes = await _database.Recipes
            .Where(r => ids.Contains(r.OutputItemDefId))
            .Include(r => r.Inputs)
            .ThenInclude(i => i.ItemDef)
            .AsNoTracking()
            .ToListAsync(cancellationToken);

        return recipes
            .GroupBy(r => r.OutputItemDefId)
            .ToDictionary(g => g.Key, g => g.OrderBy(r => r.RetiredAt is null ? 0 : 1).ThenBy(r => r.Id).First());
    }

    /// <summary>How many of each material the hangar here and the pockets hold together, in every style.</summary>
    private async Task<Dictionary<int, int>> HeldAsync(
        int characterId, int stationId, IEnumerable<int> itemDefIds, CancellationToken cancellationToken)
    {
        int[] ids = [.. itemDefIds];

        return await _database.InventoryItems
            .Where(s => ids.Contains(s.ItemDefId)
                && s.Inventory!.CharacterId == characterId
                && (s.Inventory.Kind == InventoryKind.CharacterCarried
                    || (s.Inventory.Kind == InventoryKind.StationHangar && s.Inventory.StationId == stationId)))
            .GroupBy(s => s.ItemDefId)
            .Select(g => new { ItemDefId = g.Key, Held = g.Sum(s => s.Quantity) })
            .ToDictionaryAsync(h => h.ItemDefId, h => h.Held, cancellationToken);
    }

    private async Task<long?> InventoryIdAsync(
        int characterId, InventoryKind kind, int? stationId, CancellationToken cancellationToken) =>
        await _database.Inventories
            .Where(i => i.CharacterId == characterId && i.Kind == kind && i.StationId == stationId)
            .Select(i => (long?)i.Id)
            .FirstOrDefaultAsync(cancellationToken);

    private static int KindOrder(InventoryKind kind) => kind switch
    {
        InventoryKind.Equipped => 0,
        InventoryKind.CharacterCarried => 1,
        _ => 2,
    };
}
