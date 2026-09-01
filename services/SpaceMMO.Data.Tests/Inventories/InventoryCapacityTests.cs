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
/// A container that is full stops taking things (ADR-0014).
/// </summary>
/// <remarks>
/// <para>
/// Until this existed every inventory in the game was infinite. <c>CapacityM3</c> was on the row,
/// hangars were created at zero, and nothing read it — so a character carried a planet's worth of
/// ore in their pockets, a ship's hold was decoration, and nothing distinguished a shuttle from a
/// freighter. Hauling is M4's premise and it was a formality.
/// </para>
/// <para>
/// The tests worth having are the ones that would be quietly wrong rather than the happy path. A
/// limit that half-applies is worse than none: it looks like a rule and is not one.
/// </para>
/// </remarks>
[Collection(SharedDatabase.Name)]
public sealed class InventoryCapacityTests(DatabaseFixture fixture) : IAsyncLifetime
{
    private readonly DatabaseFixture _fixture = fixture;

    private int _oreId;
    private int _hullId;
    private long _carriedId;
    private long _hangarId;

    public async Task InitializeAsync()
    {
        await _fixture.ResetAsync();
        await SeedAsync();
    }

    public Task DisposeAsync() => Task.CompletedTask;

    private static InventoryService Service(SpaceMmoDbContext context) => new(context);

