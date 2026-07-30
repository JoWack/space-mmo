using SpaceMMO.Domain.Economy;
using SpaceMMO.Domain.Market;
using Xunit;

namespace SpaceMMO.Domain.Tests.Market;

/// <summary>
/// Tests for order matching (economy-design §5).
/// </summary>
/// <remarks>
/// The failure modes here are duplicated items and duplicated money, so the conservation and
/// priority properties are asserted as invariants over generated books rather than only on
/// hand-picked examples.
/// </remarks>
public sealed class MatchingEngineTests
{
    private const int Buyer = 1;
    private const int Seller = 2;
    private const int ThirdParty = 3;

    private static readonly DateTimeOffset T0 =
        new(2026, 7, 30, 12, 0, 0, TimeSpan.Zero);

    private static Credits Cr(long whole) => Credits.FromWholeCredits(whole);

    private static RestingOrder Ask(
        long id, long price, int quantity, int seconds = 0, int characterId = Seller) =>
        new(id, characterId, OrderSide.Sell, Cr(price), quantity, T0.AddSeconds(seconds));

    private static RestingOrder Bid(
        long id, long price, int quantity, int seconds = 0, int characterId = Buyer) =>
        new(id, characterId, OrderSide.Buy, Cr(price), quantity, T0.AddSeconds(seconds));

    private static MatchRequest Buy(long limitPrice, int quantity, int characterId = Buyer) =>
        new(characterId, OrderSide.Buy, Cr(limitPrice), quantity);

    private static MatchRequest Sell(long limitPrice, int quantity, int characterId = Seller) =>
        new(characterId, OrderSide.Sell, Cr(limitPrice), quantity);

    // ── Basic crossing ───────────────────────────────────────────────────────

    [Fact]
    public void Buy_AgainstAnEmptyBook_RestsEntirely()
    {
        MatchResult result = MatchingEngine.Match(Buy(100, 10), []);

        Assert.True(result.IsUnmatched);
        Assert.Equal(10, result.QuantityUnfilled);
        Assert.True(result.GrossValue.IsZero);
    }

    [Fact]
    public void Buy_AtOrAboveTheAsk_Fills()
    {
        MatchResult result = MatchingEngine.Match(Buy(100, 5), [Ask(1, 100, 5)]);

        Assert.True(result.IsFullyFilled);
        Assert.Equal(5, result.QuantityFilled);
        Assert.Equal(Cr(100), result.Fills[0].Price);
    }

    [Fact]
    public void Buy_BelowTheAsk_DoesNotCross()
    {
        MatchResult result = MatchingEngine.Match(Buy(99, 5), [Ask(1, 100, 5)]);

        Assert.True(result.IsUnmatched);
        Assert.Equal(5, result.QuantityUnfilled);
    }

    [Fact]
    public void Sell_AboveTheBid_DoesNotCross()
    {
        MatchResult result = MatchingEngine.Match(Sell(101, 5), [Bid(1, 100, 5)]);

        Assert.True(result.IsUnmatched);
    }

    [Fact]
    public void Sell_AtOrBelowTheBid_Fills()
    {
        MatchResult result = MatchingEngine.Match(Sell(100, 5), [Bid(1, 100, 5)]);

        Assert.True(result.IsFullyFilled);
        Assert.Equal(Cr(100), result.Fills[0].Price);
    }

    [Fact]
    public void Match_IgnoresOrdersOnTheSameSide()
    {
        // A buy must never match another buy, however the prices line up.
        MatchResult result = MatchingEngine.Match(Buy(1_000, 5), [Bid(1, 10, 5, characterId: ThirdParty)]);

        Assert.True(result.IsUnmatched);
    }

    // ── Execution price ──────────────────────────────────────────────────────

