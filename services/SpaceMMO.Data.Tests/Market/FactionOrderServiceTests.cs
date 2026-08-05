using Microsoft.EntityFrameworkCore;
using SpaceMMO.Data;
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
/// The faucet of last resort: turning gathered material into credits with nothing paid up front.
/// </summary>
/// <remarks>
/// The reason this exists is that a character at zero credits could not act. Jobs charge a fee, and
/// placing a sell order charges a broker fee, so a player who spent to nothing could gather forever
/// and never get back. These tests care most about the two ways that guarantee could be broken:
/// paying nothing while taking the goods, and letting the faucet become an income.
/// </remarks>
[Collection(SharedDatabase.Name)]
public sealed class FactionOrderServiceTests(DatabaseFixture fixture) : IAsyncLifetime
{
    private readonly DatabaseFixture _fixture = fixture;

    private int _characterId;
    private int _stationId;
    private int _oreId;
    private int _plateId;

    public async Task InitializeAsync()
    {
        await _fixture.ResetAsync();
        await SeedAsync();
    }

    public Task DisposeAsync() => Task.CompletedTask;

    [Fact]
    public async Task Selling_ore_pays_credits_and_destroys_the_material()
    {
        await GiveOreAsync(50);

        await using SpaceMmoDbContext context = _fixture.CreateContext();
        var service = new FactionOrderService(context);

        FactionSaleResult result = await service.SellAsync(_characterId, _stationId, _oreId, 20);

        Assert.Equal(20, result.QuantitySold);
        Assert.Equal(Credits.FromWholeCredits(40), result.Paid);
        Assert.False(result.WasCapped);

        await using SpaceMmoDbContext check = _fixture.CreateContext();

        Character character = check.Characters.Single(c => c.Id == _characterId);

        Assert.Equal(Credits.FromWholeCredits(40), character.Balance);

        // The material is gone, not moved. This is a sink as well as a faucet: raw material
        // otherwise only ever accumulates.
        InventoryItem stack = check.InventoryItems
            .Single(i => i.Inventory!.CharacterId == _characterId && i.ItemDefId == _oreId);

        Assert.Equal(30, stack.Quantity);
    }

    [Fact]
    public async Task The_sale_is_recorded_as_a_capped_faucet()
    {
        await GiveOreAsync(10);

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        await new FactionOrderService(context).SellAsync(_characterId, _stationId, _oreId, 10);

        await using SpaceMmoDbContext check = _fixture.CreateContext();

        LedgerEntry entry = check.LedgerEntries.Single(e => e.CharacterId == _characterId);

        Assert.Equal(LedgerReason.FactionPurchase, entry.Reason);
        Assert.Equal(Credits.FromWholeCredits(20), entry.DeltaCredits);

        // Written to the same daily row sidequests use. A second counter would let a player take
        // both budgets in full and double the rate the economy was balanced for.
        CharacterFaucetDaily daily =
            check.CharacterFaucetDailies.Single(d => d.CharacterId == _characterId);

        Assert.Equal(Credits.FromWholeCredits(20), daily.CreditsGranted);
    }

    [Fact]
    public async Task At_the_daily_cap_nothing_is_sold_and_nothing_is_taken()
    {
        await GiveOreAsync(50);
        await ExhaustDailyBudgetAsync();

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        FactionSaleResult result =
            await new FactionOrderService(context).SellAsync(_characterId, _stationId, _oreId, 20);

        Assert.Equal(0, result.QuantitySold);
        Assert.True(result.WasCapped);

        await using SpaceMmoDbContext check = _fixture.CreateContext();

        // The critical half. Taking the ore and withholding the credits would be theft, and it is
        // the failure a naive cap check produces: refuse the payment, forget to refuse the removal.
        InventoryItem stack = check.InventoryItems
            .Single(i => i.Inventory!.CharacterId == _characterId && i.ItemDefId == _oreId);

        Assert.Equal(50, stack.Quantity);
    }

