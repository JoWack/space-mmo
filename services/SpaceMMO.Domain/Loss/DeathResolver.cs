using SpaceMMO.Domain.Items;
using SpaceMMO.Domain.Random;

namespace SpaceMMO.Domain.Loss;

/// <summary>
/// One entry in a dead character's possessions, as handed to the resolver.
/// </summary>
/// <param name="ItemKey">The item definition key, e.g. <c>ferrite_ore</c>.</param>
/// <param name="Category">Drives both storage model and destruction odds.</param>
/// <param name="Quantity">Stack size; always 1 for non-stackable categories.</param>
/// <param name="InstanceId">
/// The item instance's database identifier, for non-stackable categories. It discriminates
/// the random stream, so two identical items resolve independently rather than sharing a
/// roll. Ignored for stackable categories, which resolve without randomness.
/// </param>
public readonly record struct DeathItem(
    string ItemKey,
    ItemCategory Category,
    int Quantity,
    long InstanceId = 0L);

/// <summary>
/// What became of one entry after a death. Quantities always sum to the input quantity.
/// </summary>
/// <param name="InstanceId">
/// Carried through from the input so the caller knows <em>which</em> tracked instance to
/// mark damaged or destroyed. Meaningless for stackable categories.
/// </param>
public readonly record struct ResolvedItem(
    string ItemKey,
    ItemCategory Category,
    long InstanceId,
    int SurvivedQuantity,
    int DamagedQuantity,
    int DestroyedQuantity)
{
    /// <summary>Total quantity accounted for. Must equal the input quantity.</summary>
    public int TotalQuantity => SurvivedQuantity + DamagedQuantity + DestroyedQuantity;

    /// <summary>Quantity that remains lootable, whether intact or damaged.</summary>
    public int LootableQuantity => SurvivedQuantity + DamagedQuantity;
}

/// <summary>
/// Decides what survives a death, per ADR-0006.
/// </summary>
/// <remarks>
/// <para>
/// A pure function of an explicit seed. The server draws the seed, records it on the
/// death record, and applies the result — so the resolver is unit-testable, EconSim can
/// reproduce destruction runs exactly, and a player dispute over a lost item is
/// replayable from the log rather than a matter of opinion.
/// </para>
/// <para>
/// <strong>Resolution is order-independent.</strong> Each entry derives its own random
/// stream from <c>(seed, item key, instance id)</c>, so reordering the input cannot
/// change any outcome. That matters because the inventory ordering the server happens to
/// produce is not something worth depending on.
/// </para>
/// </remarks>
public static class DeathResolver
{
    /// <summary>
    /// Resolves every entry in a loadout.
    /// </summary>
    /// <param name="loadout">The dead character's possessions. May be empty.</param>
    /// <param name="cause">How they died.</param>
    /// <param name="seed">The recorded death seed. Same inputs always give same outputs.</param>
    /// <param name="table">Rule table; defaults to <see cref="DeathRuleTable.Default"/>.</param>
    /// <exception cref="ArgumentException">
    /// If any entry has a non-positive quantity, or a non-stackable entry has a quantity
    /// above 1.
    /// </exception>
    public static IReadOnlyList<ResolvedItem> Resolve(
        IReadOnlyList<DeathItem> loadout,
        DeathCause cause,
        ulong seed,
        DeathRuleTable? table = null)
    {
        ArgumentNullException.ThrowIfNull(loadout);

        table ??= DeathRuleTable.Default;

        GuardDistinctInstances(loadout);

        var results = new List<ResolvedItem>(loadout.Count);

        foreach (DeathItem item in loadout)
        {
            results.Add(ResolveOne(item, cause, seed, table));
        }

        return results;
    }