    [Fact]
    public void Fill_ExecutesAtTheRestingPrice_GivingTheTakerPriceImprovement()
    {
        // A buyer willing to pay 150 hitting a 100 ask pays 100, not 150. The patient order
        // gets exactly the price it asked for.
        MatchResult result = MatchingEngine.Match(Buy(150, 5), [Ask(1, 100, 5)]);

        Assert.Equal(Cr(100), result.Fills[0].Price);
        Assert.Equal(Cr(500), result.GrossValue);
    }

    [Fact]
    public void Fill_ExecutesAtTheRestingPrice_WhenSelling()
    {
        // A seller willing to accept 50 hitting a 100 bid receives 100.
        MatchResult result = MatchingEngine.Match(Sell(50, 5), [Bid(1, 100, 5)]);

        Assert.Equal(Cr(100), result.Fills[0].Price);
    }

    // ── Price priority ───────────────────────────────────────────────────────

    [Fact]
    public void Buy_TakesTheCheapestAskFirst()
    {
        MatchResult result = MatchingEngine.Match(
            Buy(120, 3),
            [Ask(1, 120, 3), Ask(2, 100, 3), Ask(3, 110, 3)]);

        Assert.Equal(2L, result.Fills[0].RestingOrderId);
        Assert.Equal(Cr(100), result.Fills[0].Price);
    }

    [Fact]
    public void Sell_TakesTheHighestBidFirst()
    {
        MatchResult result = MatchingEngine.Match(
            Sell(80, 3),
            [Bid(1, 90, 3), Bid(2, 110, 3), Bid(3, 100, 3)]);

        Assert.Equal(2L, result.Fills[0].RestingOrderId);
        Assert.Equal(Cr(110), result.Fills[0].Price);
    }

    [Fact]
    public void Buy_WalksTheBookInPriceOrder()
    {
        MatchResult result = MatchingEngine.Match(
            Buy(120, 9),
            [Ask(1, 120, 3), Ask(2, 100, 3), Ask(3, 110, 3)]);

        Assert.Equal([2L, 3L, 1L], result.Fills.Select(f => f.RestingOrderId));
        Assert.Equal(Cr((100 * 3) + (110 * 3) + (120 * 3)), result.GrossValue);
    }

    // ── Time priority ────────────────────────────────────────────────────────

    [Fact]
    public void AtEqualPrice_TheOldestOrderFillsFirst()
    {
        MatchResult result = MatchingEngine.Match(
            Buy(100, 3),
            [Ask(1, 100, 3, seconds: 30), Ask(2, 100, 3, seconds: 10), Ask(3, 100, 3, seconds: 20)]);

        Assert.Equal(2L, result.Fills[0].RestingOrderId);
    }

    [Fact]
    public void AtEqualPriceAndTime_OrderIdBreaksTheTie()
    {
        // Without this, two orders landing in the same clock tick would match in whatever
        // order the database happened to return, making matching non-deterministic.
        MatchResult result = MatchingEngine.Match(
            Buy(100, 3),
            [Ask(7, 100, 3), Ask(3, 100, 3), Ask(5, 100, 3)]);

        Assert.Equal(3L, result.Fills[0].RestingOrderId);
    }

    [Fact]
    public void PricePriority_BeatsTimePriority()
    {
        // A cheaper ask placed later still fills before a dearer one placed earlier.
        MatchResult result = MatchingEngine.Match(
            Buy(120, 3),
            [Ask(1, 120, 3, seconds: 0), Ask(2, 100, 3, seconds: 60)]);

        Assert.Equal(2L, result.Fills[0].RestingOrderId);
    }

    // ── Partial fills ────────────────────────────────────────────────────────

    [Fact]
    public void IncomingOrder_LargerThanTheBook_PartiallyFillsAndRests()
    {
        MatchResult result = MatchingEngine.Match(Buy(100, 10), [Ask(1, 100, 3)]);

        Assert.Equal(3, result.QuantityFilled);
        Assert.Equal(7, result.QuantityUnfilled);
        Assert.False(result.IsFullyFilled);
    }

