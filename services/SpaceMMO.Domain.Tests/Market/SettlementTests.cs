using SpaceMMO.Domain.Economy;
using SpaceMMO.Domain.Market;
using Xunit;

namespace SpaceMMO.Domain.Tests.Market;

/// <summary>
/// Tests for fill settlement (economy-design §3, §5).
/// </summary>
/// <remarks>
/// The conservation test is the one that matters: every credit taken from escrow must arrive
/// somewhere — the seller, the tax sink, or back to the buyer. A gap means credits vanishing
/// silently, and a surplus means credits being printed.
/// </remarks>
public sealed class SettlementTests
{
    private static Credits Cr(long whole) => Credits.FromWholeCredits(whole);

    // ── Escrow required at placement ─────────────────────────────────────────

    [Fact]
    public void EscrowRequired_IsTheFullQuantityAtTheLimitPrice()
    {
        Assert.Equal(Cr(2_500), Settlement.EscrowRequired(Cr(250), 10));
    }

    [Theory]
    [InlineData(0)]
    [InlineData(-1)]
    public void EscrowRequired_WithNonPositiveQuantity_Throws(int quantity)
    {
        Assert.Throws<ArgumentOutOfRangeException>(() => Settlement.EscrowRequired(Cr(250), quantity));
    }

    [Fact]
    public void EscrowRequired_WithNonPositivePrice_Throws()
    {
        Assert.Throws<ArgumentOutOfRangeException>(() => Settlement.EscrowRequired(Credits.Zero, 10));
    }

    // ── Fill at the escrowed price ───────────────────────────────────────────

    [Fact]
    public void Fill_AtTheEscrowedPrice_HasNoRefund()
    {
        FillSettlement settlement = Settlement.ForFill(Cr(250), Cr(250), 10);

        Assert.Equal(Cr(2_500), settlement.EscrowConsumed);
        Assert.True(settlement.BuyerRefund.IsZero);
        Assert.Equal(Cr(50), settlement.SalesTax);       // 2% of 2,500
        Assert.Equal(Cr(2_450), settlement.SellerProceeds);
    }

    // ── Price improvement ────────────────────────────────────────────────────

    [Fact]
    public void Fill_BelowTheEscrowedPrice_RefundsTheDifference()
    {
        // A buyer who bid 150 and filled at 100 must get the 50 per unit back. Without this the
        // difference would simply disappear from the economy.
        FillSettlement settlement = Settlement.ForFill(Cr(150), Cr(100), 10);

        Assert.Equal(Cr(1_000), settlement.EscrowConsumed);
        Assert.Equal(Cr(500), settlement.BuyerRefund);
        Assert.Equal(Cr(1_500), settlement.TotalEscrowReleased);
    }

    [Fact]
    public void Fill_TaxesTheExecutionPrice_NotTheEscrowedPrice()
    {
        // Taxing the bid rather than the trade would overcharge the seller for the buyer's
        // generosity.
        FillSettlement settlement = Settlement.ForFill(Cr(150), Cr(100), 10);

        Assert.Equal(Cr(20), settlement.SalesTax);  // 2% of 1,000, not of 1,500
    }

    [Fact]
    public void Fill_AboveTheEscrowedPrice_Throws()
    {
        // Charging a buyer more than their limit is a broken invariant, not a rounding concern.
        Assert.Throws<ArgumentOutOfRangeException>(() => Settlement.ForFill(Cr(100), Cr(150), 10));
    }

    // ── Conservation ─────────────────────────────────────────────────────────

