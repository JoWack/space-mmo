using SpaceMMO.Domain.Combat;
using Xunit;

namespace SpaceMMO.Domain.Tests.Combat;

/// <summary>The stamina pool's size, and what spending it is worth (ADR-0017, tasks 101 and 195).</summary>
public sealed class StaminaPoolTests
{
    [Theory]
    [InlineData(1, 100)]
    [InlineData(2, 101)]
    [InlineData(50, 125)]
    [InlineData(70, 135)]
    [InlineData(99, 150)]
    public void The_pool_grows_by_half_from_level_1_to_99(int level, int expected) =>
        Assert.Equal(expected, StaminaPool.Max(level));

    [Theory]
    [InlineData(0, 100)]
    [InlineData(-5, 100)]
    [InlineData(120, 150)]
    public void A_level_outside_the_curve_is_held_to_it(int level, int expected) =>
        Assert.Equal(expected, StaminaPool.Max(level));

    [Fact]
    public void Every_level_is_at_least_as_big_as_the_one_before()
    {
        for (int level = 2; level <= 99; level++)
        {
            Assert.True(StaminaPool.Max(level) >= StaminaPool.Max(level - 1), $"level {level} shrank the pool");
        }
    }

    [Theory]
    [InlineData(0, 100, 0)]
    [InlineData(250, 100, 250)]
    [InlineData(250, 40, 100)]
    [InlineData(1, 40, 0)]
    [InlineData(2, 25, 1)]
    public void Spending_earns_the_rate_per_hundred_rounded_half_up(int spent, int rate, long expected) =>
        Assert.Equal(expected, StaminaPool.XpForSpent(spent, rate));

    [Fact]
    public void Nothing_negative_is_spent_or_earned()
    {
        Assert.Throws<ArgumentOutOfRangeException>(() => StaminaPool.XpForSpent(-1, 100));
        Assert.Throws<ArgumentOutOfRangeException>(() => StaminaPool.XpForSpent(100, -1));
    }
}
