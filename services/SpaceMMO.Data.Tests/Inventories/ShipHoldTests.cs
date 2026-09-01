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
/// A hold belongs to a hull, and carries what that hull is rated for (ADR-0012, ADR-0014).
/// </summary>
/// <remarks>
/// <para>
/// <c>InventoryKind.ShipHold</c> and <c>Inventory.ShipItemInstanceId</c> have described this
/// arrangement since the first migration and nothing had ever created one — before this,
/// <c>ShipHold</c> appeared exactly once in the whole service layer, in a comment. So the schema
/// said the game worked one way and the game worked another.
/// </para>
/// <para>
/// The tests worth having are the ones that would be silently wrong. A hold keyed on its owner
/// instead of on the hull looks identical until somebody owns two ships, at which point their cargo
/// pools across a fleet and nobody can see why.
/// </para>
/// </remarks>
[Collection(SharedDatabase.Name)]
public sealed class ShipHoldTests(DatabaseFixture fixture) : IAsyncLifetime
{
    private readonly DatabaseFixture _fixture = fixture;

    private int _oreId;
    private int _shuttleId;
    private int _freighterId;
    private int _laserId;
    private int _ownerId;
    private long _hangarId;

    public async Task InitializeAsync()
    {
        await _fixture.ResetAsync();
        await SeedAsync();
    }

    public Task DisposeAsync() => Task.CompletedTask;

    private static InventoryService Service(SpaceMmoDbContext context) => new(context);

    private async Task<long> OwnAsync(int itemDefId)
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        var instance = new ItemInstance
        {
            ItemDefId = itemDefId,
            InventoryId = _hangarId,
            Condition = 100,
            AcquisitionValue = Credits.Zero,
            CreatedAt = DateTimeOffset.UtcNow,
        };

        context.ItemInstances.Add(instance);
        await context.SaveChangesAsync();

