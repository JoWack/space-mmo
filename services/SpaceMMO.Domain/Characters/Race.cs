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
/// Named 30 September 2026, by Joe: A is <em>Humanity United</em>, B is <em>Tusk and Thorn</em>
/// (design-bible §1). The names are display text, served by <see cref="Factions.DisplayName"/>;
/// the members stay <c>A</c> and <c>B</c>.
/// </para>
/// <para>
/// Because enums persist as strings, renaming the members <em>is</em> a data migration, and it
/// would buy nothing a display name does not — which is why the names live there instead.
/// </para>
/// </remarks>
public enum Faction
{
    /// <summary>Humanity United: the Humanoid and Martian homeworlds.</summary>
    A = 0,

    /// <summary>Tusk and Thorn: the Space Elf and Space Orc homeworlds.</summary>
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

    /// <summary>What to call a race to a player: "Space Elf", not "SpaceElf".</summary>
    /// <exception cref="ArgumentOutOfRangeException">If the race is not a defined value.</exception>
    public static string DisplayName(Race race) => race switch
    {
        Race.Humanoid => "Humanoid",
        Race.Martian => "Martian",
        Race.SpaceElf => "Space Elf",
        Race.SpaceOrc => "Space Orc",
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

/// <summary>
/// Faction facts that are not about any one race.
/// </summary>
public static class Factions
{
    /// <summary>
    /// What to call a faction to a player. Final names, per design-bible §1 (Joe, 30 September 2026).
    /// </summary>
    /// <exception cref="ArgumentOutOfRangeException">If the faction is not a defined value.</exception>
    public static string DisplayName(Faction faction) => faction switch
    {
        Faction.A => "Humanity United",
        Faction.B => "Tusk and Thorn",
        _ => throw new ArgumentOutOfRangeException(nameof(faction), faction, "Unknown faction."),
    };
}