    [Fact]
    public void IncomingOrder_SmallerThanARestingOrder_TakesOnlyWhatItNeeds()
    {
        MatchResult result = MatchingEngine.Match(Buy(100, 2), [Ask(1, 100, 10)]);

        Assert.Single(result.Fills);
        Assert.Equal(2, result.Fills[0].Quantity);
        Assert.True(result.IsFullyFilled);
    }

    [Fact]
    public void Match_NeverFillsMoreThanRequested()
    {
        // Overfilling is the bug that creates items out of nothing.
        MatchResult result = MatchingEngine.Match(
            Buy(200, 5),
            [Ask(1, 100, 100), Ask(2, 110, 100), Ask(3, 120, 100)]);

        Assert.Equal(5, result.QuantityFilled);
        Assert.Equal(0, result.QuantityUnfilled);
    }

    [Fact]
    public void Match_NeverTakesMoreThanARestingOrderHas()
    {
        MatchResult result = MatchingEngine.Match(
            Buy(200, 100),
            [Ask(1, 100, 3), Ask(2, 110, 4)]);

        Assert.Equal(3, result.Fills[0].Quantity);
        Assert.Equal(4, result.Fills[1].Quantity);
        Assert.Equal(93, result.QuantityUnfilled);
    }

    // ── Self-trade prevention ────────────────────────────────────────────────

    [Fact]
    public void Match_SkipsTheSameCharactersOwnOrders()
    {
        // Not because wash trading is free — both sides pay fees — but because trade history
        // is the price signal other players read before deciding what to build.
        MatchResult result = MatchingEngine.Match(
            Buy(100, 5, characterId: Buyer),
            [Ask(1, 100, 5, characterId: Buyer)]);

        Assert.True(result.IsUnmatched);
        Assert.Equal(5, result.QuantityUnfilled);
    }

    [Fact]
    public void Match_SkipsOwnOrdersButStillTakesOthers()
    {
        MatchResult result = MatchingEngine.Match(
            Buy(100, 10, characterId: Buyer),
            [Ask(1, 90, 5, characterId: Buyer), Ask(2, 100, 5, characterId: ThirdParty)]);

        Assert.Single(result.Fills);
        Assert.Equal(2L, result.Fills[0].RestingOrderId);
        Assert.Equal(5, result.QuantityUnfilled);
    }

    [Fact]
    public void SelfTradePrevention_IsIntentional()
    {
        Assert.True(MatchingEngine.SkipsOwnOrders);
    }

    // ── Exhausted and stale orders ───────────────────────────────────────────

    [Theory]
    [InlineData(0)]
    [InlineData(-1)]
    public void Match_IgnoresOrdersWithNoQuantityRemaining(int quantityRemaining)
    {
        // A fully filled order still on the loaded book must not produce a phantom fill.
        MatchResult result = MatchingEngine.Match(
            Buy(100, 5),
            [Ask(1, 100, quantityRemaining)]);

        Assert.True(result.IsUnmatched);
    }

    // ── Invariants over the whole book ───────────────────────────────────────

    [Fact]
    public void Match_AlwaysConservesQuantity()
    {
        // Filled + unfilled must equal requested, for every book shape. This is the property
        // that stops matching from creating or destroying units.
        var book = new List<RestingOrder>
        {
            Ask(1, 100, 3, 0, ThirdParty),
            Ask(2, 110, 7, 1, ThirdParty),
            Ask(3, 100, 5, 2, ThirdParty),
            Ask(4, 130, 2, 3, ThirdParty),
            Bid(5, 90, 4, 4, ThirdParty),
        };

        foreach (int quantity in new[] { 1, 3, 8, 15, 20, 100 })
        {
            foreach (long limit in new[] { 90L, 100L, 110L, 130L, 200L })
            {
                MatchResult result = MatchingEngine.Match(Buy(limit, quantity), book);

                Assert.Equal(quantity, result.QuantityFilled + result.QuantityUnfilled);
            }
        }
    }

