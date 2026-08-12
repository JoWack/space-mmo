using Microsoft.EntityFrameworkCore;
using SpaceMMO.Data.Entities;
using SpaceMMO.Data.Inventories;
using SpaceMMO.Domain.Characters;
using SpaceMMO.Domain.Economy;
using SpaceMMO.Domain.Items;
using SpaceMMO.Domain.Universe;
using Xunit;

namespace SpaceMMO.Data.Tests.Inventories;

/// <summary>
/// Integration tests for moving goods between a character's own containers.
/// </summary>
/// <remarks>
/// Until this existed, everything gathered or crafted landed in a station hangar and stayed there:
/// <see cref="InventoryKind.CharacterCarried"/> and <see cref="InventoryKind.ShipHold"/> were enum
/// values nothing created or filled. So the tests worth having are the two that would be silently
/// wrong — cost basis vanishing on the way across, and goods crossing between two people — rather
/// than the happy path, which is visible the moment anybody tries it.
/// </remarks>
[Collection(SharedDatabase.Name)]
public sealed class InventoryTransferTests(DatabaseFixture fixture) : IAsyncLifetime
{
    private readonly DatabaseFixture _fixture = fixture;

    private int _oreId;
    private int _laserId;
    private long _hangarId;
    private long _holdId;
    private long _strangersHangarId;

    public async Task InitializeAsync()
    {
        await _fixture.ResetAsync();
        await SeedAsync();
    }

    public Task DisposeAsync() => Task.CompletedTask;

    private static InventoryService Service(SpaceMmoDbContext context) => new(context);

    [Fact]
    public async Task Goods_move_between_a_characters_own_containers()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        await Service(context).AddAsync(_hangarId, _oreId, 30, Credits.FromWholeCredits(300));
        await context.SaveChangesAsync();