    [Fact]
    public async Task A_carried_inventory_holds_what_the_decision_says_it_holds()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        // Fifteen ore at 0.4 m3 is six exactly, which is the whole of it.
        await Service(context).AddAsync(_carriedId, _oreId, 15, Credits.Zero);
        await context.SaveChangesAsync();

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.Equal(15, await Service(verify).QuantityOfAsync(_carriedId, _oreId));
    }

    [Fact]
    public async Task Filling_it_exactly_is_not_overfilling_it()
    {
        // Volumes are doubles read from content and multiplied by quantities, so a container sized
        // to take exactly fifteen ore can come out a billionth of a cubic metre over and refuse the
        // last one. A pack that is full one short of its stated size is a bug nobody can reproduce.
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        await Service(context).AddAsync(_carriedId, _oreId, 14, Credits.Zero);
        await context.SaveChangesAsync();

        await Service(context).AddAsync(_carriedId, _oreId, 1, Credits.Zero);
        await context.SaveChangesAsync();

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.Equal(15, await Service(verify).QuantityOfAsync(_carriedId, _oreId));
    }

    [Fact]
    public async Task One_more_than_it_holds_is_refused_whole()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        await Service(context).AddAsync(_carriedId, _oreId, 10, Credits.Zero);
        await context.SaveChangesAsync();

        // Ten more will not fit. The refusal has to be of all ten: dropping five in and destroying
        // the rest is a silent loss of a player's property, and the same rule applied to a market
        // purchase would be a silent partial refund.
        InventoryFullException full = await Assert.ThrowsAsync<InventoryFullException>(
            () => Service(context).AddAsync(_carriedId, _oreId, 10, Credits.Zero));

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.Equal(10, await Service(verify).QuantityOfAsync(_carriedId, _oreId));

        // The numbers travel with it, so a caller can word it for a player without parsing a
        // sentence: "your pack is full" needs the room left, not the message.
        Assert.Equal(6.0, full.CapacityM3, 3);
        Assert.Equal(4.0, full.UsedM3, 3);
        Assert.Equal(4.0, full.WantedM3, 3);
        Assert.Equal(2.0, full.FreeM3, 3);
    }

    [Fact]
    public async Task A_pack_that_existed_before_the_rule_gets_the_rule()
    {
        // The case the first version of these tests could not see, because it seeded a character
        // who had never had pockets and so only ever exercised the path that creates them.
        //
        // Every carried inventory in the live database was created at zero -- unlimited -- before
        // ADR-0014 existed. Setting the capacity only where a row is created left the limit applying
        // to nobody who already had one, which was everybody: a playtest carried fifty ore through a
        // six cubic metre pack and the column read 0.
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        Inventory carried = await context.Inventories.FirstAsync(i => i.Id == _carriedId);

        carried.CapacityM3 = 0;
        await context.SaveChangesAsync();

        await using SpaceMmoDbContext reopened = _fixture.CreateContext();

        Character owner = await reopened.Characters.FirstAsync();

        Inventory found = await Service(reopened).GetOrCreateCarriedAsync(owner.Id);

        Assert.Equal(InventoryService.CarriedCapacityM3, found.CapacityM3, 3);

        // And it binds, rather than merely being written down.
        await Assert.ThrowsAsync<InventoryFullException>(
            () => Service(reopened).AddAsync(found.Id, _oreId, 50, Credits.Zero));
    }

    [Fact]
    public async Task A_station_hangar_is_still_unlimited()
    {
        // Rented storage with rent as its sink (InventoryKind.StationHangar). Capping it as well
        // would charge somebody for a container that also refuses their goods.
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        await Service(context).AddAsync(_hangarId, _oreId, 5_000, Credits.Zero);
        await context.SaveChangesAsync();

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.Equal(5_000, await Service(verify).QuantityOfAsync(_hangarId, _oreId));
    }

    [Fact]
    public async Task An_overfull_container_takes_nothing_more_and_keeps_everything()
    {
        // A legal state rather than an error. Limits arriving after goods exist leaves one, and so
        // will a hull moved into a hold that then has no room. The answer is never that it destroys
        // what is already there.
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        await Service(context).AddAsync(_carriedId, _oreId, 10, Credits.Zero);
        await context.SaveChangesAsync();

        // The limit arrives after the goods, which is how a live database gets one.
        Inventory carried = await context.Inventories.FirstAsync(i => i.Id == _carriedId);
        carried.CapacityM3 = 0.5;
        await context.SaveChangesAsync();

        await Assert.ThrowsAsync<InventoryFullException>(
            () => Service(context).AddAsync(_carriedId, _oreId, 1, Credits.Zero));

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.Equal(10, await Service(verify).QuantityOfAsync(_carriedId, _oreId));

        // And it still drains, or somebody is stuck holding goods they cannot put down.
        await Service(context).RemoveAsync(_carriedId, _oreId, 10);
        await context.SaveChangesAsync();

        await using SpaceMmoDbContext drained = _fixture.CreateContext();

        Assert.Equal(0, await Service(drained).QuantityOfAsync(_carriedId, _oreId));
    }

    [Fact]
    public async Task A_hull_in_a_container_takes_up_room()
    {
        // Instances as well as stacks. Counting only the stackable half would let anybody carry an
        // unlimited number of ships, which is the sort of hole that is obvious once stated and
        // invisible in a test that only ever adds ore.
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        context.ItemInstances.Add(new ItemInstance
        {
            ItemDefId = _hullId,
            InventoryId = _carriedId,
            Condition = 100,
            AcquisitionValue = Credits.Zero,
        });

        await context.SaveChangesAsync();

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.Equal(5.0, await Service(verify).UsedVolumeAsync(_carriedId), 3);

        // Five of six used, so two ore fit and three do not.
        await Assert.ThrowsAsync<InventoryFullException>(
            () => Service(verify).AddAsync(_carriedId, _oreId, 3, Credits.Zero));

        await Service(verify).AddAsync(_carriedId, _oreId, 2, Credits.Zero);
        await verify.SaveChangesAsync();

        await using SpaceMmoDbContext after = _fixture.CreateContext();

        Assert.Equal(2, await Service(after).QuantityOfAsync(_carriedId, _oreId));
    }

    [Fact]
    public async Task A_transfer_into_a_full_container_is_refused_like_anything_else()
    {
        // The route this rule was NOT put on, deliberately: it lives in AddAsync, which transfers
        // go through. A limit on transfer alone would give a hold you cannot fill by dragging and
        // can fill by mining into, which looks like a rule and is not one.
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        await Service(context).AddAsync(_hangarId, _oreId, 100, Credits.FromWholeCredits(1_000));
        await context.SaveChangesAsync();

        await Assert.ThrowsAsync<InventoryFullException>(
            () => Service(context).TransferAsync(_hangarId, _carriedId, _oreId, 100));

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.Equal(100, await Service(verify).QuantityOfAsync(_hangarId, _oreId));
        Assert.Equal(0, await Service(verify).QuantityOfAsync(_carriedId, _oreId));
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

        var ore = new ItemDef
        {
            Key = "ferrite_ore", Name = "Ferrite Ore", Category = ItemCategory.Raw, VolumeM3 = 0.4,
        };

        // Small for a hull, so it fits in a pocket and leaves room to measure with. The real ones
        // are 200 and 900 m3; what is under test is that an instance counts at all.
        var hull = new ItemDef
        {
            Key = "hull_toy", Name = "Toy Hull", Category = ItemCategory.Hull,
            VolumeM3 = 5.0, HoldCapacityM3 = 80.0,
        };

        context.ItemDefs.AddRange(ore, hull);
        await context.SaveChangesAsync();

        var account = new Account
        {
            Email = "carrier@local.test",
            PasswordHash = "x",
            CreatedAt = DateTimeOffset.UtcNow,
        };

        context.Accounts.Add(account);
        await context.SaveChangesAsync();

        var owner = new Character
        {
            AccountId = account.Id,
            Name = "Carrier",
            Race = Race.Humanoid,
            HomeBodyId = body.Id,
            Balance = Credits.FromWholeCredits(1_000),
            CreatedAt = DateTimeOffset.UtcNow,
        };

        context.Characters.Add(owner);
        await context.SaveChangesAsync();

        // Through the service, so what is under test is the capacity a character actually gets
        // rather than one written out here. A test that seeds its own number would have passed
        // just as happily while carried inventories went on being created unlimited.
        Inventory carried = await Service(context).GetOrCreateCarriedAsync(owner.Id);

        var hangar = new Inventory
        {
            CharacterId = owner.Id,
            Kind = InventoryKind.StationHangar,
            StationId = station.Id,
            CapacityM3 = 0,
        };

        context.Inventories.Add(hangar);
        await context.SaveChangesAsync();

        _oreId = ore.Id;
        _hullId = hull.Id;
        _carriedId = carried.Id;
        _hangarId = hangar.Id;
    }
}