    [Fact]
    public void Match_NeverExceedsTheLimitPrice()
    {
        var book = new List<RestingOrder>
        {
            Ask(1, 90, 3, 0, ThirdParty),
            Ask(2, 100, 3, 1, ThirdParty),
            Ask(3, 150, 3, 2, ThirdParty),
            Ask(4, 200, 3, 3, ThirdParty),
        };

        foreach (long limit in new[] { 90L, 100L, 150L, 199L })
        {
            MatchResult result = MatchingEngine.Match(Buy(limit, 100), book);

            foreach (Fill fill in result.Fills)
            {
                Assert.True(
                    fill.Price <= Cr(limit),
                    $"Filled at {fill.Price} against a limit of {Cr(limit)}.");
            }
        }
    }

    [Fact]
    public void Match_NeverFillsBelowASellersLimit()
    {
        var book = new List<RestingOrder>
        {
            Bid(1, 200, 3, 0, ThirdParty),
            Bid(2, 150, 3, 1, ThirdParty),
            Bid(3, 100, 3, 2, ThirdParty),
        };

        foreach (long limit in new[] { 100L, 150L, 200L })
        {
            MatchResult result = MatchingEngine.Match(Sell(limit, 100), book);

            foreach (Fill fill in result.Fills)
            {
                Assert.True(fill.Price >= Cr(limit), $"Sold at {fill.Price} below a limit of {Cr(limit)}.");
            }
        }
    }

    [Fact]
    public void Match_NeverFillsTheSameRestingOrderTwice()
    {
        // Hitting one order twice in a single match would let it oversell its own quantity.
        var book = new List<RestingOrder>
        {
            Ask(1, 100, 5, 0, ThirdParty),
            Ask(2, 100, 5, 1, ThirdParty),
            Ask(3, 100, 5, 2, ThirdParty),
        };

        MatchResult result = MatchingEngine.Match(Buy(100, 15), book);

        var ids = result.Fills.Select(f => f.RestingOrderId).ToList();

        Assert.Equal(ids.Count, ids.Distinct().Count());
    }

    [Fact]
    public void Match_IsDeterministicRegardlessOfBookOrder()
    {
        // The data layer's row ordering is not something matching should depend on.
        var book = new List<RestingOrder>
        {
            Ask(1, 100, 3, 5, ThirdParty),
            Ask(2, 100, 3, 5, ThirdParty),
            Ask(3, 90, 3, 9, ThirdParty),
            Ask(4, 110, 3, 1, ThirdParty),
        };

        MatchResult forward = MatchingEngine.Match(Buy(120, 12), book);
        MatchResult reversed = MatchingEngine.Match(Buy(120, 12), [.. Enumerable.Reverse(book)]);

        Assert.Equal(forward.Fills, reversed.Fills);
    }

    [Fact]
    public void GrossValue_IsTheSumOfFillValues()
    {
        MatchResult result = MatchingEngine.Match(
            Buy(120, 6),
            [Ask(1, 100, 2, 0, ThirdParty), Ask(2, 110, 4, 1, ThirdParty)]);

        Assert.Equal(Cr((100 * 2) + (110 * 4)), result.GrossValue);
    }

    // ── Validation ───────────────────────────────────────────────────────────

    [Theory]
    [InlineData(0)]
    [InlineData(-5)]
    public void Match_WithNonPositiveQuantity_Throws(int quantity)
    {
        Assert.Throws<ArgumentOutOfRangeException>(
            () => MatchingEngine.Match(Buy(100, quantity), []));
    }

    [Fact]
    public void Match_WithNonPositivePrice_Throws()
    {
        Assert.Throws<ArgumentOutOfRangeException>(() => MatchingEngine.Match(
            new MatchRequest(Buyer, OrderSide.Buy, Credits.Zero, 5), []));
    }

    [Fact]
    public void Match_WithNullBook_Throws()
    {
        Assert.Throws<ArgumentNullException>(() => MatchingEngine.Match(Buy(100, 5), null!));
    }
}