        await Service(context).TransferAsync(_hangarId, _holdId, _oreId, 10);
        await context.SaveChangesAsync();

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.Equal(20, await Service(verify).QuantityOfAsync(_hangarId, _oreId));
        Assert.Equal(10, await Service(verify).QuantityOfAsync(_holdId, _oreId));
    }

    [Fact]
    public async Task Cost_basis_travels_with_the_goods_and_still_sums()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        // Thirty units for three hundred credits: ten credits each, and a third of the stack.
        await Service(context).AddAsync(_hangarId, _oreId, 30, Credits.FromWholeCredits(300));
        await context.SaveChangesAsync();

        await Service(context).TransferAsync(_hangarId, _holdId, _oreId, 10);
        await context.SaveChangesAsync();

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Credits stayed = await CostBasisAsync(verify, _hangarId);
        Credits moved = await CostBasisAsync(verify, _holdId);

        // Insurance pays against acquisition value (ADR-0006), so material that arrives having
        // apparently cost nothing is material that pays out nothing when it is lost. Moving goods
        // must neither create value nor destroy it.
        Assert.Equal(Credits.FromWholeCredits(100), moved);
        Assert.Equal(Credits.FromWholeCredits(200), stayed);
        Assert.Equal(Credits.FromWholeCredits(300), stayed + moved);
    }

    [Fact]
    public async Task A_tool_moves_as_itself_and_keeps_its_condition()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        var laser = new ItemInstance
        {
            ItemDefId = _laserId,
            InventoryId = _hangarId,
            Condition = 63,
            AcquisitionValue = Credits.FromWholeCredits(250),
            CreatedAt = DateTimeOffset.UtcNow,
        };

        context.ItemInstances.Add(laser);
        await context.SaveChangesAsync();

        await Service(context).TransferInstanceAsync(laser.Id, _holdId);
        await context.SaveChangesAsync();

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        ItemInstance moved = await verify.ItemInstances.SingleAsync(i => i.Id == laser.Id);

        Assert.Equal(_holdId, moved.InventoryId);

        // An instance is not split, summed or recreated — it changes container and stays itself,
        // which is the whole reason it is a different operation from moving a stack.
        Assert.Equal(63, moved.Condition);
        Assert.Equal(Credits.FromWholeCredits(250), moved.AcquisitionValue);
    }

    [Fact]
    public async Task Goods_cannot_cross_to_another_character()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        await Service(context).AddAsync(_hangarId, _oreId, 5, Credits.FromWholeCredits(50));
        await context.SaveChangesAsync();

        // Giving items away is a trade, and a trade is the market's job: it has fees, an order book
        // and a settlement path. A transfer that crossed owners would be a way around all three.
        await Assert.ThrowsAsync<InventoryTransferException>(() =>
            Service(context).TransferAsync(_hangarId, _strangersHangarId, _oreId, 5));
    }

    [Fact]
    public async Task A_destroyed_instance_cannot_be_recovered_by_moving_it()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        var wreck = new ItemInstance
        {
            ItemDefId = _laserId,
            InventoryId = null,
            Condition = 0,
            AcquisitionValue = Credits.FromWholeCredits(250),
            CreatedAt = DateTimeOffset.UtcNow,
            DestroyedAt = DateTimeOffset.UtcNow,
        };

        context.ItemInstances.Add(wreck);
        await context.SaveChangesAsync();

        // The row survives destruction so history does (ADR-0006). It must not be a way back.
        await Assert.ThrowsAsync<InventoryTransferException>(() =>
            Service(context).TransferInstanceAsync(wreck.Id, _holdId));
    }

    [Fact]
    public async Task Moving_more_than_is_held_refuses()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        await Service(context).AddAsync(_hangarId, _oreId, 3, Credits.FromWholeCredits(30));
        await context.SaveChangesAsync();

        await Assert.ThrowsAsync<InsufficientItemsException>(() =>
            Service(context).TransferAsync(_hangarId, _holdId, _oreId, 4));
    }

    private static async Task<Credits> CostBasisAsync(SpaceMmoDbContext context, long inventoryId) =>
        await context.InventoryItems
            .Where(i => i.InventoryId == inventoryId)
            .Select(i => i.CostBasis)
            .SingleAsync();

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
        await context.SaveChangesAsync();

        var station = new Station
        {
            Key = "station_terra_hub",
            Name = "Terra Outpost",
            StarSystemId = system.Id,
            BodyId = body.Id,
            Kind = StationKind.TradingHub,
            DirectionX = -1.0,
            DockingRangeKilometres = 5.0,
        };

        context.Stations.Add(station);

        var ore = new ItemDef
        {
            Key = "ferrite_ore", Name = "Ferrite Ore", Category = ItemCategory.Raw, VolumeM3 = 0.4,
        };

        var laser = new ItemDef
        {
            Key = "crude_mining_laser", Name = "Crude Mining Laser",
            Category = ItemCategory.Tool, VolumeM3 = 2,
        };

        context.ItemDefs.AddRange(ore, laser);
        await context.SaveChangesAsync();

        var account = new Account
        {
            Email = "hauler@local.test",
            PasswordHash = "x",
            CreatedAt = DateTimeOffset.UtcNow,
        };

        context.Accounts.Add(account);
        await context.SaveChangesAsync();

        var owner = new Character
        {
            AccountId = account.Id,
            Name = "Hauler",
            Race = Race.Humanoid,
            HomeBodyId = body.Id,
            Balance = Credits.FromWholeCredits(1_000),
            CreatedAt = DateTimeOffset.UtcNow,
        };

        var stranger = new Character
        {
            AccountId = account.Id,
            Name = "Stranger",
            Race = Race.Humanoid,
            HomeBodyId = body.Id,
            Balance = Credits.FromWholeCredits(1_000),
            CreatedAt = DateTimeOffset.UtcNow,
        };

        context.Characters.AddRange(owner, stranger);
        await context.SaveChangesAsync();

        var hangar = new Inventory
        {
            CharacterId = owner.Id,
            Kind = InventoryKind.StationHangar,
            StationId = station.Id,
            CapacityM3 = 0,
        };

        // The container nothing has ever put anything into, which is the point of the exercise.
        var hold = new Inventory
        {
            CharacterId = owner.Id,
            Kind = InventoryKind.ShipHold,
            CapacityM3 = 0,
        };

        var strangers = new Inventory
        {
            CharacterId = stranger.Id,
            Kind = InventoryKind.StationHangar,
            StationId = station.Id,
            CapacityM3 = 0,
        };

        context.Inventories.AddRange(hangar, hold, strangers);
        await context.SaveChangesAsync();

        _oreId = ore.Id;
        _laserId = laser.Id;
        _hangarId = hangar.Id;
        _holdId = hold.Id;
        _strangersHangarId = strangers.Id;
    }
}
