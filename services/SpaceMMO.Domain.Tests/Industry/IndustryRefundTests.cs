using SpaceMMO.Domain.Industry;
using Xunit;

namespace SpaceMMO.Domain.Tests.Industry;

/// <summary>
/// Tests for industry job cancellation refunds (design-bible §6).
/// </summary>
/// <remarks>
/// Two properties carry the design: an early cancel must return essentially everything, so
/// misclicks are forgiven, and a late cancel must return almost nothing, so a job cannot be used
/// as a free option on the output price.
/// </remarks>
public sealed class IndustryRefundTests
{
    // ── The two properties the design rests on ───────────────────────────────

    [Fact]
    public void CancellingImmediately_RefundsEverything()
    {
        // The misclick case. No grace period is needed — proportionality already handles it.
        Assert.Equal(20, IndustryRefund.RefundedQuantity(20, elapsedSeconds: 0, totalSeconds: 600));
        Assert.Equal(20, IndustryRefund.RefundedQuantity(20, elapsedSeconds: 1, totalSeconds: 600));
    }

    [Fact]
    public void CancellingNearCompletion_RefundsAlmostNothing()
    {
        // The anti-optionality case. Backing out late must not be cheap.
        Assert.Equal(1, IndustryRefund.RefundedQuantity(20, elapsedSeconds: 570, totalSeconds: 600));
        Assert.Equal(0, IndustryRefund.RefundedQuantity(20, elapsedSeconds: 595, totalSeconds: 600));
    }

    [Theory]
    [InlineData(0, 20)]     // untouched
    [InlineData(150, 15)]   // 25% elapsed
    [InlineData(300, 10)]   // halfway
    [InlineData(450, 5)]    // 75% elapsed
    [InlineData(600, 0)]    // complete
    public void Refund_IsProportionalToTimeRemaining(long elapsed, int expected)
    {
        Assert.Equal(expected, IndustryRefund.RefundedQuantity(20, elapsed, totalSeconds: 600));
    }

    [Fact]
    public void Refund_DecreasesMonotonicallyAsTheJobProgresses()
    {
        // A later cancel must never return more than an earlier one, or there would be an
        // optimal moment to bail that rewards waiting.
        int previous = int.MaxValue;

        for (long elapsed = 0; elapsed <= 600; elapsed += 10)
        {
            int refunded = IndustryRefund.RefundedQuantity(100, elapsed, totalSeconds: 600);

            Assert.True(refunded <= previous, $"Refund rose at {elapsed}s: {previous} -> {refunded}.");
            previous = refunded;
        }
    }

    // ── Conservation ─────────────────────────────────────────────────────────

    [Fact]
    public void Refund_NeverExceedsWhatWasConsumed()
    {
        // The property that makes rounding up safe: a refund can return material but never
        // create it, so cancellation can never be a material faucet.
        foreach (int consumed in new[] { 1, 2, 7, 20, 1_000, 999_999 })
        {
            foreach (long total in new[] { 30L, 60L, 300L, 900L, 86_400L })
            {
                foreach (long elapsed in new[] { 0L, 1L, total / 3, total / 2, total - 1, total })
                {
                    int refunded = IndustryRefund.RefundedQuantity(consumed, elapsed, total);

                    Assert.InRange(refunded, 0, consumed);
                }
            }
        }
    }

    [Fact]
    public void Refund_PastCompletion_IsZero()
    {
        // A finished job should be claimed, not cancelled.
        Assert.Equal(0, IndustryRefund.RefundedQuantity(20, elapsedSeconds: 600, totalSeconds: 600));
        Assert.Equal(0, IndustryRefund.RefundedQuantity(20, elapsedSeconds: 9_999, totalSeconds: 600));
    }

    // ── Single-unit inputs ───────────────────────────────────────────────────

