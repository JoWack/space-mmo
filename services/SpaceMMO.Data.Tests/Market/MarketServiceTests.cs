using Microsoft.EntityFrameworkCore;
using SpaceMMO.Data.Entities;
using SpaceMMO.Data.Market;
using SpaceMMO.Domain.Characters;
using SpaceMMO.Domain.Economy;
using SpaceMMO.Domain.Items;
using SpaceMMO.Domain.Universe;
using Xunit;

namespace SpaceMMO.Data.Tests.Market;

/// <summary>
/// Integration tests for order placement against a real Postgres instance.
/// </summary>
/// <remarks>
/// Matching logic itself is covered exhaustively by the unit tests in
/// <c>SpaceMMO.Domain.Tests</c>. These tests exist for the things a database is required to
/// prove: that placement is atomic, that <c>FOR UPDATE</c> actually serialises competing
/// placements, and that concurrent buyers cannot consume the same resting quantity twice.
/// </remarks>
[Collection(SharedDatabase.Name)]
public sealed class MarketServiceTests(DatabaseFixture fixture) : IAsyncLifetime
{
    private readonly DatabaseFixture _fixture = fixture;

    private int _stationId;
    private int _itemDefId;
    private int _sellerId;
    private int _buyerId;
    private int _secondBuyerId;

    public async Task InitializeAsync()
    {
        await _fixture.ResetAsync();
        await SeedAsync();
    }

    public Task DisposeAsync() => Task.CompletedTask;

    private static Credits Cr(long whole) => Credits.FromWholeCredits(whole);

    // ── Basic behaviour through the real stack ───────────────────────────────

    [Fact]
    public async Task PlaceOrder_WithNoCrossingOrders_RestsOnTheBook()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();
        var service = new MarketService(context);

        PlaceOrderResult result = await service.PlaceOrderAsync(
            new PlaceOrderRequest(_sellerId, _stationId, _itemDefId, OrderSide.Sell, Cr(250), 10));

