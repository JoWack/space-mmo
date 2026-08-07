using SpaceMMO.Domain.Characters;
using SpaceMMO.Domain.Combat;
using SpaceMMO.Domain.Universe;
using Xunit;

namespace SpaceMMO.Domain.Tests.Combat;

/// <summary>
/// Tests for PvP legality (ADR-0008).
/// </summary>
/// <remarks>
/// The rule is asymmetric on purpose, so most of these check that it is asymmetric in the
/// right direction. A rule that let the intruder shoot first and the locals only answer would
/// pass every count-based test and invert the entire design.
/// </remarks>
public sealed class PvpTests
{
    private static readonly DateTimeOffset Now = new(2026, 8, 6, 12, 0, 0, TimeSpan.Zero);

    private static readonly FactionSpace Space = new()
    {
        SafeRadiusKilometres = 500.0,
        ContestedRadiusKilometres = 2_000.0,
        FactionADirection = (1.0, 0.0, 0.0),
        RetaliationWindow = TimeSpan.FromMinutes(5),
    };

    /// <summary>A point the given distance out into a faction's half.</summary>
    private static SystemPosition In(Faction faction, double kilometres) =>
        new(faction == Faction.A ? kilometres : -kilometres, 0.0, 0.0);

    private static AttackAttempt Attack(
        Faction attacker,
        Faction target,
        SystemPosition position,
        bool targetIsProtected = false,
        DateTimeOffset? targetAttackedAt = null) => new()
        {
            AttackerFaction = attacker,
            TargetFaction = target,
            Position = position,
            TargetIsProtected = targetIsProtected,
            TargetAttackedAttackerAt = targetAttackedAt,
            Now = Now,
        };

    [Theory]
    [InlineData(0.0, PvpZone.Anchorage)]
    [InlineData(499.0, PvpZone.Anchorage)]
    [InlineData(500.0, PvpZone.Anchorage)]
    [InlineData(501.0, PvpZone.Approach)]
    [InlineData(2_000.0, PvpZone.Approach)]
    [InlineData(2_001.0, PvpZone.FactionSpace)]
    public void ZonesAreConcentricRingsAroundTheCapital(double kilometres, PvpZone expected) =>
        Assert.Equal(expected, Space.ZoneAt(In(Faction.A, kilometres)));

    [Fact]
    public void TheDivideSplitsTheWholeSystem()
    {
        Assert.Equal(Faction.A, Space.TerritoryAt(new SystemPosition(5_000.0, 0.0, 0.0)));
        Assert.Equal(Faction.B, Space.TerritoryAt(new SystemPosition(-5_000.0, 0.0, 0.0)));

        // Including the regions around the capital. Those override ownership rather than
        // escaping it, which is what makes the contested ring hot on both sides.
        Assert.Equal(Faction.A, Space.TerritoryAt(new SystemPosition(100.0, 0.0, 0.0)));
        Assert.Equal(Faction.B, Space.TerritoryAt(new SystemPosition(-100.0, 0.0, 0.0)));

        // A knife edge has to belong to somebody or the rule flickers for anyone sitting on it.
        Assert.Equal(Faction.A, Space.TerritoryAt(SystemPosition.Origin));
    }

    [Fact]
    public void FriendlyFireIsNeverLegal()
    {
        foreach (double distance in new[] { 100.0, 1_000.0, 10_000.0 })
        {
            Assert.False(Pvp.CanAttack(
                Space, Attack(Faction.A, Faction.A, In(Faction.A, distance))));

            // Not even under provocation. Faction comes from race and cannot be changed, so
            // there is no story in which this becomes allowed.
            Assert.False(Pvp.CanAttack(Space, Attack(
                Faction.B, Faction.B, In(Faction.B, distance),
                targetAttackedAt: Now - TimeSpan.FromSeconds(10))));
        }
    }

    [Fact]
    public void TheAnchorageIsSafeFromEverything()
    {
        Assert.False(Pvp.CanAttack(Space, Attack(Faction.A, Faction.B, In(Faction.A, 100.0))));
        Assert.False(Pvp.CanAttack(Space, Attack(Faction.B, Faction.A, In(Faction.B, 100.0))));

        // Even from somebody who was just shot. Chasing an attacker to the docks and finishing
        // it there would make the one safe place in the system safe only until somebody arrived
        // angry.
        Assert.False(Pvp.CanAttack(Space, Attack(
            Faction.A, Faction.B, In(Faction.A, 100.0),
            targetAttackedAt: Now - TimeSpan.FromSeconds(30))));
    }

    [Fact]
    public void TheContestedApproachIsHotForBothSides()
    {
        // No provocation, no locals, no protection: this is the risk attached to the only
        // global market, and it applies on both sides of the divide.
        Assert.True(Pvp.CanAttack(Space, Attack(Faction.A, Faction.B, In(Faction.A, 1_000.0))));
        Assert.True(Pvp.CanAttack(Space, Attack(Faction.A, Faction.B, In(Faction.B, 1_000.0))));
        Assert.True(Pvp.CanAttack(Space, Attack(Faction.B, Faction.A, In(Faction.A, 1_000.0))));
        Assert.True(Pvp.CanAttack(Space, Attack(Faction.B, Faction.A, In(Faction.B, 1_000.0))));
    }