    [Fact]
    public void Settlement_AlwaysConservesEscrow()
    {
        // Escrow released must equal seller proceeds + tax + refund, for every combination.
        // This is the property that stops credits leaking or being printed at settlement.
        foreach (long escrowPrice in new[] { 1L, 7L, 100L, 250L, 99_999L })
        {
            foreach (long fillPrice in new[] { 1L, 7L, 100L, 250L, 99_999L })
            {
                if (fillPrice > escrowPrice)
                {
                    continue;
                }

                foreach (int quantity in new[] { 1, 3, 17, 1_000 })
                {
                    FillSettlement settlement = Settlement.ForFill(
                        Credits.FromMinorUnits(escrowPrice),
                        Credits.FromMinorUnits(fillPrice),
                        quantity);

                    Credits accountedFor =
                        settlement.SellerProceeds + settlement.SalesTax + settlement.BuyerRefund;

                    Assert.Equal(settlement.TotalEscrowReleased, accountedFor);
                }
            }
        }
    }

    [Fact]
    public void Settlement_EscrowReleased_NeverExceedsWhatWasLocked()
    {
        // Releasing more than was escrowed would let a fill mint credits.
        foreach (long limitPrice in new[] { 10L, 250L, 5_000L })
        {
            foreach (int quantity in new[] { 1, 5, 100 })
            {
                Credits locked = Settlement.EscrowRequired(Credits.FromMinorUnits(limitPrice), quantity);

                foreach (long fillPrice in new[] { 1L, limitPrice / 2, limitPrice })
                {
                    if (fillPrice <= 0)
                    {
                        continue;
                    }

                    FillSettlement settlement = Settlement.ForFill(
                        Credits.FromMinorUnits(limitPrice),
                        Credits.FromMinorUnits(fillPrice),
                        quantity);

                    Assert.True(
                        settlement.TotalEscrowReleased == locked,
                        $"Released {settlement.TotalEscrowReleased} against {locked} locked.");
                }
            }
        }
    }

    [Fact]
    public void Settlement_SellerNeverReceivesMoreThanTheTradeValue()
    {
        FillSettlement settlement = Settlement.ForFill(Cr(500), Cr(300), 10);

        Assert.True(settlement.SellerProceeds < MarketFees.GrossValue(Cr(300), 10));
    }

    [Fact]
    public void Settlement_TaxIsAlwaysPositive_SoTheSinkNeverLeaks()
    {
        // Tax rounds up, so even a one-minor-unit trade destroys something. A tax that rounded
        // to zero would let small trades escape the sink entirely.
        foreach (long price in new[] { 1L, 2L, 49L, 50L })
        {
            FillSettlement settlement = Settlement.ForFill(
                Credits.FromMinorUnits(price), Credits.FromMinorUnits(price), 1);

            Assert.True(settlement.SalesTax.IsPositive, $"Tax vanished at price {price}.");
        }
    }

    [Fact]
    public void Settlement_MatchesMarketFees()
    {
        // Settlement and fee calculation must not drift apart into two answers.
        Assert.Equal(
            MarketFees.SalesTax(Cr(250), 10),
            Settlement.ForFill(Cr(250), Cr(250), 10).SalesTax);

        Assert.Equal(
            MarketFees.NetSellerProceeds(Cr(250), 10),
            Settlement.ForFill(Cr(250), Cr(250), 10).SellerProceeds);
    }

    // ── Validation ───────────────────────────────────────────────────────────

    [Theory]
    [InlineData(0)]
    [InlineData(-3)]
    public void Fill_WithNonPositiveQuantity_Throws(int quantity)
    {
        Assert.Throws<ArgumentOutOfRangeException>(
            () => Settlement.ForFill(Cr(250), Cr(250), quantity));
    }

    [Fact]
    public void Fill_WithNonPositivePrices_Throws()
    {
        Assert.Throws<ArgumentOutOfRangeException>(
            () => Settlement.ForFill(Credits.Zero, Cr(250), 10));

        Assert.Throws<ArgumentOutOfRangeException>(
            () => Settlement.ForFill(Cr(250), Credits.Zero, 10));
    }

    [Fact]
    public void Fill_AtACustomTaxRate_UsesIt()
    {
        FillSettlement settlement = Settlement.ForFill(Cr(250), Cr(250), 10, salesTaxBasisPoints: 1_000);

        Assert.Equal(Cr(250), settlement.SalesTax);
        Assert.Equal(Cr(2_250), settlement.SellerProceeds);
    }
}
