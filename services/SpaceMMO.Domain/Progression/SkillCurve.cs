namespace SpaceMMO.Domain.Progression;

/// <summary>
/// The XP-to-level curve for all skills, per ADR-0004.
/// </summary>
/// <remarks>
/// <para>
/// Level is <em>always</em> derived from XP and never stored. That makes it
/// impossible for a character's level and XP to disagree, which removes an entire
/// class of desync bug — and it means granting XP is the only write path.
/// </para>
/// <para>
/// The curve is the RuneScape formula, adopted unmodified:
/// <c>xp(L) = floor( sum(n=1..L-1) floor(n + 300 * 2^(n/7)) / 4 )</c>, giving
/// 13,034,431 XP at level 99.
/// </para>
/// </remarks>
public static class SkillCurve
{
    /// <summary>Lowest level a skill can have. Characters start every skill here.</summary>
    public const int MinLevel = 1;

    /// <summary>Highest attainable level.</summary>
    public const int MaxLevel = 99;

    /// <summary>Total XP required to reach <see cref="MaxLevel"/>.</summary>
    public const long MaxLevelXp = 13_034_431L;

    /// <summary>
    /// Cumulative XP thresholds, indexed by level. <c>_thresholds[L]</c> is the total
    /// XP needed to be level <c>L</c>, so <c>_thresholds[1] == 0</c>.
    /// </summary>
    private static readonly long[] Thresholds = BuildThresholds();

    /// <summary>
    /// Total XP required to be at <paramref name="level"/>.
    /// </summary>
    /// <exception cref="ArgumentOutOfRangeException">
    /// If <paramref name="level"/> is outside 1..99.
    /// </exception>
    public static long XpForLevel(int level)
    {
        if (level is < MinLevel or > MaxLevel)
        {
            throw new ArgumentOutOfRangeException(
                nameof(level), level, $"Level must be between {MinLevel} and {MaxLevel}.");
        }

        return Thresholds[level];
    }

    /// <summary>
    /// The level corresponding to a total XP amount. XP above the level-99 threshold
    /// still reports 99 — overflow XP is tracked but confers no further levels.
    /// </summary>
    /// <exception cref="ArgumentOutOfRangeException">If <paramref name="xp"/> is negative.</exception>
    public static int LevelForXp(long xp)
    {
        ThrowIfNegative(xp);

        // Fast path: the overwhelming majority of lookups at high level, and it
        // keeps the binary search below from needing an upper-bound special case.
        if (xp >= MaxLevelXp)
        {
            return MaxLevel;
        }

        // Search levels 2..99 for the highest threshold that xp meets or exceeds.
        // Index 0 is excluded because it is not a real level.
        int found = Array.BinarySearch(Thresholds, MinLevel, MaxLevel - MinLevel + 1, xp);

        // An exact hit means xp sits precisely on a threshold, so that is the level.
        // Otherwise BinarySearch returns the bitwise complement of the insertion
        // point, and the level is the one below it.
        return found >= 0 ? found : ~found - 1;
    }

    /// <summary>
    /// XP still needed to reach the next level, or 0 at <see cref="MaxLevel"/>.
    /// </summary>
    /// <exception cref="ArgumentOutOfRangeException">If <paramref name="xp"/> is negative.</exception>
    public static long XpToNextLevel(long xp)
    {
        ThrowIfNegative(xp);

        int level = LevelForXp(xp);
        return level >= MaxLevel ? 0L : Thresholds[level + 1] - xp;
    }

    /// <summary>
    /// Fractional progress through the current level, in 0..1, for progress bars.
    /// Returns 1 at <see cref="MaxLevel"/>.
    /// </summary>
    /// <remarks>
    /// This is the one place a floating-point value is acceptable, because it is
    /// presentation only and never feeds a gameplay or economic decision.
    /// </remarks>
    /// <exception cref="ArgumentOutOfRangeException">If <paramref name="xp"/> is negative.</exception>
    public static double ProgressToNextLevel(long xp)
    {
        ThrowIfNegative(xp);

        int level = LevelForXp(xp);
        if (level >= MaxLevel)
        {
            return 1.0;
        }

        long levelStart = Thresholds[level];
        long levelEnd = Thresholds[level + 1];

        return (double)(xp - levelStart) / (levelEnd - levelStart);
    }

    /// <summary>
    /// Level plus progress in a single lookup, so UI does not pay for the binary
    /// search three times to render one skill row.
    /// </summary>
    /// <exception cref="ArgumentOutOfRangeException">If <paramref name="xp"/> is negative.</exception>
    public static SkillProgress Describe(long xp)
    {
        ThrowIfNegative(xp);

        int level = LevelForXp(xp);

        if (level >= MaxLevel)
        {
            return new SkillProgress(MaxLevel, xp, 0L, 1.0);
        }

        long levelStart = Thresholds[level];
        long levelEnd = Thresholds[level + 1];

        return new SkillProgress(
            Level: level,
            Xp: xp,
            XpToNextLevel: levelEnd - xp,
            ProgressToNextLevel: (double)(xp - levelStart) / (levelEnd - levelStart));
    }

    /// <summary>
    /// Builds the cumulative threshold table once, at type initialization.
    /// </summary>
    /// <remarks>
    /// <para>
    /// The inner term is floored per level and accumulated; the division by 4 is
    /// floored at each level against the running total. Doing it in the other order
    /// produces subtly different values at some levels.
    /// </para>
    /// <para>
    /// <c>Math.Pow</c> is used here even though generation code is restricted to
    /// integer math (ADR-0002). The magnitudes involved are small — the largest term
    /// is under 5 million — and <c>2^(n/7)</c> is either exactly representable
    /// (when <c>n</c> is a multiple of 7) or comfortably far from an integer
    /// boundary, so no <c>floor</c> here sits near a tipping point. The unit tests
    /// pin a spread of published values, which would catch any platform variance.
    /// </para>
    /// </remarks>
    private static long[] BuildThresholds()
    {
        var thresholds = new long[MaxLevel + 1];

        // Index 0 is not a level. Kept at 0 so the binary search over 1..99 has a
        // well-defined lower neighbour.
        thresholds[0] = 0L;
        thresholds[MinLevel] = 0L;

        long accumulator = 0L;

        for (int level = MinLevel; level < MaxLevel; level++)
        {
            accumulator += (long)Math.Floor(level + (300.0 * Math.Pow(2.0, level / 7.0)));
            thresholds[level + 1] = accumulator / 4L;
        }

        return thresholds;
    }

    private static void ThrowIfNegative(long xp)
    {
        if (xp < 0L)
        {
            throw new ArgumentOutOfRangeException(
                nameof(xp), xp, "XP cannot be negative. A negative value indicates a bug upstream.");
        }
    }
}
