using SpaceMMO.Domain.Gathering;
using SpaceMMO.Domain.Progression;
using Xunit;

namespace SpaceMMO.Domain.Tests.Gathering;

/// <summary>
/// Tests for gathering rates (design-bible §2).
/// </summary>
/// <remarks>
/// Raw material supply is what every downstream price rests on, so these rates are the most
/// economically load-bearing numbers in the game after the death table.
/// </remarks>
public sealed class GatheringYieldTests
{
    [Theory]
    [InlineData(1, 1)]
    [InlineData(24, 1)]
    [InlineData(25, 2)]
    [InlineData(50, 3)]
    [InlineData(75, 4)]
    [InlineData(99, 4)]
    public void UnitsPerTick_RisesWithLevel(int level, int expected)
    {
        Assert.Equal(expected, GatheringYield.UnitsPerTick(level));
    }

    [Fact]
    public void UnitsPerTick_NeverDecreases()
    {
        int previous = 0;

        for (int level = SkillCurve.MinLevel; level <= SkillCurve.MaxLevel; level++)
        {
            int units = GatheringYield.UnitsPerTick(level);

            Assert.True(units >= previous, $"Yield fell at level {level}.");
            previous = units;
        }
    }

    [Fact]
    public void UnitsPerTick_TopsOutAtFourTimesTheStartingRate()
    {
        // Enough that levelling is worth pursuing, bounded enough that a maxed gatherer does not
        // flood the market relative to a beginner.
        Assert.Equal(1, GatheringYield.UnitsPerTick(SkillCurve.MinLevel));
        Assert.Equal(4, GatheringYield.UnitsPerTick(SkillCurve.MaxLevel));
    }

    [Theory]
    [InlineData(0)]
    [InlineData(100)]
    public void UnitsPerTick_OutsideTheLevelRange_Throws(int level)
    {
        Assert.Throws<ArgumentOutOfRangeException>(() => GatheringYield.UnitsPerTick(level));
    }

    // ── Rate limiting ────────────────────────────────────────────────────────

    [Theory]
    [InlineData(0, 0)]
    [InlineData(2, 0)]      // under one tick
    [InlineData(3, 1)]
    [InlineData(29, 9)]
    [InlineData(30, 10)]
    public void EntitledTicks_IsElapsedTimeDividedByTheTickInterval(long elapsed, int expected)
    {
        Assert.Equal(expected, GatheringYield.EntitledTicks(elapsed));
    }

    [Fact]
    public void EntitledTicks_IsCappedSoIdleTimeCannotBeBanked()
    {
        // Gathering is an active verb, unlike industry jobs. Without a cap, a client could idle
        // for an hour and return to claim extraction it never performed.
        Assert.Equal(GatheringYield.MaxBankedTicks, GatheringYield.EntitledTicks(3_600));
        Assert.Equal(GatheringYield.MaxBankedTicks, GatheringYield.EntitledTicks(86_400));
    }

    [Fact]
    public void EntitledTicks_WithNegativeElapsedTime_Throws()
    {
        Assert.Throws<ArgumentOutOfRangeException>(() => GatheringYield.EntitledTicks(-1));
    }

    // ── Combined ─────────────────────────────────────────────────────────────

    [Fact]
    public void UnitsAvailable_CombinesTimeSkillAndNodeContents()
    {
        // 30 seconds is 10 ticks; a level-50 gatherer takes 3 per tick.
        Assert.Equal(30, GatheringYield.UnitsAvailable(30, skillLevel: 50, nodeRemaining: 1_000));
    }

    [Fact]
    public void UnitsAvailable_IsCappedByWhatTheNodeHolds()
    {
        // The property that stops gathering creating material from nothing.
        Assert.Equal(7, GatheringYield.UnitsAvailable(3_600, skillLevel: 99, nodeRemaining: 7));
    }

    [Fact]
    public void UnitsAvailable_WithTooLittleTime_IsZero()
    {
        Assert.Equal(0, GatheringYield.UnitsAvailable(2, skillLevel: 99, nodeRemaining: 1_000));
    }

    [Fact]
    public void UnitsAvailable_FromAnEmptyNode_IsZero()
    {
        Assert.Equal(0, GatheringYield.UnitsAvailable(3_600, skillLevel: 99, nodeRemaining: 0));
    }

    [Fact]
    public void UnitsAvailable_NeverExceedsTheNodeOrTheEntitlement()
    {
        // Swept, because either bound failing is a material faucet.
        foreach (long elapsed in new[] { 0L, 1L, 3L, 60L, 3_600L })
        {
            foreach (int level in new[] { 1, 25, 50, 99 })
            {
                foreach (int remaining in new[] { 0, 1, 5, 10_000 })
                {
                    int units = GatheringYield.UnitsAvailable(elapsed, level, remaining);

                    Assert.InRange(units, 0, remaining);

                    int ceiling = GatheringYield.EntitledTicks(elapsed)
                        * GatheringYield.UnitsPerTick(level);

                    Assert.True(units <= ceiling, $"Extracted {units} against a ceiling of {ceiling}.");
                }
            }
        }
    }

    [Fact]
    public void UnitsAvailable_WithNegativeRemaining_Throws()
    {
        Assert.Throws<ArgumentOutOfRangeException>(
            () => GatheringYield.UnitsAvailable(30, 50, nodeRemaining: -1));
    }

    [Fact]
    public void XpPerUnit_IsPerUnitSoLevellingDoesNotAccelerateItself()
    {
        // Paying per tick instead would mean a faster gatherer earns more XP for the same
        // material, compounding the advantage.
        Assert.True(GatheringYield.XpPerUnit > 0);
    }
}
