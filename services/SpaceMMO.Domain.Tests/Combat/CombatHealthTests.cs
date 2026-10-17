using SpaceMMO.Domain.Combat;
using Xunit;

namespace SpaceMMO.Domain.Tests.Combat;

/// <summary>Health, how big it is and how it comes back (ADR-0017).</summary>
public sealed class CombatHealthTests
{
    private static readonly DateTimeOffset Fight = new(2026, 10, 16, 12, 0, 0, TimeSpan.Zero);

    [Theory]
    [InlineData(1, 1_000)]
    [InlineData(50, 1_500)]
    [InlineData(70, 1_704)]
    [InlineData(99, 2_000)]
    public void Health_doubles_from_level_1_to_99(int level, int expected) =>
        Assert.Equal(expected, CombatHealth.Max(level));

    [Theory]
    [InlineData(0, 1_000)]
    [InlineData(-5, 1_000)]
    [InlineData(120, 2_000)]
    public void A_level_outside_the_curve_is_held_to_it(int level, int expected) =>
        Assert.Equal(expected, CombatHealth.Max(level));

    [Fact]
    public void Somebody_never_hurt_is_at_full_health() =>
        Assert.Equal(1_200, CombatHealth.Current(null, 1_200, null, Fight));

    [Fact]
    public void Nothing_comes_back_for_ten_seconds()
    {
        Assert.Equal(400, CombatHealth.Current(400, 1_000, Fight, Fight.AddSeconds(5)));
        Assert.Equal(400, CombatHealth.Current(400, 1_000, Fight, Fight.AddSeconds(10)));
    }

    [Fact]
    public void Then_it_comes_back_at_five_percent_a_second()
    {
        // 5% of 1,000 is 50 a second: four seconds after the wait, 200 back.
        Assert.Equal(600, CombatHealth.Current(400, 1_000, Fight, Fight.AddSeconds(14)));
        Assert.Equal(425, CombatHealth.Current(400, 1_000, Fight, Fight.AddSeconds(10.5)));
    }

    [Fact]
    public void And_stops_at_the_maximum()
    {
        // From nothing to full in 20 s, as Joe chose: here from 400, full well before a minute.
        Assert.Equal(1_000, CombatHealth.Current(400, 1_000, Fight, Fight.AddSeconds(60)));
    }

    [Fact]
    public void The_dead_do_not_regenerate() =>
        Assert.Equal(0, CombatHealth.Current(0, 1_000, Fight, Fight.AddHours(1)));

    [Fact]
    public void A_maximum_that_fell_caps_what_was_stored() =>
        Assert.Equal(1_000, CombatHealth.Current(1_500, 1_000, Fight, Fight));
}
