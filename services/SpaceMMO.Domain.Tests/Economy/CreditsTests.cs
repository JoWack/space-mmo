using SpaceMMO.Domain.Economy;
using Xunit;

namespace SpaceMMO.Domain.Tests.Economy;

/// <summary>
/// Tests for the currency value type (ADR-0005).
/// </summary>
/// <remarks>
/// These guard the properties that make money handling safe: exact integer arithmetic,
/// explicit rounding direction, and no silent overflow. A failure here is a potential
/// currency exploit, not a cosmetic bug.
/// </remarks>
public sealed class CreditsTests
{
    [Fact]
    public void FromWholeCredits_ScalesByMinorUnitFactor()
    {
        Assert.Equal(350_000L, Credits.FromWholeCredits(3_500).MinorUnits);
    }

    [Fact]
    public void Zero_IsZeroAndNeitherPositiveNorNegative()
    {
        Assert.True(Credits.Zero.IsZero);
        Assert.False(Credits.Zero.IsPositive);
        Assert.False(Credits.Zero.IsNegative);
    }

    [Fact]
    public void FromWholeCredits_Overflowing_Throws()
    {
        Assert.Throws<OverflowException>(() => Credits.FromWholeCredits(long.MaxValue));
    }

    // ── Rounding direction ───────────────────────────────────────────────────

    [Theory]
    [InlineData(350_000L, 600, 21_000L)]    // 6% of 3,500 cr = exactly 210 cr
    [InlineData(350_000L, 4_000, 140_000L)] // 40% of 3,500 cr = exactly 1,400 cr
    [InlineData(100L, 10_000, 100L)]        // 100% is identity
    [InlineData(100L, 0, 0L)]               // 0% is zero
    public void Percent_WhenExact_BothRoundingModesAgree(long minorUnits, int bp, long expected)
    {
        var amount = Credits.FromMinorUnits(minorUnits);

        Assert.Equal(expected, amount.PercentRoundedUp(bp).MinorUnits);
        Assert.Equal(expected, amount.PercentRoundedDown(bp).MinorUnits);
    }

    [Fact]
    public void PercentRoundedUp_RoundsAwayFromZero_SoFeesAreNeverFree()
    {
        // 6% of one minor unit is 0.06. A fee that rounds to zero is a free option.
        Assert.Equal(1L, Credits.FromMinorUnits(1L).PercentRoundedUp(600).MinorUnits);
    }

    [Fact]
    public void PercentRoundedDown_RoundsTowardZero_SoPayoutsNeverOverpay()
    {
        // 40% of one minor unit is 0.4, and a payout must never round up into a faucet.
        Assert.Equal(0L, Credits.FromMinorUnits(1L).PercentRoundedDown(4_000).MinorUnits);
    }

    [Fact]
    public void Percent_RoundingDifference_IsNeverMoreThanOneMinorUnit()
    {
        // The whole point of choosing a direction is that rounding is a bounded sink
        // rather than an exploitable edge. Bound it explicitly.
        foreach (long minorUnits in new[] { 1L, 7L, 99L, 333L, 12_345L, 999_999L })
        {
            foreach (int bp in new[] { 1, 600, 1_200, 2_000, 4_000, 6_000, 8_000, 9_999 })
            {
                var amount = Credits.FromMinorUnits(minorUnits);
                long difference =
                    amount.PercentRoundedUp(bp).MinorUnits - amount.PercentRoundedDown(bp).MinorUnits;

                Assert.InRange(difference, 0L, 1L);
            }
        }
    }

    [Fact]
    public void Percent_AtLargeBalances_DoesNotOverflow()
    {
        // 10^15 minor units is 10 trillion credits — reachable in a late-game economy.
        // Scaling that by 10,000 basis points overflows int64, which is exactly why the
        // implementation widens to Int128 internally.
        var huge = Credits.FromMinorUnits(1_000_000_000_000_000L);

        Assert.Equal(huge.MinorUnits, huge.PercentRoundedDown(10_000).MinorUnits);
        Assert.Equal(huge.MinorUnits / 2, huge.PercentRoundedDown(5_000).MinorUnits);
    }

    [Fact]
    public void Percent_WithNegativeRate_Throws()
    {
        Assert.Throws<ArgumentOutOfRangeException>(
            () => Credits.FromMinorUnits(100L).PercentRoundedUp(-1));
    }

    [Fact]
    public void Percent_OnNegativeAmount_Throws()
    {
        // "Round up" has no defensible meaning on a debit — it would favour the player.
        // Callers must handle sign explicitly so the intent is visible at the call site.
        var debit = Credits.FromMinorUnits(-100L);

        Assert.Throws<InvalidOperationException>(() => debit.PercentRoundedUp(600));
        Assert.Throws<InvalidOperationException>(() => debit.PercentRoundedDown(600));
    }