    [Fact]
    public void SingleExpensiveInput_IsReturnedWhenCancelledEarly()
    {
        // Rounding down would floor a hull section cancelled at 5% to zero, so the input would
        // simply vanish. That reads as a bug however it is documented.
        Assert.Equal(1, IndustryRefund.RefundedQuantity(1, elapsedSeconds: 45, totalSeconds: 900));
    }

    [Fact]
    public void SingleExpensiveInput_IsLostWhenCancelledLate()
    {
        Assert.Equal(0, IndustryRefund.RefundedQuantity(1, elapsedSeconds: 800, totalSeconds: 900));
    }

    [Fact]
    public void SingleInput_FlipsAtTheHalfwayPoint()
    {
        // Half rounds up, favouring the player — safe because the refund is capped by the input.
        Assert.Equal(1, IndustryRefund.RefundedQuantity(1, elapsedSeconds: 450, totalSeconds: 900));
        Assert.Equal(0, IndustryRefund.RefundedQuantity(1, elapsedSeconds: 451, totalSeconds: 900));
    }

    // ── Determinism ──────────────────────────────────────────────────────────

    [Fact]
    public void Refund_UsesNoFloatingPoint_SoItIsExactlyReproducible()
    {
        // Awkward ratios that a double would round inconsistently across platforms.
        Assert.Equal(
            IndustryRefund.RefundedQuantity(7, 333, 999),
            IndustryRefund.RefundedQuantity(7, 333, 999));

        Assert.Equal(5, IndustryRefund.RefundedQuantity(7, 333, 999));
    }

    [Fact]
    public void Refund_HandlesVeryLargeJobsWithoutOverflow()
    {
        // A million units over a day-long job: the intermediate product is ~1.7e11, which needs
        // 64-bit arithmetic.
        Assert.Equal(
            1_000_000, IndustryRefund.RefundedQuantity(1_000_000, 0, totalSeconds: 86_400));
    }

    // ── Batch duration ───────────────────────────────────────────────────────

    [Theory]
    [InlineData(60, 1, 60L)]
    [InlineData(60, 5, 300L)]
    [InlineData(900, 3, 2_700L)]
    public void TotalJobSeconds_ScalesLinearlyWithRuns(int recipeSeconds, int runs, long expected)
    {
        // No batch discount: batching saves clicks, not time.
        Assert.Equal(expected, IndustryRefund.TotalJobSeconds(recipeSeconds, runs));
    }

    [Fact]
    public void TotalJobSeconds_ForLargeBatches_DoesNotOverflow()
    {
        Assert.Equal(
            86_400L * 100_000, IndustryRefund.TotalJobSeconds(86_400, 100_000));
    }

    // ── Validation ───────────────────────────────────────────────────────────

    [Fact]
    public void RefundedQuantity_WithInvalidArguments_Throws()
    {
        Assert.Throws<ArgumentOutOfRangeException>(() => IndustryRefund.RefundedQuantity(-1, 0, 600));
        Assert.Throws<ArgumentOutOfRangeException>(() => IndustryRefund.RefundedQuantity(20, -1, 600));
        Assert.Throws<ArgumentOutOfRangeException>(() => IndustryRefund.RefundedQuantity(20, 0, 0));
    }

    [Fact]
    public void RefundedQuantity_OfNothing_IsZero()
    {
        Assert.Equal(0, IndustryRefund.RefundedQuantity(0, 0, 600));
    }

    [Fact]
    public void TotalJobSeconds_WithInvalidArguments_Throws()
    {
        Assert.Throws<ArgumentOutOfRangeException>(() => IndustryRefund.TotalJobSeconds(0, 1));
        Assert.Throws<ArgumentOutOfRangeException>(() => IndustryRefund.TotalJobSeconds(60, 0));
    }

    [Fact]
    public void RefundIsWorthless_MatchesAZeroRefund()
    {
        Assert.True(IndustryRefund.RefundIsWorthless(20, 595, 600));
        Assert.False(IndustryRefund.RefundIsWorthless(20, 100, 600));
    }
}
