namespace SpaceMMO.Domain.Combat;

/// <summary>
/// How big a character's stamina pool is, and what spending it is worth (ADR-0017, tasks 101 and 195).
/// </summary>
/// <remarks>
/// <para>
/// <strong>100 at stamina 1, rising with each level to 150 at 99</strong> (Joe, 16 October). A veteran sprints or
/// dodges for longer; a newcomer can still get away. The pool itself, its refill and what spends it are the game
/// server's walk model: only the size is decided here, where the level is.
/// </para>
/// <para>
/// <strong>Stamina earns XP for stamina spent</strong>, in a fight or out of one, so it levels as a character
/// moves around the world. Running in circles levels it too, slowly, and that was accepted (ADR-0017).
/// </para>
/// </remarks>
public static class StaminaPool
{
    /// <summary>The skill that sizes the pool and earns XP for spending it.</summary>
    public const string SkillKey = "stamina";

    /// <summary>The pool at stamina 1.</summary>
    public const int AtLevelOne = 100;

    /// <summary>The pool at stamina 99: half again level 1's.</summary>
    public const int AtLevelNinetyNine = 150;

    /// <summary>The largest pool a character with this stamina level can have.</summary>
    /// <remarks>Linear in the level, rounded to the nearest point, as health is; a level outside 1–99 is clamped.</remarks>
    public static int Max(int staminaLevel)
    {
        int level = Math.Clamp(staminaLevel, 1, 99);

        // Integer and rounded half up: (50 * (L - 1) + 49) / 98, added to level 1's.
        int above = ((AtLevelNinetyNine - AtLevelOne) * (level - 1) + 49) / 98;

        return AtLevelOne + above;
    }

    /// <summary>
    /// XP for stamina spent: the points spent times the skill's rate, authored per hundred points, rounded half up.
    /// </summary>
    /// <remarks>
    /// The rate is content. The game server reports what was spent in batches of whole points, so the rounding
    /// costs at most half an XP a batch.
    /// </remarks>
    /// <exception cref="ArgumentOutOfRangeException">If either is negative.</exception>
    public static long XpForSpent(int spent, int xpPerHundredSpent)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(spent);
        ArgumentOutOfRangeException.ThrowIfNegative(xpPerHundredSpent);

        return ((long)spent * xpPerHundredSpent + 50) / 100;
    }
}