    /// <summary>
    /// Rejects duplicate <c>(item key, instance id)</c> pairs among tracked instances.
    /// </summary>
    /// <remarks>
    /// Two instances sharing a discriminator would share a random stream and always
    /// resolve identically — losing both pieces of armor or neither, never one. That is a
    /// subtle statistical bug that would be very hard to notice in play, so it fails loudly
    /// here instead. The usual cause is a caller passing the default instance id for
    /// everything.
    /// </remarks>
    private static void GuardDistinctInstances(IReadOnlyList<DeathItem> loadout)
    {
        var seen = new HashSet<(string ItemKey, long InstanceId)>();

        foreach (DeathItem item in loadout)
        {
            if (item.Category.IsStackable())
            {
                continue;
            }

            if (!seen.Add((item.ItemKey, item.InstanceId)))
            {
                throw new ArgumentException(
                    $"Loadout contains more than one '{item.ItemKey}' with instance id " +
                    $"{item.InstanceId}. Tracked instances need distinct ids, or they share a " +
                    "random stream and always resolve identically.");
            }
        }
    }

    private static ResolvedItem ResolveOne(
        DeathItem item, DeathCause cause, ulong seed, DeathRuleTable table)
    {
        if (item.Quantity <= 0)
        {
            throw new ArgumentException(
                $"Item '{item.ItemKey}' has quantity {item.Quantity}; must be positive.");
        }

        bool stackable = item.Category.IsStackable();

        if (!stackable && item.Quantity != 1)
        {
            throw new ArgumentException(
                $"Item '{item.ItemKey}' is {item.Category}, which is not stackable, but has " +
                $"quantity {item.Quantity}. Non-stackable items are tracked per instance and " +
                "must be passed as separate entries.");
        }

        OutcomeWeights weights = table.For(cause, item.Category);

        return stackable
            ? ResolveStack(item, weights)
            : ResolveInstance(item, weights, seed);
    }

    /// <summary>
    /// Resolves a stackable entry proportionally, with no randomness.
    /// </summary>
    /// <remarks>
    /// <para>
    /// Rolling per unit would be more faithful but wildly swingy — a hold of 10,000 ore
    /// would sometimes survive whole and sometimes vanish entirely, which is both bad
    /// play experience and terrible for EconSim, whose material-flow measurements would
    /// be dominated by variance rather than by the rates being tested.
    /// </para>
    /// <para>
    /// The survivor count is floored and the remainder goes to destroyed, so rounding
    /// always favours the sink over the player. That is the same "round against the actor"
    /// rule the money code follows (ADR-0005), and it keeps rounding from being a slow
    /// material faucet.
    /// </para>
    /// <para>
    /// Stackable categories cannot be damaged — the storage model has nowhere to record
    /// condition — and <see cref="DeathRuleTable"/> enforces a zero damaged weight for
    /// them at construction.
    /// </para>
    /// </remarks>
    private static ResolvedItem ResolveStack(DeathItem item, OutcomeWeights weights)
    {
        int survived = item.Quantity * weights.SurvivedPercent / 100;
        int destroyed = item.Quantity - survived;

        return new ResolvedItem(
            item.ItemKey,
            item.Category,
            item.InstanceId,
            SurvivedQuantity: survived,
            DamagedQuantity: 0,
            DestroyedQuantity: destroyed);
    }

    /// <summary>
    /// Resolves a single tracked instance with one roll. Variance is the point here —
    /// whether your armor survives should be a real gamble.
    /// </summary>
    private static ResolvedItem ResolveInstance(DeathItem item, OutcomeWeights weights, ulong seed)
    {
        ulong itemSeed = StableHash.Combine(
            StableHash.Combine(seed, StableHash.Fnv1a64(item.ItemKey)),
            (ulong)item.InstanceId);

        var rng = new SplitMix64(itemSeed);

        LootOutcome outcome = weights.Resolve(rng.NextBelow(100));

        return new ResolvedItem(
            item.ItemKey,
            item.Category,
            item.InstanceId,
            SurvivedQuantity: outcome == LootOutcome.Survived ? 1 : 0,
            DamagedQuantity: outcome == LootOutcome.Damaged ? 1 : 0,
            DestroyedQuantity: outcome == LootOutcome.Destroyed ? 1 : 0);
    }
}
