using SpaceMMO.Domain.Economy;
using SpaceMMO.Domain.Industry;
using Xunit;

namespace SpaceMMO.Domain.Tests.Industry;

/// <summary>
/// Tests for industry job fees (economy-design §3).
/// </summary>
public sealed class IndustryFeesTests
{
    private static Credits Cr(long whole) => Credits.FromWholeCredits(whole);

    [Theory]
    [InlineData(1, 15)]    // 10 base + 5
    [InlineData(2, 20)]
    [InlineData(5, 35)]
    [InlineData(100, 510)]
    public void ForJob_ChargesBasePlusPerRun(int runs, long expected)
    {
        Assert.Equal(Cr(expected), IndustryFees.ForJob(runs));
    }

    [Fact]
    public void ForJob_ScalesWithRuns_SoTheSinkTracksProductionVolume()
    {
        // The per-run component is what ties this sink to actual output. If the fee were purely
        // flat, mass manufacturing would contribute no more than tinkering.
        Assert.True(IndustryFees.ForJob(10) > IndustryFees.ForJob(1));
        Assert.True(IndustryFees.ForJob(100) > IndustryFees.ForJob(10));
    }

    [Fact]
    public void ForJob_IsAlwaysPositive_SoStartingAJobIsNeverFree()
    {
        // A free start would make start-and-cancel cost nothing at all, which is the loophole the
        // fee exists to close.
        foreach (int runs in new[] { 1, 2, 7, 1_000 })
        {
            Assert.True(IndustryFees.ForJob(runs).IsPositive);
        }
    }

    [Fact]
    public void ForJob_WithExplicitRates_UsesThem()
    {
        Assert.Equal(
            Cr(1_100), IndustryFees.ForJob(10, baseFee: Cr(100), perRunFee: Cr(100)));
    }

    [Fact]
    public void ForJob_WithZeroRates_IsZero()
    {
        Assert.True(
            IndustryFees.ForJob(10, Credits.Zero, Credits.Zero).IsZero);
    }

    [Fact]
    public void ForJob_MatchesTheDocumentedDefaults()
    {
        Assert.Equal(Cr(10), IndustryFees.DefaultBaseFee);
        Assert.Equal(Cr(5), IndustryFees.DefaultPerRunFee);
    }

    [Theory]
    [InlineData(0)]
    [InlineData(-1)]
    public void ForJob_WithNonPositiveRuns_Throws(int runs)
    {
        Assert.Throws<ArgumentOutOfRangeException>(() => IndustryFees.ForJob(runs));
    }

    [Fact]
    public void ForJob_WithNegativeRates_Throws()
    {
        Credits negative = Credits.FromMinorUnits(-1);

        Assert.Throws<ArgumentOutOfRangeException>(
            () => IndustryFees.ForJob(1, negative, Cr(5)));

        Assert.Throws<ArgumentOutOfRangeException>(
            () => IndustryFees.ForJob(1, Cr(10), negative));
    }
}
