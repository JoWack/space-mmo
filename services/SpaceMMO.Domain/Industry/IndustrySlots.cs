using SpaceMMO.Domain.Progression;

namespace SpaceMMO.Domain.Industry;

/// <summary>
/// How many jobs a character may run at once, per design-bible §6.
/// </summary>
/// <remarks>
/// <para>
/// Slots are counted <strong>per skill</strong>, not globally: a refining job consumes a
/// refining slot. A master refiner therefore runs five refining lines while still being limited
/// to one shipcrafting line, which is what turns specialisation into a real choice rather than
/// a flavour label.
/// </para>
/// <para>
/// Slots are also what give job cancellation a cost beyond materials. A blocked slot is lost
/// throughput, so starting the wrong recipe stings even when the refund is generous.
/// </para>
/// </remarks>
public static class IndustrySlots
{
    /// <summary>Slots available at level 1, before any threshold is reached.</summary>
    public const int BaseSlots = 1;

    /// <summary>
    /// Levels that grant an extra slot.
    /// </summary>
    /// <remarks>
    /// Spaced to land roughly at the quarter points of the grind rather than of the level range —
    /// level 50 is only about 0.8% of the XP needed for 99 (ADR-0004), so evenly spaced levels
    /// would front-load nearly every reward into the first few hours.
    /// </remarks>
    private static readonly int[] SlotThresholds = [25, 50, 75, 99];

    /// <summary>Maximum slots at <see cref="SkillCurve.MaxLevel"/>.</summary>
    public static int MaxSlots => BaseSlots + SlotThresholds.Length;

    /// <summary>
    /// Concurrent jobs allowed for a skill at a given level.
    /// </summary>
    /// <exception cref="ArgumentOutOfRangeException">If the level is outside 1..99.</exception>
    public static int MaxConcurrentJobs(int skillLevel)
    {
        if (skillLevel is < SkillCurve.MinLevel or > SkillCurve.MaxLevel)
        {
            throw new ArgumentOutOfRangeException(
                nameof(skillLevel),
                skillLevel,
                $"Level must be between {SkillCurve.MinLevel} and {SkillCurve.MaxLevel}.");
        }

        int slots = BaseSlots;

        foreach (int threshold in SlotThresholds)
        {
            if (skillLevel >= threshold)
            {
                slots++;
            }
        }

        return slots;
    }

    /// <summary>
    /// The next level that grants a slot, or null once every threshold is passed.
    /// </summary>
    /// <remarks>Exists so the UI can show what the player is working toward.</remarks>
    public static int? NextSlotLevel(int skillLevel)
    {
        foreach (int threshold in SlotThresholds)
        {
            if (skillLevel < threshold)
            {
                return threshold;
            }
        }

        return null;
    }
}
