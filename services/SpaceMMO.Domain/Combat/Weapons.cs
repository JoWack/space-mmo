namespace SpaceMMO.Domain.Combat;

/// <summary>A weapon's family: what it fires or swings (ADR-0015).</summary>
/// <remarks>Persisted and sent as a name or number like every enum here; append, never reorder.</remarks>
public enum WeaponFamily
{
    Laser = 0,
    Kinetic = 1,

    /// <summary>Weapons that focus a field or frequency: the Staff, the Focus, the Saber.</summary>
    Resonance = 2,

    /// <summary>Guided ship ordnance.</summary>
    Missile = 3,
}

/// <summary>Whether a weapon strikes up close or at range.</summary>
public enum WeaponType
{
    Ranged = 0,
    Melee = 1,
}

/// <summary>Whether a weapon is carried by a character or fitted to a ship.</summary>
public enum WeaponMount
{
    OnFoot = 0,
    Ship = 1,
}

/// <summary>
/// A weapon's skill, and what decides which other weapons share its XP (ADR-0015).
/// </summary>
/// <param name="SkillKey">The skill's content key, e.g. <c>laser_rifle</c>.</param>
public readonly record struct WeaponSkill(
    string SkillKey, WeaponFamily Family, WeaponType Type, WeaponMount Mount)
{
    /// <summary>
    /// Whether two weapons share XP: the same family, type and mount, and not the same weapon.
    /// </summary>
    /// <remarks>
    /// The mount is part of it because ship and on-foot weapons never share, even within a family: a
    /// Laser Rifle does not train a ship's beam (Joe, 15 October).
    /// </remarks>
    public bool IsSiblingOf(WeaponSkill other) =>
        !string.Equals(SkillKey, other.SkillKey, StringComparison.Ordinal)
        && Family == other.Family
        && Type == other.Type
        && Mount == other.Mount;
}

/// <summary>How XP earned with one weapon divides among its family (ADR-0015).</summary>
public static class WeaponXp
{
    /// <summary>The weapon used keeps this share when it has siblings; they divide the rest.</summary>
    public const int UsedPercent = 80;

    /// <summary>
    /// Divides XP earned with a weapon between it and its siblings.
    /// </summary>
    /// <param name="used">The weapon that did the damage.</param>
    /// <param name="xp">What the damage was worth. Never negative.</param>
    /// <param name="catalogue">Every weapon skill there is; the used one may be among them.</param>
    /// <returns>XP by skill key: the used weapon always, each sibling when there are any.</returns>
    /// <remarks>
    /// <para>
    /// <strong>80% to the weapon, 20% divided equally among its siblings</strong> (Joe, 15 October). With
    /// none, the weapon keeps all of it, so a fight is worth the same total however many weapons anyone
    /// owns: a collection is not a multiplier.
    /// </para>
    /// <para>
    /// <strong>Integer and conserving.</strong> Each sibling's share is floored and whatever rounding
    /// leaves goes to the weapon used, so the shares always sum to exactly what was earned.
    /// </para>
    /// </remarks>
    /// <exception cref="ArgumentOutOfRangeException">If XP is negative.</exception>
    public static IReadOnlyDictionary<string, long> Share(
        WeaponSkill used, long xp, IEnumerable<WeaponSkill> catalogue)
    {
        ArgumentNullException.ThrowIfNull(catalogue);
        ArgumentOutOfRangeException.ThrowIfNegative(xp);

        string[] siblings = [.. catalogue
            .Where(used.IsSiblingOf)
            .Select(s => s.SkillKey)
            .Distinct(StringComparer.Ordinal)
            .Order(StringComparer.Ordinal)];

        var shares = new Dictionary<string, long>(StringComparer.Ordinal);

        if (siblings.Length == 0)
        {
            shares[used.SkillKey] = xp;

            return shares;
        }

        // Int128 so a large award times a percentage cannot overflow before the divide.
        long pool = (long)((Int128)xp * (100 - UsedPercent) / 100);
        long each = pool / siblings.Length;

        foreach (string sibling in siblings)
        {
            shares[sibling] = each;
        }

        shares[used.SkillKey] = xp - (each * siblings.Length);

        return shares;
    }
}

/// <summary>When a weapon's skill points arrive (ADR-0015).</summary>
public static class WeaponTree
{
    /// <summary>Points in a weapon's trees. Twenty cannot fill both of its two trees.</summary>
    public const int PointCount = 20;

    /// <summary>The level of the first point.</summary>
    public const int FirstPointLevel = 4;

    /// <summary>
    /// The level of the last point, and of the last thing a weapon's level grants beyond cosmetics.
    /// </summary>
    /// <remarks>
    /// 70, not the 50 first written: on the ADR-0004 curve level 50 is 0.78% of the XP to 99, and 70 is
    /// 5.7%, so building fully into one weapon takes real time (Joe, 15 October).
    /// </remarks>
    public const int LastPointLevel = 70;

    /// <summary>
    /// The level at which a point arrives: evenly spaced from <see cref="FirstPointLevel"/> to
    /// <see cref="LastPointLevel"/>, about every three and a half levels, rounded to the nearest level.
    /// </summary>
    /// <param name="point">Which point, 1 to <see cref="PointCount"/>.</param>
    /// <exception cref="ArgumentOutOfRangeException">If there is no such point.</exception>
    public static int LevelOfPoint(int point)
    {
        ArgumentOutOfRangeException.ThrowIfLessThan(point, 1);
        ArgumentOutOfRangeException.ThrowIfGreaterThan(point, PointCount);

        // Exact in integers: (point - 1) * 66 / 19 rounded half up, without a double in sight.
        int span = LastPointLevel - FirstPointLevel;
        int steps = PointCount - 1;

        return FirstPointLevel + ((((point - 1) * span * 2) + steps) / (steps * 2));
    }

    /// <summary>How many points a weapon at this level has unlocked.</summary>
    public static int PointsUnlockedAt(int level)
    {
        int unlocked = 0;

        for (int point = 1; point <= PointCount && LevelOfPoint(point) <= level; point++)
        {
            unlocked++;
        }

        return unlocked;
    }
}