        return instance.Id;
    }

    [Fact]
    public async Task A_hull_gets_a_hold_the_size_its_definition_says()
    {
        long shuttle = await OwnAsync(_shuttleId);

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        Inventory hold = await Service(context).GetOrCreateShipHoldAsync(shuttle);

        Assert.Equal(InventoryKind.ShipHold, hold.Kind);
        Assert.Equal(shuttle, hold.ShipItemInstanceId);
        Assert.Equal(_ownerId, hold.CharacterId);
        Assert.Equal(80.0, hold.CapacityM3, 3);
    }

    [Fact]
    public async Task Two_ships_are_two_holds_that_do_not_pool()
    {
        // The whole reason the hold hangs off the hull rather than off the character. Keyed on the
        // owner it would be one container, and a fleet would share a boot.
        long first = await OwnAsync(_shuttleId);
        long second = await OwnAsync(_freighterId);

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        Inventory shuttleHold = await Service(context).GetOrCreateShipHoldAsync(first);
        Inventory freighterHold = await Service(context).GetOrCreateShipHoldAsync(second);

        Assert.NotEqual(shuttleHold.Id, freighterHold.Id);

        // And they are different sizes, which is the first thing in this game that has ever told a
        // shuttle from a freighter.
        Assert.Equal(80.0, shuttleHold.CapacityM3, 3);
        Assert.Equal(360.0, freighterHold.CapacityM3, 3);

        await Service(context).AddAsync(shuttleHold.Id, _oreId, 50, Credits.Zero);
        await context.SaveChangesAsync();

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.Equal(50, await Service(verify).QuantityOfAsync(shuttleHold.Id, _oreId));
        Assert.Equal(0, await Service(verify).QuantityOfAsync(freighterHold.Id, _oreId));
    }

    [Fact]
    public async Task Asking_twice_gets_the_same_hold()
    {
        long shuttle = await OwnAsync(_shuttleId);

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        Inventory once = await Service(context).GetOrCreateShipHoldAsync(shuttle);
        Inventory twice = await Service(context).GetOrCreateShipHoldAsync(shuttle);

        Assert.Equal(once.Id, twice.Id);
    }

    [Fact]
    public async Task A_hold_bounded_by_its_hull_refuses_what_will_not_fit()
    {
        // 80 m3 against ore at 0.4 is two hundred units, which is one resource node exactly.
        long shuttle = await OwnAsync(_shuttleId);

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        Inventory hold = await Service(context).GetOrCreateShipHoldAsync(shuttle);

        await Service(context).AddAsync(hold.Id, _oreId, 200, Credits.Zero);
        await context.SaveChangesAsync();

        await Assert.ThrowsAsync<InventoryFullException>(
            () => Service(context).AddAsync(hold.Id, _oreId, 1, Credits.Zero));
    }

    [Fact]
    public async Task Re_rating_a_hull_in_content_reaches_the_holds_that_exist()
    {
        // How much a hull carries is a property of the hull, so re-authoring it has to reach the
        // containers already hanging off one. Setting the capacity only where a hold is created is
        // the mistake the carried inventories made, and it applied the rule to nobody who already
        // had pockets.
        long shuttle = await OwnAsync(_shuttleId);

        await using (SpaceMmoDbContext first = _fixture.CreateContext())
        {
            await Service(first).GetOrCreateShipHoldAsync(shuttle);
        }

        await using (SpaceMmoDbContext retune = _fixture.CreateContext())
        {
            ItemDef hull = await retune.ItemDefs.FirstAsync(d => d.Id == _shuttleId);
            hull.HoldCapacityM3 = 120.0;
            await retune.SaveChangesAsync();
        }

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        Inventory hold = await Service(context).GetOrCreateShipHoldAsync(shuttle);

        Assert.Equal(120.0, hold.CapacityM3, 3);
    }

    [Fact]
    public async Task Only_a_hull_gets_a_hold()
    {
        // A hold on a mining laser is not a smaller mistake than a hold on nothing: it makes
        // ShipItemInstanceId mean something other than what its own comment says.
        long laser = await OwnAsync(_laserId);

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        await Assert.ThrowsAsync<InventoryTransferException>(
            () => Service(context).GetOrCreateShipHoldAsync(laser));
    }

    [Fact]
    public async Task A_hull_nobody_owns_gets_no_hold()
    {
        // ItemInstance.InventoryId is null once an instance is destroyed, and a hold has to belong
        // to somebody. A container addressed to nobody is the state ADR-0006 calls being inside the
        // explosion rather than surviving it.
        long shuttle = await OwnAsync(_shuttleId);

        await using (SpaceMmoDbContext destroy = _fixture.CreateContext())
        {
            ItemInstance instance = await destroy.ItemInstances.FirstAsync(i => i.Id == shuttle);
            instance.InventoryId = null;
            await destroy.SaveChangesAsync();
        }

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        await Assert.ThrowsAsync<InventoryTransferException>(
            () => Service(context).GetOrCreateShipHoldAsync(shuttle));
    }

    [Fact]
    public async Task An_unrated_hull_carries_nothing_rather_than_everything()
    {
        // Zero means unlimited on this column, which is right for a station hangar and wrong for a
        // ship: an unlimited hold is a bank account you can fly, and ADR-0008's planet-locked
        // materials become a shopping list rather than a journey. A hull whose definition forgot to
        // say gets a hold that refuses everything, which is loud.
        await using (SpaceMmoDbContext unrate = _fixture.CreateContext())
        {
            ItemDef hull = await unrate.ItemDefs.FirstAsync(d => d.Id == _shuttleId);
            hull.HoldCapacityM3 = null;
            await unrate.SaveChangesAsync();
        }

        long shuttle = await OwnAsync(_shuttleId);

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        Inventory hold = await Service(context).GetOrCreateShipHoldAsync(shuttle);

        Assert.Equal(0.0, hold.CapacityM3, 3);
    }

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

        // The authored volumes and hold ratings, so what is under test is the shipped pack rather
        // than numbers invented here.
        var ore = new ItemDef
        {
            Key = "ferrite_ore", Name = "Ferrite Ore", Category = ItemCategory.Raw, VolumeM3 = 0.4,
        };

        var shuttle = new ItemDef
        {
            Key = "hull_shuttle", Name = "Shuttle", Category = ItemCategory.Hull,
            VolumeM3 = 200.0, HoldCapacityM3 = 80.0,
        };

        var freighter = new ItemDef
        {
            Key = "hull_freighter", Name = "Freighter", Category = ItemCategory.Hull,
            VolumeM3 = 900.0, HoldCapacityM3 = 360.0,
        };

        var laser = new ItemDef
        {
            Key = "crude_mining_laser", Name = "Crude Mining Laser",
            Category = ItemCategory.Tool, VolumeM3 = 2.0,
        };

        context.ItemDefs.AddRange(ore, shuttle, freighter, laser);
        await context.SaveChangesAsync();

        var account = new Account
        {
            Email = "pilot@local.test",
            PasswordHash = "x",
            CreatedAt = DateTimeOffset.UtcNow,
        };

        context.Accounts.Add(account);
        await context.SaveChangesAsync();

        var owner = new Character
        {
            AccountId = account.Id,
            Name = "Pilot",
            Race = Race.Humanoid,
            HomeBodyId = body.Id,
            Balance = Credits.FromWholeCredits(1_000),
            CreatedAt = DateTimeOffset.UtcNow,
        };

        context.Characters.Add(owner);
        await context.SaveChangesAsync();

        Inventory hangar = await Service(context).GetOrCreateStationHangarAsync(
            owner.Id, station.Id);

        _oreId = ore.Id;
        _shuttleId = shuttle.Id;
        _freighterId = freighter.Id;
        _laserId = laser.Id;
        _ownerId = owner.Id;
        _hangarId = hangar.Id;
    }
}
