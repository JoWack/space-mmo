using SpaceMMO.Domain.Universe;
using Xunit;

namespace SpaceMMO.Domain.Tests.Items;

/// <summary>
/// Ships arrive at the places that are for ships (ADR-0012).
/// </summary>
/// <remarks>
/// A rule about kinds rather than a flag on the row. A boolean per station would let two spaceports
/// disagree about whether they are spaceports, and the first one authored without it is a player
/// standing at a shipyard that will not give them their ship.
/// </remarks>
public sealed class StationKindSummoningTests
{
    [Theory]
    [InlineData(StationKind.Spaceport)]
    [InlineData(StationKind.Capital)]
    public void Ships_are_summoned_where_ships_are_handled(StationKind kind)
    {
        // Read off what the kinds already document: a spaceport is "ship docking, refitting, and
        // industry facilities", and the capital is "everything".
        Assert.True(kind.AllowsShipSummoning());
    }

    [Theory]
    [InlineData(StationKind.TradingHub)]
    [InlineData(StationKind.Housing)]
    [InlineData(StationKind.Social)]
    public void And_not_at_a_market_a_house_or_a_bar(StationKind kind)
    {
        Assert.False(kind.AllowsShipSummoning());
    }

    [Fact]
    public void Every_kind_has_an_answer()
    {
        // A new station kind should be a deliberate decision about whether ships come to it, not a
        // default that falls out of a switch. This does not fail when one is added — nothing can —
        // but it does mean the count is written down where somebody adding the sixth will read it.
        Assert.Equal(5, Enum.GetValues<StationKind>().Length);
    }
}
