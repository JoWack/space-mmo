namespace SpaceMMO.Domain.Combat;

/// <summary>
/// How much health a character has, and how it comes back (ADR-0017, task 194).
/// </summary>
/// <remarks>
/// <para>
/// <strong>1,000 at constitution 1, rising with each level to 2,000 at 99</strong> (Joe, 16 October). A veteran
/// is tougher, and two newcomers can still bring one down.
/// </para>
/// <para>
/// <strong>Out of combat it comes back quickly</strong> (Joe, 16 October): 10 s after the last damage dealt or
/// taken, at 5% of the maximum a second, so full in 20 s from nothing. Fights stay fights, and a skirmish does
/// not cripple anybody for the evening.
/// </para>
/// <para>
/// <strong>Stored as of the last fight, and worked out when asked.</strong> The database holds the value health
/// had at the last moment of combat and when that was; what it is now follows from the clock. Nothing ticks,
/// so nothing has to run on the server for health to come back while a player is offline.
/// </para>
/// </remarks>
public static class CombatHealth
{
    /// <summary>Health at constitution 1.</summary>
    public const int AtLevelOne = 1_000;

    /// <summary>Health at constitution 99: twice level 1's.</summary>
    public const int AtLevelNinetyNine = 2_000;

    /// <summary>How long after the last damage dealt or taken health starts coming back.</summary>
    public static readonly TimeSpan RegenDelay = TimeSpan.FromSeconds(10);

    /// <summary>How fast it comes back, in percent of the maximum a second.</summary>
    public const int RegenPercentPerSecond = 5;

    /// <summary>The most health a character with this constitution level can have.</summary>
    /// <remarks>Linear in the level, rounded to the nearest point; a level outside 1–99 is clamped.</remarks>
    public static int Max(int constitutionLevel)
    {
        int level = Math.Clamp(constitutionLevel, 1, 99);

        // Integer and rounded half up: (1000 * (L - 1) + 49) / 98, added to level 1's.
        int above = ((AtLevelNinetyNine - AtLevelOne) * (level - 1) + 49) / 98;

        return AtLevelOne + above;
    }

    /// <summary>
    /// Health now, from what it was at the last moment of combat.
    /// </summary>
    /// <param name="stored">Health as of <paramref name="lastCombatAt"/>, or null for a character never hurt.</param>
    /// <param name="max">The character's maximum now (<see cref="Max"/>).</param>
    /// <param name="lastCombatAt">When they last dealt or took damage, or null if never.</param>
    /// <param name="now">The server's clock.</param>
    /// <remarks>
    /// Never above the maximum, which can have risen since (a constitution level gained in the same fight), and
    /// never below zero. Zero stays zero: the dead do not regenerate, they respawn.
    /// </remarks>
    public static int Current(int? stored, int max, DateTimeOffset? lastCombatAt, DateTimeOffset now)
    {
        if (stored is not int was)
        {
            return max;
        }

        if (was <= 0)
        {
            return 0;
        }

        if (lastCombatAt is not { } at)
        {
            return Math.Min(was, max);
        }

        TimeSpan regenerating = now - at - RegenDelay;

        if (regenerating <= TimeSpan.Zero)
        {
            return Math.Min(was, max);
        }

        // Whole milliseconds, so the rate is exact rather than a per-second step.
        long gained = (long)max * RegenPercentPerSecond * (long)regenerating.TotalMilliseconds / 100_000;

        return (int)Math.Min(max, was + gained);
    }
}
