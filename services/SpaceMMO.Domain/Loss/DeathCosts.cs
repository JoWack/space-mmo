using SpaceMMO.Domain.Economy;
using SpaceMMO.Domain.Items;

namespace SpaceMMO.Domain.Loss;

/// <summary>One stack a dead character was carrying, as the rule needs it.</summary>
/// <param name="CostBasis">What the whole stack cost its owner: the dropped half takes its share with it.</param>
public readonly record struct CarriedGoods(
    int ItemDefId, ItemStyle Style, ItemCategory Category, int Quantity, Credits CostBasis);

/// <summary>One thing a dead character was wearing (task 193's slots).</summary>
public readonly record struct WornItem(long ItemInstanceId, int Condition);

/// <summary>What became of one carried stack: all of it is lost, part dropped and the rest destroyed.</summary>
/// <param name="DroppedCostBasis">The share of the stack's cost basis that went into the container.</param>
public readonly record struct LostGoods(
    int ItemDefId, ItemStyle Style, int Dropped, int Destroyed, Credits DroppedCostBasis);

/// <summary>What became of one worn item: kept, at a lower condition.</summary>
public readonly record struct WornDown(long ItemInstanceId, int Before, int After);

/// <summary>Everything one death costs.</summary>
public sealed record DeathCostPlan(IReadOnlyList<LostGoods> Lost, IReadOnlyList<WornDown> Worn)
{
    /// <summary>A death that costs nothing: at a station.</summary>
    public static DeathCostPlan Nothing { get; } = new([], []);

    /// <summary>Whether anything drops where the body fell, which is whether there is a container at all.</summary>
    public bool DropsAnything => Lost.Any(l => l.Dropped > 0);
}

/// <summary>
/// What dying costs (ADR-0016, task 103): the materials you carry, half dropped and half destroyed, and wear on
/// what you wear.
/// </summary>
/// <remarks>
/// <para>
/// <strong>No roll in it.</strong> ADR-0006 rolled each item against a table by cause; ADR-0016 has nothing left to
/// chance, so what a death costs follows from what was carried, and the same death always costs the same.
/// </para>
/// <para>
/// <strong>Materials only</strong>: the stackable categories (<see cref="ItemCategoryExtensions.IsStackable"/>).
/// Weapons, tools and armour are kept whether worn or carried, and only what is worn wears.
/// </para>
/// </remarks>
public static class DeathCosts
{
    /// <summary>Condition a worn item loses each death: 10% of the 0–100 scale (ADR-0016, Joe's preview number).</summary>
    public const int WearPerDeath = 10;

    /// <summary>How long the dropped half lies where it fell before it is destroyed (ADR-0016).</summary>
    public static readonly TimeSpan LootableFor = TimeSpan.FromMinutes(10);

    /// <summary>
    /// How a lost stack divides: half dropped, half destroyed, and <strong>an odd stack's extra unit drops</strong>
    /// (Joe, 19 October). Five ore is three dropped and two destroyed; a single unit drops.
    /// </summary>
    /// <exception cref="ArgumentOutOfRangeException">If the quantity is not positive.</exception>
    public static (int Dropped, int Destroyed) Split(int quantity)
    {
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(quantity);

        int destroyed = quantity / 2;

        return (quantity - destroyed, destroyed);
    }

    /// <summary>A worn item's condition after a death: ten lower, and never below broken.</summary>
    public static int Worn(int condition) => Math.Max(0, condition - WearPerDeath);

    /// <summary>
    /// The cause of a death on foot: at a station when the character was docked at one, which costs nothing.
    /// </summary>
    public static DeathCause OnFoot(bool docked) =>
        docked ? DeathCause.PersonalCombatStation : DeathCause.PersonalCombatPlanet;

    /// <summary>Whether a death costs anything at all (ADR-0016: dying at a station does not).</summary>
    public static bool CostsAnything(DeathCause cause) => cause != DeathCause.PersonalCombatStation;

    /// <summary>What one death costs.</summary>
    /// <param name="cause">How they died. At a station, nothing.</param>
    /// <param name="carried">Every stack they carried; anything not stackable is ignored, since it is kept.</param>
    /// <param name="worn">Everything they wore.</param>
    public static DeathCostPlan Plan(
        DeathCause cause, IReadOnlyList<CarriedGoods> carried, IReadOnlyList<WornItem> worn)
    {
        ArgumentNullException.ThrowIfNull(carried);
        ArgumentNullException.ThrowIfNull(worn);

        if (!CostsAnything(cause))
        {
            return DeathCostPlan.Nothing;
        }

        var lost = new List<LostGoods>();

        foreach (CarriedGoods stack in carried)
        {
            if (!stack.Category.IsStackable() || stack.Quantity <= 0)
            {
                continue;
            }

            (int dropped, int destroyed) = Split(stack.Quantity);

            lost.Add(new LostGoods(
                stack.ItemDefId, stack.Style, dropped, destroyed, ShareOf(stack.CostBasis, dropped, stack.Quantity)));
        }

        WornDown[] wornDown = [.. worn.Select(w => new WornDown(w.ItemInstanceId, w.Condition, Worn(w.Condition)))];

        return new DeathCostPlan(lost, wornDown);
    }

    /// <summary>
    /// The cost basis that goes with part of a stack: floored, as <c>InventoryService.RemoveAsync</c> takes a share,
    /// so what goes and what stays always sum back to the whole.
    /// </summary>
    public static Credits ShareOf(Credits basis, int part, int whole)
    {
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(whole);
        ArgumentOutOfRangeException.ThrowIfNegative(part);

        // Int128 keeps the intermediate exact, as RemoveAsync's does.
        return Credits.FromMinorUnits((long)((Int128)basis.MinorUnits * part / whole));
    }
}
