namespace SpaceMMO.Domain.Loss;

/// <summary>
/// How a character died. Determines what survives, per ADR-0006.
/// </summary>
/// <remarks>
/// Persisted on the death record, so names are part of the schema. The distinction
/// between <see cref="ShipExplosion"/> and <see cref="ShipDisabled"/> is gameplay-visible
/// and drives a deliberate tactical fork: a pirate who wants loot must disable rather
/// than obliterate.
/// </remarks>
public enum DeathCause
{
    /// <summary>Hull detonation — reactor breach or catastrophic structural failure.</summary>
    ShipExplosion = 0,

    /// <summary>Hull integrity lost without detonation; leaves a salvageable wreck.</summary>
    ShipDisabled = 1,

    /// <summary>Killed on foot on a planet surface.</summary>
    PersonalCombatPlanet = 2,

    /// <summary>Killed on foot inside a station. Stations are safe zones; nothing is lost.</summary>
    PersonalCombatStation = 3,

    /// <summary>Fall, atmosphere, or temperature. No attacker.</summary>
    EnvironmentalPlanet = 4,

    /// <summary>Deliberate. Nothing survives, and any insurance policy is void.</summary>
    SelfDestruct = 5,
}

/// <summary>
/// What happened to one item as a result of a death.
/// </summary>
public enum LootOutcome
{
    /// <summary>Intact and lootable.</summary>
    Survived = 0,

    /// <summary>Lootable but at reduced condition; unusable until repaired.</summary>
    Damaged = 1,

    /// <summary>Removed from the game entirely. This is the material sink.</summary>
    Destroyed = 2,
}
