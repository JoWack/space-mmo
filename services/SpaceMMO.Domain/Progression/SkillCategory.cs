namespace SpaceMMO.Domain.Progression;

/// <summary>
/// The three skill categories, per design-bible §2.
/// </summary>
/// <remarks>Persisted as a string; these names are part of the schema.</remarks>
public enum SkillCategory
{
    /// <summary>Gathering, crafting, refining, cooking, construction.</summary>
    Life = 0,

    /// <summary>Personal combat, plus the constitution and stamina pools.</summary>
    Combat = 1,

    /// <summary>Ship handling, ship weapons, warp.</summary>
    Pilot = 2,
}
