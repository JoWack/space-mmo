using SpaceMMO.Domain.Economy;
using SpaceMMO.Domain.Loss;

namespace SpaceMMO.Domain.Industry;

/// <summary>What one repair does to an item (task 196).</summary>
/// <param name="Restored">
/// Whole points between the item's condition and its whole maximum. The maximum lost, the materials and the credits are
/// all in proportion to this.
/// </param>
/// <param name="ConditionAfter">Its condition after: the new maximum's whole points.</param>
/// <param name="MaxAfterHundredths">Its maximum after, in hundredths of a point.</param>
/// <param name="WornOut">
/// Whether this repair uses it up. The item is destroyed rather than repaired, and nothing is charged.
/// </param>
public readonly record struct RepairPlan(int Restored, int ConditionAfter, int MaxAfterHundredths, bool WornOut);

/// <summary>
/// What a repair at a station's industry bay does and costs (ADR-0016 as amended 24 October, task 196).
/// </summary>
/// <remarks>
/// <para>
/// <strong>Condition comes back to the maximum, and the maximum falls in proportion to what came back</strong>: 18 per
/// 100 restored. A top-up after one death costs about 2; a full repair from broken costs 18. Repaired after every
/// death, a new item's maximum falls 1.8 a death and it is worn out at its 51st repair: Joe's "about 50 deaths".
/// </para>
/// <para>
/// <strong>The maximum is kept in hundredths of a point</strong>, so that small repairs are not rounded to nothing or
/// to a whole point. 18 per 100 is exactly 18 hundredths a point, so no repair rounds at all. Condition stays whole
/// points, and a repair brings it to the whole part of the new maximum.
/// </para>
/// <para>
/// <strong>Material owed is kept in ten-thousandths of a unit</strong>, as a tab on each item. A whole unit is charged
/// when the tab reaches one, so every repair costs exactly its share and topping up ten times costs what one full
/// repair does (Joe, 25 October).
/// </para>
/// </remarks>
public static class Repairs
{
    /// <summary>A new item's condition and maximum, in points.</summary>
    public const int FullCondition = 100;

    /// <summary>How finely the maximum is kept: hundredths of a point.</summary>
    public const int HundredthsPerPoint = 100;

    /// <summary>A new item's maximum, in hundredths.</summary>
    public const int NewMaxHundredths = FullCondition * HundredthsPerPoint;

    /// <summary>
    /// Points of maximum lost for every 100 restored: 18, about 50 deaths' lifetime (Joe, 24 October), against 10
    /// (about 90) and 2 (about 450).
    /// </summary>
    public const int MaxLostPerHundredRestored = 18;

    /// <summary>A full repair takes a quarter of the item's recipe (Joe, 25 October).</summary>
    public const int RecipeShareDivisor = 4;

    /// <summary>How finely the tab of material owed is kept: ten-thousandths of a unit.</summary>
    public const int TabPerUnit = 10_000;

    /// <summary>
    /// Below this maximum, in hundredths, an item is worn out: less than one death's wear, so the next death would
    /// break it whatever its owner did. The threshold the "about 50 deaths" Joe chose was worked from.
    /// </summary>
    public const int WornOutBelowHundredths = DeathCosts.WearPerDeath * HundredthsPerPoint;

    /// <summary>The whole points a maximum allows: what a repair brings condition up to.</summary>
    public static int WholeMax(int maxHundredths) => maxHundredths / HundredthsPerPoint;

    /// <summary>Whether a repair would restore anything: condition below the whole maximum.</summary>
    public static bool IsDamaged(int condition, int maxHundredths) => condition < WholeMax(maxHundredths);

    /// <summary>What repairing an item does to it.</summary>
    /// <exception cref="ArgumentOutOfRangeException">
    /// If either value is out of range, or the item is not damaged, which no repair is offered for.
    /// </exception>
    public static RepairPlan Plan(int condition, int maxHundredths)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(condition);
        ArgumentOutOfRangeException.ThrowIfNegative(maxHundredths);
        ArgumentOutOfRangeException.ThrowIfGreaterThan(maxHundredths, NewMaxHundredths);

        if (!IsDamaged(condition, maxHundredths))
        {
            throw new ArgumentOutOfRangeException(
                nameof(condition), condition, $"Nothing to repair at {condition} of {maxHundredths / 100.0:0.##}.");
        }

        int restored = WholeMax(maxHundredths) - condition;

        // Exact: a hundredth a point and "per 100 restored" cancel, leaving 18 hundredths for each point.
        int lost = restored * HundredthsPerPoint * MaxLostPerHundredRestored / FullCondition;
        int maxAfter = maxHundredths - lost;

        return new RepairPlan(restored, WholeMax(maxAfter), maxAfter, maxAfter < WornOutBelowHundredths);
    }

    /// <summary>
    /// What each point restored adds to the tab for one recipe input, in ten-thousandths of a unit: a quarter of the
    /// input per 100 points, shared among what one run makes.
    /// </summary>
    /// <remarks>
    /// Exact whenever the run's output divides the input times 25, which every recipe making a single item does. Any
    /// other is rounded to the nearest ten-thousandth, half a ten-thousandth of a unit a point at worst.
    /// </remarks>
    /// <exception cref="ArgumentOutOfRangeException">If either quantity is not positive.</exception>
    public static long OwedPerPoint(int inputQuantity, int outputQuantity)
    {
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(inputQuantity);
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(outputQuantity);

        long numerator = (long)inputQuantity * TabPerUnit;
        long denominator = (long)outputQuantity * RecipeShareDivisor * FullCondition;

        return (numerator + (denominator / 2)) / denominator;
    }

    /// <summary>
    /// What a repair charges of one input: the whole units its tab reaches, and what is left on the tab after.
    /// </summary>
    /// <exception cref="ArgumentOutOfRangeException">If the tab is not below one unit, or anything is negative.</exception>
    public static (int Units, int TabAfter) Charge(int tabBefore, long owedPerPoint, int restored)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(tabBefore);
        ArgumentOutOfRangeException.ThrowIfGreaterThanOrEqual(tabBefore, TabPerUnit);
        ArgumentOutOfRangeException.ThrowIfNegative(owedPerPoint);
        ArgumentOutOfRangeException.ThrowIfNegative(restored);

        long total = tabBefore + (owedPerPoint * restored);

        return ((int)(total / TabPerUnit), (int)(total % TabPerUnit));
    }

    /// <summary>
    /// The credits a repair costs: the industry bay's fee for one run (<see cref="IndustryFees.ForJob(int)"/>) for 100
    /// points, its share for less, to the nearest credit with a half rounding up (Joe, 25 October).
    /// </summary>
    /// <remarks>Not carried like the materials: Joe's words were "rounded to the credit". Three points or fewer is free.</remarks>
    /// <exception cref="ArgumentOutOfRangeException">If restored is negative.</exception>
    public static Credits CreditsFor(int restored)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(restored);

        long share = IndustryFees.ForJob(1).MinorUnits * restored;
        long perCredit = FullCondition * Credits.MinorUnitsPerCredit;

        return Credits.FromWholeCredits((share + (perCredit / 2)) / perCredit);
    }
}
