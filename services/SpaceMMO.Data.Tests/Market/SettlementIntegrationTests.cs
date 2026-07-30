using Microsoft.EntityFrameworkCore;
using SpaceMMO.Data.Entities;
using SpaceMMO.Data.Inventories;
using SpaceMMO.Data.Market;
using SpaceMMO.Domain.Characters;
using SpaceMMO.Domain.Economy;
using SpaceMMO.Domain.Items;
using SpaceMMO.Domain.Universe;
using Xunit;

namespace SpaceMMO.Data.Tests.Market;

/// <summary>
/// Integration tests for escrow and settlement.
/// </summary>
/// <remarks>
/// The conservation tests are the point. Money supply is
/// <c>Σ balances + Σ escrow on open orders</c>, and it may only change by the sales tax and
/// broker fees that were actually charged. Material is conserved absolutely — trading cannot
/// create or destroy a single unit of ore.
/// </remarks>
[Collection(SharedDatabase.Name)]
public sealed class SettlementIntegrationTests(DatabaseFixture fixture) : IAsyncLifetime
{
    private readonly DatabaseFixture _fixture = fixture;

    private const int StartingCredits = 13_000;
    private const int SellerStartingOre = 100;

    private int _stationId;
    private int _itemDefId;
    private int _sellerId;
    private int _buyerId;

    public async Task InitializeAsync()
    {
        await _fixture.ResetAsync();
        await SeedAsync();
    }

    public Task DisposeAsync() => Task.CompletedTask;

    private static Credits Cr(long whole) => Credits.FromWholeCredits(whole);

    // ── Escrow at placement ──────────────────────────────────────────────────

    [Fact]
    public async Task BuyOrder_LocksCreditsAtPlacement()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        // 10 at 250 = 2,500 escrow, plus a 1% broker fee of 25.
        PlaceOrderResult result = await new MarketService(context).PlaceOrderAsync(
            new PlaceOrderRequest(_buyerId, _stationId, _itemDefId, OrderSide.Buy, Cr(250), 10));

        Assert.Equal(Cr(25), result.BrokerFee);
        Assert.Equal(Cr(2_500), result.EscrowRemaining);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Credits balance = await BalanceOfAsync(verify, _buyerId);
        Assert.Equal(Cr(StartingCredits - 2_500 - 25), balance);

