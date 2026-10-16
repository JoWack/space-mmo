using SpaceMMO.Domain.Combat;
using SpaceMMO.Domain.Progression;
using Xunit;

namespace SpaceMMO.Domain.Tests.Combat;

/// <summary>
/// ADR-0015's rules: who shares a weapon's XP, how it divides, and when skill points arrive.
/// </summary>
public sealed class WeaponTests
{
    // The on-foot weapons Joe settled on 15 October, and the three ship weapons named so far.
    private static readonly WeaponSkill LaserRifle = new("laser_rifle", WeaponFamily.Laser, WeaponType.Ranged, WeaponMount.OnFoot);
    private static readonly WeaponSkill LaserPistol = new("laser_pistol", WeaponFamily.Laser, WeaponType.Ranged, WeaponMount.OnFoot);
    private static readonly WeaponSkill Cutter = new("cutter", WeaponFamily.Laser, WeaponType.Melee, WeaponMount.OnFoot);
    private static readonly WeaponSkill AssaultRifle = new("assault_rifle", WeaponFamily.Kinetic, WeaponType.Ranged, WeaponMount.OnFoot);
    private static readonly WeaponSkill Magnum = new("magnum", WeaponFamily.Kinetic, WeaponType.Ranged, WeaponMount.OnFoot);
    private static readonly WeaponSkill Daggers = new("daggers", WeaponFamily.Kinetic, WeaponType.Melee, WeaponMount.OnFoot);
    private static readonly WeaponSkill Staff = new("staff", WeaponFamily.Resonance, WeaponType.Ranged, WeaponMount.OnFoot);
    private static readonly WeaponSkill Focus = new("focus", WeaponFamily.Resonance, WeaponType.Ranged, WeaponMount.OnFoot);
    private static readonly WeaponSkill Saber = new("saber", WeaponFamily.Resonance, WeaponType.Melee, WeaponMount.OnFoot);
    private static readonly WeaponSkill ShipLaser = new("ship_laser", WeaponFamily.Laser, WeaponType.Ranged, WeaponMount.Ship);
    private static readonly WeaponSkill Railgun = new("railgun", WeaponFamily.Kinetic, WeaponType.Ranged, WeaponMount.Ship);
    private static readonly WeaponSkill Missiles = new("missiles", WeaponFamily.Missile, WeaponType.Ranged, WeaponMount.Ship);

    private static readonly WeaponSkill[] Catalogue =
    [
        LaserRifle, LaserPistol, Cutter, AssaultRifle, Magnum, Daggers, Staff, Focus, Saber, ShipLaser, Railgun, Missiles,
    ];

    // ── Sharing ──────────────────────────────────────────────────────────────

    [Fact]
    public void TheWeaponUsedKeepsEightyPercentAndItsSiblingTheRest()
    {
        // Joe's own example: the Laser Rifle, whose one Laser + Ranged sibling is the Laser Pistol.
        IReadOnlyDictionary<string, long> shares = WeaponXp.Share(LaserRifle, 1_000, Catalogue);

        Assert.Equal(2, shares.Count);
        Assert.Equal(800, shares["laser_rifle"]);
        Assert.Equal(200, shares["laser_pistol"]);
    }

    [Fact]
    public void TheHealerSharesWithTheStaff()
    {
        IReadOnlyDictionary<string, long> shares = WeaponXp.Share(Focus, 500, Catalogue);

        Assert.Equal(400, shares["focus"]);
        Assert.Equal(100, shares["staff"]);
    }

    [Fact]
    public void AWeaponWithNoSiblingKeepsEverything()
    {
        // The Cutter is alone in Laser melee, so nothing divides. Otherwise owning no siblings would lose a
        // fifth of every fight's XP.
        IReadOnlyDictionary<string, long> shares = WeaponXp.Share(Cutter, 1_000, Catalogue);

        Assert.Equal(1_000, Assert.Single(shares).Value);
    }