    [Fact]
    public async Task A_partial_budget_sells_only_what_it_can_pay_for()
    {
        await GiveOreAsync(50);

        // Room for exactly five units at 2 cr each.
        await ExhaustDailyBudgetAsync(FaucetBudget.DefaultDailyCap - Credits.FromWholeCredits(10));

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        FactionSaleResult result =
            await new FactionOrderService(context).SellAsync(_characterId, _stationId, _oreId, 20);

        // Reduced rather than refused: a player at the cap can still sell the one unit they need,
        // and the rest stays theirs rather than being bought at a discount they did not agree to.
        Assert.Equal(5, result.QuantitySold);
        Assert.Equal(Credits.FromWholeCredits(10), result.Paid);
        Assert.True(result.WasCapped);

        await using SpaceMmoDbContext check = _fixture.CreateContext();

        InventoryItem stack = check.InventoryItems
            .Single(i => i.Inventory!.CharacterId == _characterId && i.ItemDefId == _oreId);

        Assert.Equal(45, stack.Quantity);
    }

    [Fact]
    public async Task A_manufactured_good_has_no_standing_order()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        // Refined goods are what players are meant to compete on. A standing bid on them would put
        // a floor under the market this game is built around.
        await Assert.ThrowsAsync<NotBoughtByFactionException>(
            () => new FactionOrderService(context).SellAsync(_characterId, _stationId, _plateId, 1));
    }

    [Fact]
    public async Task Selling_more_than_is_held_is_refused()
    {
        await GiveOreAsync(3);

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        await Assert.ThrowsAsync<InsufficientItemsException>(
            () => new FactionOrderService(context).SellAsync(_characterId, _stationId, _oreId, 10));

        await using SpaceMmoDbContext check = _fixture.CreateContext();

        // No credits created for a sale that did not happen.
        Assert.Equal(Credits.Zero, check.Characters.Single(c => c.Id == _characterId).Balance);
        Assert.Empty(check.LedgerEntries.Where(e => e.CharacterId == _characterId));
    }

    private async Task GiveOreAsync(int quantity)
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        var hangar = new Inventory
        {
            CharacterId = _characterId,
            StationId = _stationId,
            Kind = InventoryKind.StationHangar,
            CapacityM3 = 0,
        };

        context.Inventories.Add(hangar);
        await context.SaveChangesAsync();

        context.InventoryItems.Add(new InventoryItem
        {
            InventoryId = hangar.Id,
            ItemDefId = _oreId,
            Quantity = quantity,
            CostBasis = Credits.Zero,
        });

        await context.SaveChangesAsync();
    }

    private async Task ExhaustDailyBudgetAsync(Credits? granted = null)
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        context.CharacterFaucetDailies.Add(new CharacterFaucetDaily
        {
            CharacterId = _characterId,
            UtcDate = DateOnly.FromDateTime(DateTimeOffset.UtcNow.UtcDateTime),
            CreditsGranted = granted ?? FaucetBudget.DefaultDailyCap,
        });

        await context.SaveChangesAsync();
    }

    private async Task SeedAsync()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        var system = new StarSystem
        {
            Key = "system_origin",
            Name = "Origin",
            Seed = 1,
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
        await context.SaveChangesAsync();

        var station = new Station
        {
            Key = "station_terra_hub",
            Name = "Terra Outpost",
            StarSystemId = system.Id,
            BodyId = body.Id,
            Kind = StationKind.TradingHub,
        };

        var account = new Account
        {
            Email = "faction@example.com",
            PasswordHash = "not-a-real-hash",
            CreatedAt = DateTimeOffset.UtcNow,
        };

        var ore = new ItemDef
        {
            Key = "ferrite_ore",
            Name = "Ferrite Ore",
            Category = ItemCategory.Raw,
            VolumeM3 = 0.4,
            FactionBuyPrice = Credits.FromWholeCredits(2),
        };

        var plate = new ItemDef
        {
            Key = "ferrite_plate",
            Name = "Ferrite Plate",
            Category = ItemCategory.Refined,
            VolumeM3 = 0.2,
        };

        context.Stations.Add(station);
        context.Accounts.Add(account);
        context.ItemDefs.AddRange(ore, plate);
        await context.SaveChangesAsync();

        var character = new Character
        {
            AccountId = account.Id,
            Name = "Prospector",
            Race = Race.Humanoid,
            HomeBodyId = body.Id,
            CreatedAt = DateTimeOffset.UtcNow,
        };

        context.Characters.Add(character);
        await context.SaveChangesAsync();

        _characterId = character.Id;
        _stationId = station.Id;
        _oreId = ore.Id;
        _plateId = plate.Id;
    }
}
