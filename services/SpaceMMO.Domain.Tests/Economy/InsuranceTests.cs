using SpaceMMO.Domain.Economy;
using SpaceMMO.Domain.Loss;
using Xunit;

namespace SpaceMMO.Domain.Tests.Economy;

/// <summary>
/// Tests for ship insurance (ADR-0006).
/// </summary>
/// <remarks>
/// The unprofitability tests are the important ones. Insurance is the most exploitable
/// system in the game — a single case where deliberate destruction pays is an unbounded
/// credit printer — so those properties are asserted across a sweep of values rather than
/// spot-checked.
/// </remarks>
public sealed class InsuranceTests
{
    /// <summary>A 3,500 cr hull — the price target for the starting shuttle.</summary>
    private static readonly Credits ShuttleValue = Credits.FromWholeCredits(3_500);

    private static readonly InsuranceTier[] AllTiers = Enum.GetValues<InsuranceTier>();

    [Theory]
    [InlineData(InsuranceTier.None, 0, 0)]
    [InlineData(InsuranceTier.Basic, 4_000, 600)]
    [InlineData(InsuranceTier.Standard, 6_000, 1_200)]
    [InlineData(InsuranceTier.Premium, 8_000, 2_000)]
    public void TermsFor_MatchesTheDesignedRates(
        InsuranceTier tier, int expectedPayoutBp, int expectedPremiumBp)
    {
        InsuranceTerms terms = Insurance.TermsFor(tier);

        Assert.Equal(tier, terms.Tier);
        Assert.Equal(expectedPayoutBp, terms.PayoutBasisPoints);
        Assert.Equal(expectedPremiumBp, terms.PremiumBasisPoints);
    }

    [Fact]
    public void TermsFor_UndefinedTier_Throws()
    {
        Assert.Throws<ArgumentOutOfRangeException>(() => Insurance.TermsFor((InsuranceTier)99));
    }

    [Fact]
    public void NoTier_PaysOutOneHundredPercent()
    {
        // The single most important structural guard: a 100% payout would make loss free,
        // and free loss plus any premium discount is a printer.
        foreach (InsuranceTier tier in AllTiers)
        {
            Assert.True(
                Insurance.TermsFor(tier).PayoutBasisPoints < Credits.OneHundredPercentBasisPoints,
                $"{tier} pays out at or above 100%, which makes deliberate loss profitable.");
        }
    }

    // ── Premiums and payouts ─────────────────────────────────────────────────

    [Theory]
    [InlineData(InsuranceTier.None, 0L)]
    [InlineData(InsuranceTier.Basic, 21_000L)]     // 6% of 3,500 cr = 210 cr
    [InlineData(InsuranceTier.Standard, 42_000L)]  // 12% = 420 cr
    [InlineData(InsuranceTier.Premium, 70_000L)]   // 20% = 700 cr
    public void PremiumFor_ChargesTheTierRate(InsuranceTier tier, long expectedMinorUnits)
    {
        Assert.Equal(expectedMinorUnits, Insurance.PremiumFor(tier, ShuttleValue).MinorUnits);
    }

    [Theory]
    [InlineData(InsuranceTier.None, 0L)]
    [InlineData(InsuranceTier.Basic, 140_000L)]    // 40% of 3,500 cr = 1,400 cr
    [InlineData(InsuranceTier.Standard, 210_000L)] // 60% = 2,100 cr
    [InlineData(InsuranceTier.Premium, 280_000L)]  // 80% = 2,800 cr
    public void PayoutFor_PaysTheTierRate(InsuranceTier tier, long expectedMinorUnits)
    {
        Credits payout = Insurance.PayoutFor(tier, ShuttleValue, DeathCause.ShipExplosion);

        Assert.Equal(expectedMinorUnits, payout.MinorUnits);
    }

    [Fact]
    public void PayoutFor_SelfDestruct_IsVoidAtEveryTier()
    {
        // Removes the zero-risk path to a payout entirely.
        foreach (InsuranceTier tier in AllTiers)
        {
            Assert.True(
                Insurance.PayoutFor(tier, ShuttleValue, DeathCause.SelfDestruct).IsZero,
                $"{tier} paid out on self-destruct, which voids the policy.");
        }
    }

    [Fact]
    public void PayoutFor_PaysOnEveryOtherCause()
    {
        // Only self-destruct voids coverage; being blown up by someone else is the
        // situation insurance exists for.
        foreach (DeathCause cause in Enum.GetValues<DeathCause>())
        {
            if (cause == DeathCause.SelfDestruct)
            {
                continue;
            }

            Assert.True(
                Insurance.PayoutFor(InsuranceTier.Premium, ShuttleValue, cause).IsPositive,
                $"Premium tier should pay out for {cause}.");
        }
    }