    [Fact]
    public void SeveralSiblingsDivideTheFifthBetweenThem_AndTheTotalNeverGrows()
    {
        // A third Kinetic gun, as the Scattergun would be: a collection is not a multiplier.
        WeaponSkill scattergun = new("scattergun", WeaponFamily.Kinetic, WeaponType.Ranged, WeaponMount.OnFoot);

        IReadOnlyDictionary<string, long> shares =
            WeaponXp.Share(AssaultRifle, 1_000, [.. Catalogue, scattergun]);

        Assert.Equal(800, shares["assault_rifle"]);
        Assert.Equal(100, shares["magnum"]);
        Assert.Equal(100, shares["scattergun"]);
        Assert.Equal(1_000, shares.Values.Sum());
    }

    [Theory]
    [InlineData(1L)]
    [InlineData(7L)]
    [InlineData(1_001L)]
    [InlineData(13_034_431L)]
    public void RoundingNeverCreatesOrLosesXp(long xp)
    {
        WeaponSkill third = new("third", WeaponFamily.Kinetic, WeaponType.Ranged, WeaponMount.OnFoot);

        IReadOnlyDictionary<string, long> shares = WeaponXp.Share(Magnum, xp, [.. Catalogue, third]);

        Assert.Equal(xp, shares.Values.Sum());
        Assert.True(shares["magnum"] >= shares["assault_rifle"]);
    }

    [Fact]
    public void ShipAndOnFootWeaponsNeverShare_EvenWithinAFamily()
    {
        Assert.False(LaserRifle.IsSiblingOf(ShipLaser));
        Assert.False(AssaultRifle.IsSiblingOf(Railgun));
        Assert.Equal(1_000, Assert.Single(WeaponXp.Share(ShipLaser, 1_000, Catalogue)).Value);
    }

    [Fact]
    public void MeleeAndRangedNeverShare_EvenWithinAFamily()
    {
        Assert.False(Saber.IsSiblingOf(Staff));
        Assert.False(Daggers.IsSiblingOf(Magnum));
        Assert.DoesNotContain("saber", WeaponXp.Share(Staff, 1_000, Catalogue).Keys);
    }

    [Fact]
    public void AWeaponIsNotItsOwnSibling() =>
        Assert.False(LaserRifle.IsSiblingOf(LaserRifle));

    [Fact]
    public void NegativeXpIsRefused() =>
        Assert.Throws<ArgumentOutOfRangeException>(() => WeaponXp.Share(LaserRifle, -1, Catalogue));

    // ── Points ───────────────────────────────────────────────────────────────

    [Fact]
    public void PointsArriveAtTheLevelsTheAdrLists()
    {
        // The list in ADR-0015, literally, so the rule and the record cannot drift apart.
        int[] expected = [4, 7, 11, 14, 18, 21, 25, 28, 32, 35, 39, 42, 46, 49, 53, 56, 60, 63, 67, 70];

        int[] actual = [.. Enumerable.Range(1, WeaponTree.PointCount).Select(WeaponTree.LevelOfPoint)];

        Assert.Equal(expected, actual);
    }

    [Theory]
    [InlineData(1, 0)]
    [InlineData(3, 0)]
    [InlineData(4, 1)]
    [InlineData(49, 14)]
    [InlineData(50, 14)]
    [InlineData(69, 19)]
    [InlineData(70, 20)]
    [InlineData(99, 20)]
    public void PointsUnlockedFollowTheLevel(int level, int points) =>
        Assert.Equal(points, WeaponTree.PointsUnlockedAt(level));

    [Fact]
    public void TheTreeCompletesAtAboutAFifteenthOfTheWayTo99()
    {
        // Why the cap moved from 50 to 70 (Joe, 15 October): 50 was 0.78% of the XP to 99, 70 is 5.7%.
        double share = (double)SkillCurve.XpForLevel(WeaponTree.LastPointLevel) / SkillCurve.XpForLevel(99);

        Assert.InRange(share, 0.05, 0.06);
    }
}
