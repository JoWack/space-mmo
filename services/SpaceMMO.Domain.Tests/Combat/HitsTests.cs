using SpaceMMO.Domain.Combat;
using Xunit;

namespace SpaceMMO.Domain.Tests.Combat;

/// <summary>What a hit does, and what it is worth (ADR-0017).</summary>
public sealed class HitsTests
{
    [Theory]
    [InlineData(HitZone.Head, HitFacing.Front, 150)]
    [InlineData(HitZone.Body, HitFacing.Front, 100)]
    [InlineData(HitZone.Legs, HitFacing.Front, 75)]
    [InlineData(HitZone.Body, HitFacing.Side, 100)]
    [InlineData(HitZone.Body, HitFacing.Back, 120)]
    [InlineData(HitZone.Head, HitFacing.Back, 180)]
    [InlineData(HitZone.Legs, HitFacing.Back, 90)]
    public void The_zones_and_facings_Joe_chose(HitZone zone, HitFacing facing, int expected) =>
        Assert.Equal(expected, Hits.Damage(100, zone, facing));

    [Fact]
    public void A_rifle_bolt_in_the_back_of_the_head_does_99()
    {
        // The tier-1 Laser Rifle's 55, times 1.5 and 1.2.
        Assert.Equal(99, Hits.Damage(55, HitZone.Head, HitFacing.Back));
        Assert.Equal(41, Hits.Damage(55, HitZone.Legs, HitFacing.Front));
    }

    [Fact]
    public void A_rifle_kills_in_about_nine_seconds_on_the_body()
    {
        // ADR-0017's 8-12 s fight: 55 every half second against level 1's 1,000.
        int bolts = (int)Math.Ceiling(1_000.0 / Hits.Damage(55, HitZone.Body, HitFacing.Front));
        double seconds = (bolts - 1) * 0.5;

        Assert.InRange(seconds, 8.0, 12.0);
    }

    [Theory]
    [InlineData(55, 12, 7)]
    [InlineData(100, 12, 12)]
    [InlineData(4, 12, 0)]
    [InlineData(5, 12, 1)]
    [InlineData(0, 12, 0)]
    public void XP_is_the_damage_times_the_rate_rounded(int damage, int rate, long expected) =>
        Assert.Equal(expected, CombatXp.ForDamage(damage, rate));

    [Theory]
    [InlineData(300, 100)]
    [InlineData(7, 2)]
    [InlineData(2, 0)]
    public void Constitution_takes_a_third_on_top(long combatXp, long expected) =>
        Assert.Equal(expected, CombatXp.ConstitutionShare(combatXp));
}