        MarketOrder order = await verify.MarketOrders.SingleAsync();
        Assert.Equal(Cr(2_500), order.EscrowedCredits);
        Assert.Equal(0, order.ReservedQuantity);
    }

    [Fact]
    public async Task SellOrder_ReservesGoodsAtPlacement()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        await new MarketService(context).PlaceOrderAsync(
            new PlaceOrderRequest(_sellerId, _stationId, _itemDefId, OrderSide.Sell, Cr(250), 10));

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        // The goods have left the seller's hangar, so they cannot be sold twice.
        Assert.Equal(SellerStartingOre - 10, await OreHeldByAsync(verify, _sellerId));

        MarketOrder order = await verify.MarketOrders.SingleAsync();
        Assert.Equal(10, order.ReservedQuantity);
        Assert.True(order.EscrowedCredits.IsZero);
    }

    [Fact]
    public async Task BuyOrder_WithoutEnoughCredits_IsRejected()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        await Assert.ThrowsAsync<InsufficientFundsException>(() =>
            new MarketService(context).PlaceOrderAsync(new PlaceOrderRequest(
                _buyerId, _stationId, _itemDefId, OrderSide.Buy, Cr(1_000), 100)));

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        // Nothing was written, and the balance is untouched.
        Assert.Equal(0, await verify.MarketOrders.CountAsync());
        Assert.Equal(0, await verify.LedgerEntries.CountAsync());
        Assert.Equal(Cr(StartingCredits), await BalanceOfAsync(verify, _buyerId));
    }

    [Fact]
    public async Task SellOrder_WithoutEnoughGoods_IsRejected()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        await Assert.ThrowsAsync<InsufficientItemsException>(() =>
            new MarketService(context).PlaceOrderAsync(new PlaceOrderRequest(
                _sellerId, _stationId, _itemDefId, OrderSide.Sell, Cr(250), SellerStartingOre + 1)));

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.Equal(0, await verify.MarketOrders.CountAsync());
        Assert.Equal(SellerStartingOre, await OreHeldByAsync(verify, _sellerId));
    }

    // ── Full settlement ──────────────────────────────────────────────────────

    [Fact]
    public async Task CompletedTrade_MovesCreditsAndGoods()
    {
        await PlaceAsync(_sellerId, OrderSide.Sell, price: 250, quantity: 10);
        await PlaceAsync(_buyerId, OrderSide.Buy, price: 250, quantity: 10);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        // Buyer: paid 2,500 for the goods plus a 25 broker fee.
        Assert.Equal(Cr(StartingCredits - 2_500 - 25), await BalanceOfAsync(verify, _buyerId));
        Assert.Equal(10, await OreHeldByAsync(verify, _buyerId));

        // Seller: received 2,500 less 2% tax (50), and paid their own 25 broker fee.
        Assert.Equal(
            Cr(StartingCredits + 2_450 - 25), await BalanceOfAsync(verify, _sellerId));
        Assert.Equal(SellerStartingOre - 10, await OreHeldByAsync(verify, _sellerId));

        Trade trade = await verify.Trades.SingleAsync();
        Assert.Equal(Cr(50), trade.SalesTax);

        // All escrow has been released.
        Assert.True((await SumAsync(verify.MarketOrders.Select(o => o.EscrowedCredits))).IsZero);
    }

    [Fact]
    public async Task BuyerFillingBelowTheirLimit_IsRefundedTheDifference()
    {
        // Seller asks 200; buyer bids 300 for 10. Escrow locks 3,000 but the trade is 2,000, so
        // 1,000 must come back. Without the refund those credits would simply vanish.
        await PlaceAsync(_sellerId, OrderSide.Sell, price: 200, quantity: 10);

        await using SpaceMmoDbContext context = _fixture.CreateContext();
        PlaceOrderResult result = await new MarketService(context).PlaceOrderAsync(
            new PlaceOrderRequest(_buyerId, _stationId, _itemDefId, OrderSide.Buy, Cr(300), 10));

        Assert.True(result.IsFullyFilled);
        Assert.True(result.EscrowRemaining.IsZero);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        // Broker fee is on the order as placed (1% of 3,000 = 30), not on the executed value.
        Assert.Equal(Cr(StartingCredits - 2_000 - 30), await BalanceOfAsync(verify, _buyerId));

        Credits refunded = await SumAsync(verify.LedgerEntries
            .Where(e => e.CharacterId == _buyerId
                && e.Reason == LedgerReason.MarketEscrowReleased)
            .Select(e => e.DeltaCredits));

        Assert.Equal(Cr(1_000), refunded);

        // Tax is on the execution price, not the bid.
        Trade trade = await verify.Trades.SingleAsync();
        Assert.Equal(Cr(40), trade.SalesTax);
    }

    [Fact]
    public async Task PartialFill_SettlesOnlyTheFilledPortion()
    {
        await PlaceAsync(_sellerId, OrderSide.Sell, price: 250, quantity: 4);

        await using SpaceMmoDbContext context = _fixture.CreateContext();
        PlaceOrderResult result = await new MarketService(context).PlaceOrderAsync(
            new PlaceOrderRequest(_buyerId, _stationId, _itemDefId, OrderSide.Buy, Cr(250), 10));

        Assert.Equal(4, result.QuantityFilled);

        // 4 units settled at 250; the remaining 6 stay escrowed at 250 = 1,500.
        Assert.Equal(Cr(1_500), result.EscrowRemaining);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();
        Assert.Equal(4, await OreHeldByAsync(verify, _buyerId));
    }

    // ── Cancellation ─────────────────────────────────────────────────────────

    [Fact]
    public async Task CancellingABuyOrder_ReleasesEscrowButKeepsTheBrokerFee()
    {
        PlaceOrderResult placed = await PlaceAsync(_buyerId, OrderSide.Buy, price: 250, quantity: 10);

        await using SpaceMmoDbContext context = _fixture.CreateContext();
        Assert.True(await new MarketService(context).CancelOrderAsync(placed.OrderId, _buyerId));

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        // Escrow back, fee gone — the fee is what stops order spam being free.
        Assert.Equal(Cr(StartingCredits - 25), await BalanceOfAsync(verify, _buyerId));

        MarketOrder order = await verify.MarketOrders.SingleAsync();
        Assert.True(order.EscrowedCredits.IsZero);
        Assert.NotNull(order.CancelledAt);
        Assert.Equal(0, order.QuantityRemaining);
    }

    [Fact]
    public async Task CancellingASellOrder_ReturnsTheGoods()
    {
        PlaceOrderResult placed = await PlaceAsync(_sellerId, OrderSide.Sell, price: 250, quantity: 10);

        await using SpaceMmoDbContext context = _fixture.CreateContext();
        await new MarketService(context).CancelOrderAsync(placed.OrderId, _sellerId);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.Equal(SellerStartingOre, await OreHeldByAsync(verify, _sellerId));
        Assert.Equal(0, (await verify.MarketOrders.SingleAsync()).ReservedQuantity);
    }

    [Fact]
    public async Task CancellingTwice_IsANoOp()
    {
        PlaceOrderResult placed = await PlaceAsync(_buyerId, OrderSide.Buy, price: 250, quantity: 10);

        await using SpaceMmoDbContext first = _fixture.CreateContext();
        Assert.True(await new MarketService(first).CancelOrderAsync(placed.OrderId, _buyerId));

        await using SpaceMmoDbContext second = _fixture.CreateContext();
        Assert.False(await new MarketService(second).CancelOrderAsync(placed.OrderId, _buyerId));

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        // Critically, escrow was not released twice.
        Assert.Equal(Cr(StartingCredits - 25), await BalanceOfAsync(verify, _buyerId));
    }

    [Fact]
    public async Task CancellingSomeoneElsesOrder_IsRejected()
    {
        PlaceOrderResult placed = await PlaceAsync(_buyerId, OrderSide.Buy, price: 250, quantity: 10);

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        await Assert.ThrowsAsync<UnauthorizedAccessException>(() =>
            new MarketService(context).CancelOrderAsync(placed.OrderId, _sellerId));
    }

    // ── Conservation ─────────────────────────────────────────────────────────

    [Fact]
    public async Task Trading_ConservesMaterialExactly()
    {
        // Ore can only move between hangars and sell-order reservations. A trade must never
        // create or destroy a single unit.
        await PlaceAsync(_sellerId, OrderSide.Sell, price: 250, quantity: 30);
        await PlaceAsync(_buyerId, OrderSide.Buy, price: 250, quantity: 10);
        await PlaceAsync(_buyerId, OrderSide.Buy, price: 250, quantity: 15);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        int inHangars = await verify.InventoryItems
            .Where(i => i.ItemDefId == _itemDefId)
            .SumAsync(i => i.Quantity);

        int reserved = await verify.MarketOrders
            .Where(o => o.ItemDefId == _itemDefId)
            .SumAsync(o => o.ReservedQuantity);

        Assert.Equal(SellerStartingOre, inHangars + reserved);
    }

    [Fact]
    public async Task Trading_ConservesMoneyExceptForFeesAndTax()
    {
        await PlaceAsync(_sellerId, OrderSide.Sell, price: 250, quantity: 20);
        await PlaceAsync(_buyerId, OrderSide.Buy, price: 300, quantity: 10);
        PlaceOrderResult resting = await PlaceAsync(_buyerId, OrderSide.Buy, price: 100, quantity: 5);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Credits balances = await SumAsync(verify.Characters.Select(c => c.Balance));
        Credits escrow = await SumAsync(verify.MarketOrders.Select(o => o.EscrowedCredits));
        Credits tax = await SumAsync(verify.Trades.Select(t => t.SalesTax));

        // Broker fees are recorded as negative deltas, so negate to get the amount destroyed.
        Credits brokerFees = -await SumAsync(verify.LedgerEntries
            .Where(e => e.Reason == LedgerReason.BrokerFee)
            .Select(e => e.DeltaCredits));

        // Everything that started in the economy is still in a balance or in escrow, except the
        // credits the two declared sinks removed.
        Credits expected = Cr(StartingCredits * 2) - tax - brokerFees;

        Assert.Equal(expected, balances + escrow);
        Assert.True(resting.EscrowRemaining.IsPositive);
    }

    [Fact]
    public async Task LedgerReconcilesAgainstEveryBalance()
    {
        // The ledger is authoritative over the cached balance (ADR-0005), so they must agree.
        await PlaceAsync(_sellerId, OrderSide.Sell, price: 250, quantity: 20);
        await PlaceAsync(_buyerId, OrderSide.Buy, price: 300, quantity: 10);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        foreach (Character character in await verify.Characters.ToListAsync())
        {
            int characterId = character.Id;

            Credits ledgerSum = await SumAsync(verify.LedgerEntries
                .Where(e => e.CharacterId == characterId)
                .Select(e => e.DeltaCredits));

            Assert.Equal(Cr(StartingCredits) + ledgerSum, character.Balance);
        }
    }

    [Fact]
    public async Task EveryLedgerEntry_HasAClassifiedReason()
    {
        await PlaceAsync(_sellerId, OrderSide.Sell, price: 250, quantity: 10);
        await PlaceAsync(_buyerId, OrderSide.Buy, price: 300, quantity: 10);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        List<LedgerEntry> entries = await verify.LedgerEntries.ToListAsync();

        Assert.NotEmpty(entries);

        foreach (LedgerEntry entry in entries)
        {
            // Throws for an unclassified reason, which is what makes faucet and sink
            // attribution trustworthy.
            LedgerReasonKind kind = LedgerReasons.KindOf(entry.Reason);

            Assert.InRange(kind, LedgerReasonKind.Faucet, LedgerReasonKind.Transfer);
            Assert.NotNull(entry.ReferenceId);
            Assert.False(entry.DeltaCredits.IsZero);
        }
    }

    [Fact]
    public async Task Trading_CreatesNoCredits()
    {
        // No market operation may increase the money supply. Every ledger reason written by the
        // market must be a transfer or a sink, never a faucet.
        await PlaceAsync(_sellerId, OrderSide.Sell, price: 250, quantity: 10);
        await PlaceAsync(_buyerId, OrderSide.Buy, price: 300, quantity: 10);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        List<LedgerReason> reasons = await verify.LedgerEntries
            .Select(e => e.Reason)
            .Distinct()
            .ToListAsync();

        Assert.DoesNotContain(LedgerReasonKind.Faucet, reasons.Select(LedgerReasons.KindOf));
    }

    // ── Helpers ──────────────────────────────────────────────────────────────

    private async Task<PlaceOrderResult> PlaceAsync(
        int characterId, OrderSide side, long price, int quantity)
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        return await new MarketService(context).PlaceOrderAsync(
            new PlaceOrderRequest(characterId, _stationId, _itemDefId, side, Cr(price), quantity));
    }

    /// <summary>
    /// Totals a column of <see cref="Credits"/> in memory.
    /// </summary>
    /// <remarks>
    /// EF cannot translate <c>Sum(x =&gt; x.Amount.MinorUnits)</c> because the value converter
    /// makes the inside of <see cref="Credits"/> invisible to SQL. Projecting the column and
    /// adding client-side keeps the arithmetic in the type that guarantees it is exact, which is
    /// the point of the type in the first place.
    /// </remarks>
    private static async Task<Credits> SumAsync(IQueryable<Credits> query)
    {
        List<Credits> amounts = await query.ToListAsync();

        Credits total = Credits.Zero;

        foreach (Credits amount in amounts)
        {
            total += amount;
        }

        return total;
    }

    private static async Task<Credits> BalanceOfAsync(SpaceMmoDbContext context, int characterId) =>
        (await context.Characters.SingleAsync(c => c.Id == characterId)).Balance;

    private async Task<int> OreHeldByAsync(SpaceMmoDbContext context, int characterId) =>
        await context.InventoryItems
            .Where(i => i.ItemDefId == _itemDefId
                && context.Inventories.Any(inv => inv.Id == i.InventoryId
                    && inv.CharacterId == characterId))
            .SumAsync(i => i.Quantity);

    private async Task SeedAsync()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        var system = new StarSystem
        {
            Key = "system_origin",
            Name = "Origin",
            Seed = 42,
            GeneratorVersion = 1,
            SecurityLevel = SecurityLevel.Secure,
        };
        context.StarSystems.Add(system);
        await context.SaveChangesAsync();

        var body = new Body
        {
            Key = "body_terra",
            Name = "Terra",
            StarSystemId = system.Id,
            Kind = BodyKind.Planet,
            SecurityLevel = SecurityLevel.Secure,
            RadiusKm = 637.1,
        };
        context.Bodies.Add(body);

        var itemDef = new ItemDef
        {
            Key = "ferrite_ore",
            Name = "Ferrite Ore",
            Category = ItemCategory.Raw,
            VolumeM3 = 0.4,
        };
        context.ItemDefs.Add(itemDef);
        await context.SaveChangesAsync();

        var station = new Station
        {
            Key = "station_terra_hub",
            Name = "Terra Trading Hub",
            StarSystemId = system.Id,
            BodyId = body.Id,
            Kind = StationKind.TradingHub,
        };
        context.Stations.Add(station);

        var account = new Account
        {
            Email = "settlement@example.com",
            PasswordHash = "not-a-real-hash",
            CreatedAt = DateTimeOffset.UtcNow,
        };
        context.Accounts.Add(account);
        await context.SaveChangesAsync();

        var seller = new Character
        {
            AccountId = account.Id,
            Name = "Seller",
            Race = Race.Humanoid,
            HomeBodyId = body.Id,
            Balance = Cr(StartingCredits),
            CreatedAt = DateTimeOffset.UtcNow,
        };

        var buyer = new Character
        {
            AccountId = account.Id,
            Name = "Buyer",
            Race = Race.Martian,
            HomeBodyId = body.Id,
            Balance = Cr(StartingCredits),
            CreatedAt = DateTimeOffset.UtcNow,
        };

        context.Characters.AddRange(seller, buyer);
        await context.SaveChangesAsync();

        // Give the seller something to sell.
        var hangar = new Inventory
        {
            CharacterId = seller.Id,
            StationId = station.Id,
            Kind = InventoryKind.StationHangar,
            CapacityM3 = 0,
        };
        context.Inventories.Add(hangar);
        await context.SaveChangesAsync();

        context.InventoryItems.Add(new InventoryItem
        {
            InventoryId = hangar.Id,
            ItemDefId = itemDef.Id,
            Quantity = SellerStartingOre,
        });
        await context.SaveChangesAsync();

        _stationId = station.Id;
        _itemDefId = itemDef.Id;
        _sellerId = seller.Id;
        _buyerId = buyer.Id;
    }
}