    [Fact]
    public void PremiumFor_NegativeValue_Throws()
    {
        Assert.Throws<ArgumentOutOfRangeException>(
            () => Insurance.PremiumFor(InsuranceTier.Basic, Credits.FromMinorUnits(-1L)));
    }

    // ── The exploit that must not exist ──────────────────────────────────────

    [Fact]
    public void LossIsAlwaysUnprofitable_AcrossEveryTierAndValue()
    {
        // Swept rather than spot-checked: one counterexample is an unbounded credit
        // printer, so this is the highest-value assertion in the file.
        long[] values =
        [
            0L, 1L, 7L, 99L, 100L, 101L, 12_345L, 350_000L,
            1_000_000_000L, 1_000_000_000_000L, 900_000_000_000_000L,
        ];

        foreach (InsuranceTier tier in AllTiers)
        {
            foreach (long minorUnits in values)
            {
                var value = Credits.FromMinorUnits(minorUnits);

                Assert.True(
                    Insurance.LossIsAlwaysUnprofitable(tier, value),
                    $"Deliberate loss is profitable at {tier} with value {value}.");
            }
        }
    }

    [Fact]
    public void DeliberateLoss_LeavesTheOwnerWorseOff()
    {
        // The same property stated as the arithmetic a player would actually do:
        // destroy an insured hull and count what you have left.
        foreach (InsuranceTier tier in AllTiers)
        {
            Credits premium = Insurance.PremiumFor(tier, ShuttleValue);
            Credits payout = Insurance.PayoutFor(tier, ShuttleValue, DeathCause.ShipExplosion);

            Credits net = payout - ShuttleValue - premium;

            Assert.True(net.IsNegative, $"{tier} yields a non-negative net of {net} on deliberate loss.");
        }
    }

    [Fact]
    public void HigherTiers_CostMoreAndPayMore()
    {
        // Monotonic in both directions, so there is never a strictly dominated tier.
        InsuranceTier[] ascending =
        [
            InsuranceTier.None, InsuranceTier.Basic, InsuranceTier.Standard, InsuranceTier.Premium,
        ];

        for (int i = 1; i < ascending.Length; i++)
        {
            Assert.True(
                Insurance.PremiumFor(ascending[i], ShuttleValue)
                    > Insurance.PremiumFor(ascending[i - 1], ShuttleValue),
                $"{ascending[i]} premium should exceed {ascending[i - 1]}.");

            Assert.True(
                Insurance.PayoutFor(ascending[i], ShuttleValue, DeathCause.ShipExplosion)
                    > Insurance.PayoutFor(ascending[i - 1], ShuttleValue, DeathCause.ShipExplosion),
                $"{ascending[i]} payout should exceed {ascending[i - 1]}.");
        }
    }

    // ── Break-even ───────────────────────────────────────────────────────────

    [Theory]
    [InlineData(InsuranceTier.None, 0)]
    [InlineData(InsuranceTier.Basic, 1_500)]     // 15%
    [InlineData(InsuranceTier.Standard, 2_000)]  // 20%
    [InlineData(InsuranceTier.Premium, 2_500)]   // 25%
    public void BreakEvenLossRate_MatchesPremiumOverPayout(InsuranceTier tier, int expectedBp)
    {
        Assert.Equal(expectedBp, Insurance.BreakEvenLossRateBasisPoints(tier));
    }

    [Fact]
    public void BreakEvenLossRate_RisesWithTier()
    {
        // The intended shape: premium coverage is a decision made before something
        // dangerous, not a default. If this inverted, the top tier would be strictly
        // better for everyone and the tiers would be pointless.
        Assert.True(
            Insurance.BreakEvenLossRateBasisPoints(InsuranceTier.Basic)
                < Insurance.BreakEvenLossRateBasisPoints(InsuranceTier.Standard));

        Assert.True(
            Insurance.BreakEvenLossRateBasisPoints(InsuranceTier.Standard)
                < Insurance.BreakEvenLossRateBasisPoints(InsuranceTier.Premium));
    }

    [Fact]
    public void CoversLoss_IsFalseOnlyForNone()
    {
        Assert.False(Insurance.TermsFor(InsuranceTier.None).CoversLoss);
        Assert.True(Insurance.TermsFor(InsuranceTier.Basic).CoversLoss);
        Assert.True(Insurance.TermsFor(InsuranceTier.Standard).CoversLoss);
        Assert.True(Insurance.TermsFor(InsuranceTier.Premium).CoversLoss);
    }
}
