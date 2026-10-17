namespace SpaceMMO.Domain.Combat;

/// <summary>Where on a body a hit landed (192, Joe 16 October).</summary>
/// <remarks>Crosses the wire from the game server as a number, and is stored as a name: append, never reorder.</remarks>
public enum HitZone
{
    Head = 0,
    Body = 1,
    Legs = 2,
}

/// <summary>Which way the target was facing the shot: from in front, from the side, or from behind.</summary>
public enum HitFacing
{
    Front = 0,
    Side = 1,
    Back = 2,
}

/// <summary>What a hit does before armour (ADR-0017).</summary>
/// <remarks>
/// <strong>Head ×1.5, body ×1, legs ×0.75; ×1.2 from behind, multiplying</strong> (Joe, 16 October), so a shot in
/// the back of the head does ×1.8. Aiming is rewarded without being all-or-nothing, and being caught from
/// behind matters. Armour, when it exists, protects its own zone after this (179).
/// </remarks>
public static class Hits
{
    /// <summary>A zone's multiplier, in percent.</summary>
    public static int ZonePercent(HitZone zone) => zone switch
    {
        HitZone.Head => 150,
        HitZone.Body => 100,
        HitZone.Legs => 75,
        _ => throw new ArgumentOutOfRangeException(nameof(zone), zone, "Unknown hit zone."),
    };

    /// <summary>A facing's multiplier, in percent.</summary>
    public static int FacingPercent(HitFacing facing) => facing switch
    {
        HitFacing.Front => 100,
        HitFacing.Side => 100,
        HitFacing.Back => 120,
        _ => throw new ArgumentOutOfRangeException(nameof(facing), facing, "Unknown hit facing."),
    };

    /// <summary>A hit's damage: the weapon's, times its zone's and its facing's multipliers, rounded half up.</summary>
    /// <exception cref="ArgumentOutOfRangeException">If the weapon's damage is negative.</exception>
    public static int Damage(int weaponDamage, HitZone zone, HitFacing facing)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(weaponDamage);

        long scaled = (long)weaponDamage * ZonePercent(zone) * FacingPercent(facing);

        return (int)((scaled + 5_000) / 10_000);
    }
}

/// <summary>What fighting is worth (ADR-0015, ADR-0017).</summary>
public static class CombatXp
{
    /// <summary>The skill that earns a share of every combat XP award: maximum health.</summary>
    public const string ConstitutionSkillKey = "constitution";

    /// <summary>Constitution earns this fraction of every combat XP award, on top (Joe, 16 October): a third.</summary>
    public const int ConstitutionDivisor = 3;

    /// <summary>
    /// XP for a hit: its damage times the weapon skill's rate, authored per hundred points of damage, rounded
    /// half up.
    /// </summary>
    /// <remarks>
    /// The rate is content. A weapon reaches level 70 in about 50 hours of fighting with it (Joe, 16 October),
    /// which on ADR-0017's estimate is about 12 XP per hundred points; it is set against what the first weapon
    /// measures.
    /// </remarks>
    /// <exception cref="ArgumentOutOfRangeException">If either is negative.</exception>
    public static long ForDamage(int damage, int xpPerHundredDamage)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(damage);
        ArgumentOutOfRangeException.ThrowIfNegative(xpPerHundredDamage);

        return ((long)damage * xpPerHundredDamage + 50) / 100;
    }

    /// <summary>Constitution's share of a combat XP award: a third of it, on top of the weapon's.</summary>
    public static long ConstitutionShare(long combatXp)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(combatXp);

        return combatXp / ConstitutionDivisor;
    }
}
