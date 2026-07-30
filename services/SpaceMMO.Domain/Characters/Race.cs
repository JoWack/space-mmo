namespace SpaceMMO.Domain.Characters;

/// <summary>
/// Playable races, per design-bible §1. Chosen at character creation and never changeable.
/// </summary>
/// <remarks>
/// Race determines faction and starting planet. Persisted as a string, so these names are
/// part of the schema — adding a member is safe, renaming one is a data migration.
/// </remarks>
public enum Race
{
    /// <summary>Earth-like origin: temperate, oceans, familiar.</summary>
    Humanoid = 0,

    /// <summary>Mars-like origin: thin atmosphere, red desert, low gravity.</summary>
    Martian = 1,

    /// <summary>Lush, luminous, quasi-mystical origin — a planet that reads as alive.</summary>
    SpaceElf = 2,

    /// <summary>Dark, industrial, grimy origin; heavy gravity and perpetual overcast.</summary>
    SpaceOrc = 3,
}

/// <summary>
/// The two factions, per design-bible §1.
/// </summary>
/// <remarks>
/// <para>
/// TODO(name): both factions still need names, so the members are bare placeholders rather
/// than invented lore.
/// </para>
/// <para>
/// Because enums persist as strings, renaming these later <em>is</em> a data migration — a
/// one-statement <c>UPDATE characters SET ...</c>, trivial before launch and worth doing
/// before any real player data exists.
/// </para>
/// </remarks>
public enum Faction
{
    /// <summary>Humanoid and Martian homeworlds. TODO(name).</summary>
    A = 0,

    /// <summary>Space Elf and Space Orc homeworlds. TODO(name).</summary>
    B = 1,
}

/// <summary>
/// Race-derived facts, per design-bible §1.
/// </summary>
/// <remarks>
/// Faction and starting body are <em>computed</em> from race rather than stored alongside
/// it. Storing all three would allow a character row to claim a Space Orc in Faction A,
/// and a validation rule you cannot violate beats one you have to remember to check.
/// </remarks>
public static class Races
{
    /// <summary>The faction a race belongs to.</summary>
    /// <exception cref="ArgumentOutOfRangeException">If the race is not a defined value.</exception>
    public static Faction FactionFor(Race race) => race switch
    {
        Race.Humanoid or Race.Martian => Faction.A,
        Race.SpaceElf or Race.SpaceOrc => Faction.B,
        _ => throw new ArgumentOutOfRangeException(nameof(race), race, "Unknown race."),
    };

    /// <summary>
    /// The key of the body a character of this race starts on. All four sit in the starting
    /// system at the galactic centre.
    /// </summary>
    /// <exception cref="ArgumentOutOfRangeException">If the race is not a defined value.</exception>
    public static string HomeBodyKeyFor(Race race) => race switch
    {
        Race.Humanoid => "body_terra",
        Race.Martian => "body_ares",
        Race.SpaceElf => "body_verdance",
        Race.SpaceOrc => "body_grimhold",
        _ => throw new ArgumentOutOfRangeException(nameof(race), race, "Unknown race."),
    };
}
