namespace SpaceMMO.Domain.Loss;

/// <summary>
/// How a character died, as recorded on the death record.
/// </summary>
/// <remarks>
/// <para>
/// Persisted by name, so names are part of the schema. Written for ADR-0006, where the cause decided what
/// survived; under ADR-0016 only two facts about a death matter to its cost — whether it happened at a station,
/// which costs nothing, and whether a ship went down with its pilot, whose hold is then lost too — and only the
/// on-foot causes can happen yet (task 103).
/// </para>
/// <para>
/// The ship and environment causes are kept because nothing can die that way yet and they will be needed as they
/// are: what a disabled ship gives up is still open (ADR-0016).
/// </para>
/// </remarks>
public enum DeathCause
{
    /// <summary>Hull detonation — reactor breach or catastrophic structural failure.</summary>
    ShipExplosion = 0,

    /// <summary>Hull integrity lost without detonation; leaves a wreck.</summary>
    ShipDisabled = 1,

    /// <summary>Killed on foot away from any station.</summary>
    PersonalCombatPlanet = 2,

    /// <summary>Killed on foot while docked at a station. Costs nothing (ADR-0016): hubs stay safe.</summary>
    PersonalCombatStation = 3,

    /// <summary>Fall, atmosphere, or temperature. No attacker.</summary>
    EnvironmentalPlanet = 4,

    /// <summary>Deliberate.</summary>
    SelfDestruct = 5,
}