    // ── Arithmetic ───────────────────────────────────────────────────────────

    [Fact]
    public void Addition_And_Subtraction_AreExact()
    {
        var a = Credits.FromWholeCredits(1_000);
        var b = Credits.FromWholeCredits(250);

        Assert.Equal(Credits.FromWholeCredits(1_250), a + b);
        Assert.Equal(Credits.FromWholeCredits(750), a - b);
        Assert.Equal(Credits.FromWholeCredits(-1_000), -a);
    }

    [Fact]
    public void Subtraction_CanGoNegative_ForLedgerDeltas()
    {
        // Balances should never be negative, but ledger deltas routinely are.
        Credits result = Credits.FromWholeCredits(100) - Credits.FromWholeCredits(250);

        Assert.True(result.IsNegative);
        Assert.Equal(-15_000L, result.MinorUnits);
    }

    [Fact]
    public void Addition_Summing_IsAssociative()
    {
        // The property floating point would break, and the reason this type exists.
        var amounts = new[]
        {
            Credits.FromMinorUnits(1L),
            Credits.FromMinorUnits(7L),
            Credits.FromMinorUnits(333L),
            Credits.FromMinorUnits(99_999L),
        };

        Credits forward = Credits.Zero;
        foreach (Credits amount in amounts)
        {
            forward += amount;
        }

        Credits backward = Credits.Zero;
        for (int i = amounts.Length - 1; i >= 0; i--)
        {
            backward += amounts[i];
        }

        Assert.Equal(forward, backward);
        Assert.Equal(100_340L, forward.MinorUnits);
    }

    [Fact]
    public void Multiplication_ScalesByWholeMultiplier()
    {
        var fee = Credits.FromWholeCredits(25);

        Assert.Equal(Credits.FromWholeCredits(250), fee * 10L);
        Assert.Equal(Credits.FromWholeCredits(250), 10L * fee);
    }

    [Fact]
    public void Addition_Overflowing_Throws()
    {
        // CheckForOverflowUnderflow is on for the whole build; a silent wraparound in a
        // balance would be a currency exploit.
        var max = Credits.FromMinorUnits(long.MaxValue);

        Assert.Throws<OverflowException>(() => max + Credits.FromMinorUnits(1L));
    }

    // ── Comparison ───────────────────────────────────────────────────────────

    [Fact]
    public void Comparison_OrdersByMinorUnits()
    {
        var small = Credits.FromWholeCredits(10);
        var large = Credits.FromWholeCredits(20);

        Assert.True(small < large);
        Assert.True(large > small);
        Assert.True(small <= Credits.FromWholeCredits(10));
        Assert.True(small >= Credits.FromWholeCredits(10));
        Assert.Equal(-1, small.CompareTo(large));
        Assert.Equal(0, small.CompareTo(Credits.FromWholeCredits(10)));
    }

    [Fact]
    public void MinAndMax_PickTheExpectedOperand()
    {
        var small = Credits.FromWholeCredits(10);
        var large = Credits.FromWholeCredits(20);

        Assert.Equal(small, Credits.Min(small, large));
        Assert.Equal(large, Credits.Max(small, large));
    }

    [Fact]
    public void Sorting_UsesTheComparison()
    {
        var amounts = new[]
        {
            Credits.FromWholeCredits(30),
            Credits.FromWholeCredits(10),
            Credits.FromWholeCredits(20),
        };

        Array.Sort(amounts);

        Assert.Equal(Credits.FromWholeCredits(10), amounts[0]);
        Assert.Equal(Credits.FromWholeCredits(30), amounts[2]);
    }

    [Fact]
    public void Equality_IsValueBased()
    {
        Assert.Equal(Credits.FromMinorUnits(500L), Credits.FromWholeCredits(5));
        Assert.NotEqual(Credits.FromMinorUnits(501L), Credits.FromWholeCredits(5));
    }

    // ── Formatting ───────────────────────────────────────────────────────────

    [Theory]
    [InlineData(0L, "0.00 cr")]
    [InlineData(5L, "0.05 cr")]
    [InlineData(100L, "1.00 cr")]
    [InlineData(123_450L, "1,234.50 cr")]
    [InlineData(-2_500L, "-25.00 cr")]
    public void ToString_FormatsAsWholeCreditsWithMinorDigits(long minorUnits, string expected)
    {
        // Formatting is centralized so the divide-by-100 exists in exactly one place.
        Assert.Equal(expected, Credits.FromMinorUnits(minorUnits).ToString());
    }
}
