using SpaceMMO.Domain.Characters;
using Xunit;

namespace SpaceMMO.Domain.Tests.Characters;

/// <summary>
/// Tests for race-derived facts (design-bible §1).
/// </summary>
/// <remarks>
/// Faction and home body are computed from race rather than stored, so these are the only
/// place the mapping exists. A wrong answer here puts a character in the wrong faction with
/// no stored value to contradict it.
/// </remarks>
public sealed class RacesTests
{
    private static readonly Race[] AllRaces = Enum.GetValues<Race>();

    [Theory]
    [InlineData(Race.Humanoid, Faction.A)]
    [InlineData(Race.Martian, Faction.A)]
    [InlineData(Race.SpaceElf, Faction.B)]
    [InlineData(Race.SpaceOrc, Faction.B)]
    public void FactionFor_MatchesTheDesignedAlignment(Race race, Faction expected)
    {
        Assert.Equal(expected, Races.FactionFor(race));
    }

    [Theory]
    [InlineData(Race.Humanoid, "body_terra")]
    [InlineData(Race.Martian, "body_ares")]
    [InlineData(Race.SpaceElf, "body_verdance")]
    [InlineData(Race.SpaceOrc, "body_grimhold")]
    public void HomeBodyKeyFor_MatchesTheStartingPlanets(Race race, string expected)
    {
        Assert.Equal(expected, Races.HomeBodyKeyFor(race));
    }

    [Fact]
    public void EveryRace_HasAFactionAndAHomeBody()
    {
        // Guards against a race being added without wiring up its derived facts, which would
        // throw at character creation rather than at build time.
        foreach (Race race in AllRaces)
        {
            Assert.InRange(Races.FactionFor(race), Faction.A, Faction.B);
            Assert.False(string.IsNullOrWhiteSpace(Races.HomeBodyKeyFor(race)));
        }
    }

    [Fact]
    public void EveryRace_HasADistinctHomeBody()
    {
        // Two races sharing a starting planet would give one of them an economic head start
        // on the other's resource nodes.
        var homeBodies = AllRaces.Select(Races.HomeBodyKeyFor).ToList();

        Assert.Equal(homeBodies.Count, homeBodies.Distinct().Count());
    }

    [Fact]
    public void BothFactions_HaveExactlyTwoRaces()
    {
        // The design is deliberately symmetric: neither faction should outnumber the other.
        Assert.Equal(2, AllRaces.Count(r => Races.FactionFor(r) == Faction.A));
        Assert.Equal(2, AllRaces.Count(r => Races.FactionFor(r) == Faction.B));
    }

    [Fact]
    public void UndefinedRace_Throws()
    {
        Assert.Throws<ArgumentOutOfRangeException>(() => Races.FactionFor((Race)99));
        Assert.Throws<ArgumentOutOfRangeException>(() => Races.HomeBodyKeyFor((Race)99));
    }
}
