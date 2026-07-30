namespace SpaceMMO.Domain.Progression;

/// <summary>
/// A skill's derived progression state, as shown in the skills panel.
/// </summary>
/// <param name="Level">Derived level, 1..99.</param>
/// <param name="Xp">Total accumulated XP, including any overflow past level 99.</param>
/// <param name="XpToNextLevel">XP remaining to the next level; 0 at level 99.</param>
/// <param name="ProgressToNextLevel">Fractional progress through the current level, 0..1.</param>
/// <remarks>
/// Produced by <see cref="SkillCurve.Describe"/>. This is a projection for display
/// and is never persisted — <see cref="Xp"/> is the only stored value, per ADR-0004.
/// </remarks>
public readonly record struct SkillProgress(
    int Level,
    long Xp,
    long XpToNextLevel,
    double ProgressToNextLevel);
