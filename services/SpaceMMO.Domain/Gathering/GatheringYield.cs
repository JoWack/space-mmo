using SpaceMMO.Domain.Progression;

namespace SpaceMMO.Domain.Gathering;

/// <summary>
/// How fast a character extracts material, per design-bible §2.
/// </summary>
/// <remarks>
/// <para>
/// Deliberately deterministic rather than a per-tick success roll. A roll would be more
/// RuneScape-authentic, but deterministic yield makes EconSim's material-flow measurements
/// depend on the rates being tested rather than on variance, which matters far more while the
/// economy is being balanced than the texture of individual swings does.
/// </para>
/// <para>
/// Levelling multiplies throughput about fourfold across the whole grind. That is enough to feel
/// worth pursuing without making a level-99 gatherer flood the market relative to a beginner —
/// the raw material supply curve is what every downstream price rests on.
/// </para>
/// </remarks>
public static class GatheringYield
{
    /// <summary>Seconds per extraction tick. The same for every skill and node.</summary>
    public const int TickSeconds = 3;

    /// <summary>
    /// Ticks a character may bank while away from the keyboard.
    /// </summary>
    /// <remarks>
    /// Gathering is an active verb, unlike industry jobs, which are explicitly designed to run
    /// while logged off. A small bank absorbs latency and brief interruptions without letting an
    /// idle client return and claim an hour of extraction it never performed.
    /// </remarks>
    public const int MaxBankedTicks = 20;

    /// <summary>Units extracted per tick at a given skill level.</summary>
    /// <remarks>1 unit at level 1, rising to 4 at level 75 and above.</remarks>
    /// <exception cref="ArgumentOutOfRangeException">If the level is outside 1..99.</exception>
    public static int UnitsPerTick(int skillLevel)
    {
        if (skillLevel is < SkillCurve.MinLevel or > SkillCurve.MaxLevel)
        {
            throw new ArgumentOutOfRangeException(
                nameof(skillLevel),
                skillLevel,
                $"Level must be between {SkillCurve.MinLevel} and {SkillCurve.MaxLevel}.");
        }

        return 1 + (skillLevel / 25);
    }

    /// <summary>
    /// Ticks the elapsed wall-clock time entitles a character to.
    /// </summary>
    /// <remarks>
    /// This is what makes gathering server-authoritative. The client asks to gather; the server
    /// decides how much time has actually passed since they last did, so a client that asks a
    /// hundred times a second still extracts exactly as much as one asking every three seconds.
    /// </remarks>
    /// <exception cref="ArgumentOutOfRangeException">If elapsed seconds is negative.</exception>
    public static int EntitledTicks(long elapsedSeconds)
    {
        if (elapsedSeconds < 0)
        {
            throw new ArgumentOutOfRangeException(
                nameof(elapsedSeconds), elapsedSeconds, "Elapsed time cannot be negative.");
        }

        long ticks = elapsedSeconds / TickSeconds;

        return (int)Math.Min(ticks, MaxBankedTicks);
    }

    /// <summary>
    /// Units a character may extract given elapsed time, skill, and what the node still holds.
    /// </summary>
    /// <returns>Zero if not enough time has passed or the node is empty.</returns>
    /// <exception cref="ArgumentOutOfRangeException">
    /// If elapsed seconds is negative, the level is out of range, or the remaining quantity is
    /// negative.
    /// </exception>
    public static int UnitsAvailable(long elapsedSeconds, int skillLevel, int nodeRemaining)
    {
        if (nodeRemaining < 0)
        {
            throw new ArgumentOutOfRangeException(
                nameof(nodeRemaining), nodeRemaining, "Remaining quantity cannot be negative.");
        }

        int ticks = EntitledTicks(elapsedSeconds);

        if (ticks == 0 || nodeRemaining == 0)
        {
            return 0;
        }

        return Math.Min(ticks * UnitsPerTick(skillLevel), nodeRemaining);
    }

    /// <summary>XP awarded per unit extracted.</summary>
    /// <remarks>
    /// Per unit rather than per tick, so a high-level gatherer earns XP at the same rate per
    /// material as a beginner. Paying per tick would make levelling accelerate itself.
    /// </remarks>
    public const long XpPerUnit = 5;
}
