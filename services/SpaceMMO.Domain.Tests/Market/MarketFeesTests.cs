using SpaceMMO.Domain.Economy;
using SpaceMMO.Domain.Market;
using Xunit;

namespace SpaceMMO.Domain.Tests.Market;

/// <summary>
/// Tests for market fees (economy-design §3).
/// </summary>
public sealed class MarketFeesTests
{
    private static Credits Cr(long whole) => Credits.FromWholeCredits(whole);

    [Fact]
    public void GrossValue_MultipliesPriceByQuantity()
    {
        Assert.Equal(Cr(2_500), MarketFees.GrossValue(Cr(250), 10));
    }

    [Theory]
    [InlineData(0)]
    [InlineData(-1)]
    public void GrossValue_WithNonPositiveQuantity_Throws(int quantity)
    {
        Assert.Throws<ArgumentOutOfRangeException>(() => MarketFees.GrossValue(Cr(100), quantity));
    }

    [Fact]
    public void BrokerFee_IsOnePercentByDefault()
    {
        // 1% of 2,500 cr = 25 cr.
        Assert.Equal(Cr(25), MarketFees.BrokerFee(Cr(250), 10));
    }

    [Fact]
    public void SalesTax_IsTwoPercentByDefault()
    {
        // 2% of 2,500 cr = 50 cr.
        Assert.Equal(Cr(50), MarketFees.SalesTax(Cr(250), 10));
    }

    [Fact]
    public void Fees_RoundUp_SoTheyAreNeverFree()
    {
        // 1% of a single minor unit is 0.01, which must still cost something. A fee that
        // rounds to zero is a free option, and free options get farmed.
        Credits fee = MarketFees.BrokerFee(Credits.FromMinorUnits(1L), 1);

        Assert.Equal(1L, fee.MinorUnits);
    }

    [Fact]
    public void NetSellerProceeds_IsGrossMinusTax()
    {
        Credits gross = MarketFees.GrossValue(Cr(250), 10);
        Credits tax = MarketFees.SalesTax(Cr(250), 10);

        Assert.Equal(gross - tax, MarketFees.NetSellerProceeds(Cr(250), 10));
        Assert.Equal(Cr(2_450), MarketFees.NetSellerProceeds(Cr(250), 10));
    }

    [Fact]
    public void NetSellerProceeds_IsAlwaysLessThanGross()
    {
        // If tax ever rounded to zero the sink would leak at small trade sizes.
        foreach (long price in new[] { 1L, 7L, 100L, 12_345L })
        {
            foreach (int quantity in new[] { 1, 3, 1_000 })
            {
                var unitPrice = Credits.FromMinorUnits(price);

                Assert.True(
                    MarketFees.NetSellerProceeds(unitPrice, quantity)
                        < MarketFees.GrossValue(unitPrice, quantity),
                    $"Tax vanished at price {price} quantity {quantity}.");
            }
        }
    }

    [Fact]
    public void BuyerCost_IsGross_SoATradeIsNeverTaxedTwice()
    {
        // Tax comes out of the seller's side only. Charging both would silently double the
        // sink rate and throw off every price target in the design.
        Assert.Equal(MarketFees.GrossValue(Cr(250), 10), MarketFees.BuyerCost(Cr(250), 10));
    }

    [Fact]
    public void TaxIsTheDifferenceBetweenWhatTheBuyerPaysAndTheSellerReceives()
    {
        // The credits destroyed on a trade are exactly the tax — no more, no less. This is
        // what EconSim's ledger-conservation invariant depends on.
        Credits buyerPays = MarketFees.BuyerCost(Cr(250), 10);
        Credits sellerGets = MarketFees.NetSellerProceeds(Cr(250), 10);

        Assert.Equal(MarketFees.SalesTax(Cr(250), 10), buyerPays - sellerGets);
    }

    [Fact]
    public void Fees_ScaleWithTradeSize()
    {
        // Proportional rather than flat, so the sink stays meaningful as the economy grows.
        Assert.True(MarketFees.SalesTax(Cr(250), 100) > MarketFees.SalesTax(Cr(250), 10));
        Assert.True(MarketFees.BrokerFee(Cr(2_500), 10) > MarketFees.BrokerFee(Cr(250), 10));
    }

    [Fact]
    public void Fees_AcceptACustomRate()
    {
        Assert.Equal(Cr(250), MarketFees.BrokerFee(Cr(250), 10, basisPoints: 1_000));
        Assert.Equal(Cr(250), MarketFees.SalesTax(Cr(250), 10, basisPoints: 1_000));
    }

    [Fact]
    public void Fees_AtAZeroRate_AreZero()
    {
        Assert.True(MarketFees.BrokerFee(Cr(250), 10, basisPoints: 0).IsZero);
        Assert.True(MarketFees.SalesTax(Cr(250), 10, basisPoints: 0).IsZero);
    }

    [Fact]
    public void DefaultRates_MatchTheDesignDocument()
    {
        Assert.Equal(100, MarketFees.DefaultBrokerFeeBasisPoints);
        Assert.Equal(200, MarketFees.DefaultSalesTaxBasisPoints);
    }
}