        Assert.Equal(0, result.QuantityFilled);
        Assert.Equal(10, result.QuantityResting);
        Assert.False(result.IsFullyFilled);
        Assert.Empty(result.TradeIds);
    }

    [Fact]
    public async Task PlaceOrder_CrossingARestingOrder_FillsAndRecordsATrade()
    {
        await RestOrderAsync(_sellerId, OrderSide.Sell, price: 250, quantity: 10);

        await using SpaceMmoDbContext context = _fixture.CreateContext();
        var service = new MarketService(context);

        PlaceOrderResult result = await service.PlaceOrderAsync(
            new PlaceOrderRequest(_buyerId, _stationId, _itemDefId, OrderSide.Buy, Cr(250), 10));

        Assert.Equal(10, result.QuantityFilled);
        Assert.Equal(0, result.QuantityResting);
        Assert.True(result.IsFullyFilled);
        Assert.Single(result.TradeIds);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();
        Trade trade = await verify.Trades.SingleAsync();

        Assert.Equal(10, trade.Quantity);
        Assert.Equal(Cr(250), trade.Price);
        Assert.Equal(_buyerId, trade.BuyerCharacterId);
        Assert.Equal(_sellerId, trade.SellerCharacterId);

        // 2% of 2,500 cr.
        Assert.Equal(Cr(50), trade.SalesTax);

        // Both sides of the trade must reference real orders, not a placeholder zero.
        Assert.NotEqual(0L, trade.BuyOrderId);
        Assert.NotEqual(0L, trade.SellOrderId);
    }

    [Fact]
    public async Task PlaceOrder_FillingCompletely_StillPersistsTheIncomingOrder()
    {
        // Regression: an order that filled instantly used to leave no row, so its trades
        // pointed at order id 0 — a dangling reference in exactly the records that matter most
        // for auditing. Every trade must resolve to two real orders.
        await RestOrderAsync(_sellerId, OrderSide.Sell, price: 250, quantity: 10);

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        PlaceOrderResult result = await new MarketService(context).PlaceOrderAsync(
            new PlaceOrderRequest(_buyerId, _stationId, _itemDefId, OrderSide.Buy, Cr(250), 10));

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        MarketOrder taker = await verify.MarketOrders.SingleAsync(o => o.Id == result.OrderId);
        Assert.Equal(10, taker.QuantityOriginal);
        Assert.Equal(0, taker.QuantityRemaining);

        Trade trade = await verify.Trades.SingleAsync();
        Assert.Equal(result.OrderId, trade.BuyOrderId);

        // Both referenced orders must actually exist.
        var orderIds = await verify.MarketOrders.Select(o => o.Id).ToListAsync();
        Assert.Contains(trade.BuyOrderId, orderIds);
        Assert.Contains(trade.SellOrderId, orderIds);
    }

    [Fact]
    public async Task PlaceOrder_PartiallyFilling_RestsTheRemainderAndLinksBothOrders()
    {
        await RestOrderAsync(_sellerId, OrderSide.Sell, price: 250, quantity: 4);

        await using SpaceMmoDbContext context = _fixture.CreateContext();
        var service = new MarketService(context);

        PlaceOrderResult result = await service.PlaceOrderAsync(
            new PlaceOrderRequest(_buyerId, _stationId, _itemDefId, OrderSide.Buy, Cr(250), 10));

        Assert.Equal(4, result.QuantityFilled);
        Assert.Equal(6, result.QuantityResting);
        Assert.False(result.IsFullyFilled);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();
        Trade trade = await verify.Trades.SingleAsync();

        // The buy side is backfilled after the resting order gets its identity.
        Assert.Equal(result.OrderId, trade.BuyOrderId);

        MarketOrder resting = await verify.MarketOrders.SingleAsync(o => o.Id == result.OrderId);
        Assert.Equal(6, resting.QuantityRemaining);
    }

    [Fact]
    public async Task PlaceOrder_DecrementsTheRestingOrder()
    {
        long sellOrderId = await RestOrderAsync(_sellerId, OrderSide.Sell, price: 250, quantity: 10);

        await using SpaceMmoDbContext context = _fixture.CreateContext();
        await new MarketService(context).PlaceOrderAsync(
            new PlaceOrderRequest(_buyerId, _stationId, _itemDefId, OrderSide.Buy, Cr(250), 3));

        await using SpaceMmoDbContext verify = _fixture.CreateContext();
        MarketOrder sellOrder = await verify.MarketOrders.SingleAsync(o => o.Id == sellOrderId);

        Assert.Equal(7, sellOrder.QuantityRemaining);
        Assert.Equal(10, sellOrder.QuantityOriginal);
    }

    [Fact]
    public async Task PlaceOrder_DoesNotMatchTheSameCharactersOwnOrder()
    {
        await RestOrderAsync(_sellerId, OrderSide.Sell, price: 250, quantity: 10);

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        PlaceOrderResult result = await new MarketService(context).PlaceOrderAsync(
            new PlaceOrderRequest(_sellerId, _stationId, _itemDefId, OrderSide.Buy, Cr(250), 10));

        Assert.Equal(0, result.QuantityFilled);
        Assert.Empty(result.TradeIds);
    }

    [Fact]
    public async Task PlaceOrder_IgnoresExpiredOrders()
    {
        await RestOrderAsync(
            _sellerId, OrderSide.Sell, price: 250, quantity: 10, expiresInDays: -1);

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        PlaceOrderResult result = await new MarketService(context).PlaceOrderAsync(
            new PlaceOrderRequest(_buyerId, _stationId, _itemDefId, OrderSide.Buy, Cr(250), 10));

        Assert.Equal(0, result.QuantityFilled);
    }

    [Fact]
    public async Task PlaceOrder_IgnoresCancelledOrders()
    {
        long orderId = await RestOrderAsync(_sellerId, OrderSide.Sell, price: 250, quantity: 10);

        await using (SpaceMmoDbContext cancel = _fixture.CreateContext())
        {
            MarketOrder order = await cancel.MarketOrders.SingleAsync(o => o.Id == orderId);
            order.CancelledAt = DateTimeOffset.UtcNow;
            await cancel.SaveChangesAsync();
        }

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        PlaceOrderResult result = await new MarketService(context).PlaceOrderAsync(
            new PlaceOrderRequest(_buyerId, _stationId, _itemDefId, OrderSide.Buy, Cr(250), 10));

        Assert.Equal(0, result.QuantityFilled);
    }

    [Fact]
    public async Task PlaceOrder_TakesTheCheapestAskFirst_ThroughTheRealQuery()
    {
        // Confirms the raw FOR UPDATE query loads the whole crossing set, leaving priority to
        // the pure engine rather than to whatever order Postgres returns rows in.
        await RestOrderAsync(_sellerId, OrderSide.Sell, price: 300, quantity: 5);
        await RestOrderAsync(_secondBuyerId, OrderSide.Sell, price: 200, quantity: 5);

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        await new MarketService(context).PlaceOrderAsync(
            new PlaceOrderRequest(_buyerId, _stationId, _itemDefId, OrderSide.Buy, Cr(300), 5));

        await using SpaceMmoDbContext verify = _fixture.CreateContext();
        Trade trade = await verify.Trades.SingleAsync();

        Assert.Equal(Cr(200), trade.Price);
    }

    // ── The reason this project exists ───────────────────────────────────────

    [Fact]
    public async Task TwoConcurrentBuyers_CannotOverfillOneSellOrder()
    {
        // The bug this guards against creates items from nothing: both buyers read "10
        // available", both are told they bought 10, and 20 units enter the economy from a
        // 10-unit order. FOR UPDATE is what makes the second placement wait and then re-read.
        long sellOrderId = await RestOrderAsync(_sellerId, OrderSide.Sell, price: 250, quantity: 10);

        await using SpaceMmoDbContext firstContext = _fixture.CreateContext();
        await using SpaceMmoDbContext secondContext = _fixture.CreateContext();

        var firstService = new MarketService(firstContext);
        var secondService = new MarketService(secondContext);

        var request = new PlaceOrderRequest(
            _buyerId, _stationId, _itemDefId, OrderSide.Buy, Cr(250), 10);

        var secondRequest = request with { CharacterId = _secondBuyerId };

        // Genuinely parallel, on separate connections.
        Task<PlaceOrderResult> first = Task.Run(() => firstService.PlaceOrderAsync(request));
        Task<PlaceOrderResult> second = Task.Run(() => secondService.PlaceOrderAsync(secondRequest));

        PlaceOrderResult[] results = await Task.WhenAll(first, second);

        int totalFilled = results.Sum(r => r.QuantityFilled);

        Assert.Equal(10, totalFilled);

        // One buyer got everything; the other got nothing and rested its full quantity.
        Assert.Contains(results, r => r.QuantityFilled == 10);
        Assert.Contains(results, r => r.QuantityFilled == 0 && r.QuantityResting == 10);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        MarketOrder sellOrder = await verify.MarketOrders.SingleAsync(o => o.Id == sellOrderId);
        Assert.Equal(0, sellOrder.QuantityRemaining);

        int tradedQuantity = await verify.Trades.SumAsync(t => t.Quantity);
        Assert.Equal(10, tradedQuantity);
    }

    [Fact]
    public async Task ManyConcurrentBuyers_TradeExactlyTheAvailableQuantity()
    {
        // Six buyers chasing 12 units in 4-unit lots: three should fill, three should rest.
        // Any total other than 12 is either duplication or loss.
        long sellOrderId = await RestOrderAsync(_sellerId, OrderSide.Sell, price: 250, quantity: 12);

        var contexts = new List<SpaceMmoDbContext>();
        var tasks = new List<Task<PlaceOrderResult>>();

        try
        {
            for (int i = 0; i < 6; i++)
            {
                SpaceMmoDbContext context = _fixture.CreateContext();
                contexts.Add(context);

                var service = new MarketService(context);
                var request = new PlaceOrderRequest(
                    _buyerId, _stationId, _itemDefId, OrderSide.Buy, Cr(250), 4);

                tasks.Add(Task.Run(() => service.PlaceOrderAsync(
                    request with { CharacterId = _buyerId })));
            }

            PlaceOrderResult[] results = await Task.WhenAll(tasks);

            Assert.Equal(12, results.Sum(r => r.QuantityFilled));
            Assert.Equal(3, results.Count(r => r.QuantityFilled == 4));
            Assert.Equal(3, results.Count(r => r.QuantityFilled == 0));
        }
        finally
        {
            foreach (SpaceMmoDbContext context in contexts)
            {
                await context.DisposeAsync();
            }
        }

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        MarketOrder sellOrder = await verify.MarketOrders.SingleAsync(o => o.Id == sellOrderId);
        Assert.Equal(0, sellOrder.QuantityRemaining);
        Assert.Equal(12, await verify.Trades.SumAsync(t => t.Quantity));
    }

    [Fact]
    public async Task FailedPlacement_LeavesNoPartialState()
    {
        // A rolled-back transaction must not leave a trade without its order, or an order
        // whose quantity was decremented for a trade that never happened.
        await using SpaceMmoDbContext context = _fixture.CreateContext();
        var service = new MarketService(context);

        await Assert.ThrowsAsync<ArgumentOutOfRangeException>(() => service.PlaceOrderAsync(
            new PlaceOrderRequest(_buyerId, _stationId, _itemDefId, OrderSide.Buy, Cr(250), 0)));

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.Equal(0, await verify.MarketOrders.CountAsync());
        Assert.Equal(0, await verify.Trades.CountAsync());
    }

    // ── Seeding ──────────────────────────────────────────────────────────────

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
            Key = "ferrite_plate",
            Name = "Ferrite Plate",
            Category = ItemCategory.Refined,
            VolumeM3 = 0.2,
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
            Email = "test@example.com",
            PasswordHash = "not-a-real-hash",
            CreatedAt = DateTimeOffset.UtcNow,
        };
        context.Accounts.Add(account);
        await context.SaveChangesAsync();

        Character seller = NewCharacter(account.Id, body.Id, "Seller");
        Character buyer = NewCharacter(account.Id, body.Id, "Buyer");
        Character secondBuyer = NewCharacter(account.Id, body.Id, "SecondBuyer");

        context.Characters.AddRange(seller, buyer, secondBuyer);
        await context.SaveChangesAsync();

        _stationId = station.Id;
        _itemDefId = itemDef.Id;
        _sellerId = seller.Id;
        _buyerId = buyer.Id;
        _secondBuyerId = secondBuyer.Id;

        // Anyone who might place a sell order needs goods to reserve, since sell orders now move
        // items out of the hangar at placement.
        foreach (int characterId in new[] { seller.Id, secondBuyer.Id })
        {
            var hangar = new Inventory
            {
                CharacterId = characterId,
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
                Quantity = 1_000,
            });
        }

        await context.SaveChangesAsync();
    }

    private static Character NewCharacter(int accountId, int homeBodyId, string name) => new()
    {
        AccountId = accountId,
        Name = name,
        Race = Race.Humanoid,
        HomeBodyId = homeBodyId,
        Balance = Credits.FromWholeCredits(13_000),
        CreatedAt = DateTimeOffset.UtcNow,
    };

    /// <summary>
    /// Puts an order on the book through the real service.
    /// </summary>
    /// <remarks>
    /// This used to insert rows directly, which was faster to write but produced orders that
    /// could not exist in production — a sell order with no reserved goods, promising items its
    /// owner did not hold. The <c>reserved_quantity &gt;= 0</c> check constraint caught it as soon
    /// as a fill tried to decrement below zero. Going through the service keeps test setup and
    /// production on the same path, so invalid states cannot be constructed in the first place.
    /// </remarks>
    private async Task<long> RestOrderAsync(
        int characterId, OrderSide side, long price, int quantity, int expiresInDays = 30)
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        PlaceOrderResult result = await new MarketService(context).PlaceOrderAsync(
            new PlaceOrderRequest(
                characterId, _stationId, _itemDefId, side, Cr(price), quantity, expiresInDays));

        return result.OrderId;
    }
}