    [Fact]
    public void LocalsMayEngageAnIntruderAndTheIntruderMayNotFireFirst()
    {
        SystemPosition deepInB = In(Faction.B, 10_000.0);

        // B is home and sees a foreign ship: legal on sight.
        Assert.True(Pvp.CanAttack(Space, Attack(Faction.B, Faction.A, deepInB)));

        // A is the intruder and has not been shot at: not legal. This is the direction that
        // matters — an implementation that had these the wrong way round would make raiding a
        // hunt and defending impossible, and would still pass any test that only counted how
        // many attacks were allowed.
        Assert.False(Pvp.CanAttack(Space, Attack(Faction.A, Faction.B, deepInB)));

        // And the mirror image, so the rule is not accidentally written around one faction.
        SystemPosition deepInA = In(Faction.A, 10_000.0);

        Assert.True(Pvp.CanAttack(Space, Attack(Faction.A, Faction.B, deepInA)));
        Assert.False(Pvp.CanAttack(Space, Attack(Faction.B, Faction.A, deepInA)));
    }

    [Fact]
    public void AnIntruderMayAnswerWhoeverShotThem()
    {
        SystemPosition deepInB = In(Faction.B, 10_000.0);

        Assert.True(Pvp.CanAttack(Space, Attack(
            Faction.A, Faction.B, deepInB,
            targetAttackedAt: Now - TimeSpan.FromMinutes(1))));
    }

    [Fact]
    public void RetaliationExpires()
    {
        SystemPosition deepInB = In(Faction.B, 10_000.0);

        Assert.True(Pvp.CanAttack(Space, Attack(
            Faction.A, Faction.B, deepInB,
            targetAttackedAt: Now - TimeSpan.FromMinutes(5))));

        Assert.False(Pvp.CanAttack(Space, Attack(
            Faction.A, Faction.B, deepInB,
            targetAttackedAt: Now - TimeSpan.FromMinutes(5, 1))));
    }

    [Fact]
    public void AnAttackFromTheFutureIsNotProvocation()
    {
        SystemPosition deepInB = In(Faction.B, 10_000.0);

        // A clock problem must not open a permanent licence to shoot somebody, which is what a
        // naive "elapsed <= window" would do for any timestamp ahead of now.
        Assert.False(Pvp.CanAttack(Space, Attack(
            Faction.A, Faction.B, deepInB,
            targetAttackedAt: Now + TimeSpan.FromHours(1))));
    }

    [Fact]
    public void NewPlayersAreSafeUntilTheyShootSomebody()
    {
        // The last leg of the onboarding chain crosses the contested ring in the ship the
        // player just built, which is the run people quit over losing.
        Assert.False(Pvp.CanAttack(Space, Attack(
            Faction.A, Faction.B, In(Faction.A, 1_000.0), targetIsProtected: true)));

        Assert.False(Pvp.CanAttack(Space, Attack(
            Faction.B, Faction.A, In(Faction.B, 10_000.0), targetIsProtected: true)));

        // But protection is a shield against being ganked, not a licence to shoot from behind.
        // Whoever they hit may hit back.
        Assert.True(Pvp.CanAttack(Space, Attack(
            Faction.A, Faction.B, In(Faction.A, 1_000.0),
            targetIsProtected: true,
            targetAttackedAt: Now - TimeSpan.FromSeconds(30))));
    }

    [Fact]
    public void TheDivideCanPointAnywhere()
    {
        // The plane is content, so nothing may assume it is the X axis. A system laid out along
        // Y must behave identically.
        var rotated = Space with { FactionADirection = (0.0, 1.0, 0.0) };

        Assert.Equal(Faction.A, rotated.TerritoryAt(new SystemPosition(0.0, 5_000.0, 0.0)));
        Assert.Equal(Faction.B, rotated.TerritoryAt(new SystemPosition(0.0, -5_000.0, 0.0)));

        Assert.True(Pvp.CanAttack(rotated, new AttackAttempt
        {
            AttackerFaction = Faction.B,
            TargetFaction = Faction.A,
            Position = new SystemPosition(0.0, -10_000.0, 0.0),
            Now = Now,
        }));

        Assert.False(Pvp.CanAttack(rotated, new AttackAttempt
        {
            AttackerFaction = Faction.A,
            TargetFaction = Faction.B,
            Position = new SystemPosition(0.0, -10_000.0, 0.0),
            Now = Now,
        }));
    }

    [Fact]
    public void TheCapitalNeedNotBeAtTheOrigin()
    {
        // Nothing should assume the capital sits at (0,0,0) just because it does today.
        var offset = Space with { Capital = new SystemPosition(9_000.0, -400.0, 250.0) };

        Assert.Equal(PvpZone.Anchorage, offset.ZoneAt(offset.Capital));

        Assert.Equal(
            PvpZone.Approach,
            offset.ZoneAt(new SystemPosition(9_000.0 + 1_000.0, -400.0, 250.0)));

        Assert.Equal(
            PvpZone.FactionSpace,
            offset.ZoneAt(new SystemPosition(9_000.0 + 5_000.0, -400.0, 250.0)));
    }
}
