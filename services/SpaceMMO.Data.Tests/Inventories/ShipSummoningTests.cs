using Microsoft.EntityFrameworkCore;
using SpaceMMO.Data.Entities;
using SpaceMMO.Data.Inventories;
using SpaceMMO.Data.Ships;
using SpaceMMO.Domain.Characters;
using SpaceMMO.Domain.Economy;
using SpaceMMO.Domain.Items;
using SpaceMMO.Domain.Universe;
using Xunit;

namespace SpaceMMO.Data.Tests.Inventories;

/// <summary>
/// Summoning a hull you own, and reaching its hold only when you are with it (ADR-0012).
/// </summary>
/// <remarks>
/// <para>
/// Before this, the ship a player flew was an unowned pawn the game mode spawned thirty metres away
/// so that boarding could be tested. Nobody owned it, nothing was inside it, and crafting a hull
/// through the questline produced an item that did nothing.
/// </para>
/// <para>
/// The tests worth having are the refusals. A summon that works is visible the moment anybody tries
/// it; a summon that quietly accepts somebody else's hull, or a hold that opens from the wrong side
/// of the system, is not.
/// </para>
/// </remarks>
[Collection(SharedDatabase.Name)]
public sealed class ShipSummoningTests(DatabaseFixture fixture) : IAsyncLifetime
{
    private readonly DatabaseFixture _fixture = fixture;

    private int _shuttleId;
    private int _laserId;
    private int _oreId;
    private int _pilotId;
    private int _strangerId;
    private int _spaceportId;
    private int _marketId;

    public async Task InitializeAsync()
    {
        await _fixture.ResetAsync();
        await SeedAsync();
    }

    public Task DisposeAsync() => Task.CompletedTask;

    private static ShipService Ships(SpaceMmoDbContext context) => new(context);

    private static InventoryService Inventories(SpaceMmoDbContext context) => new(context);

    /// <summary>Puts an instance in a character's hangar at a station.</summary>
    private async Task<long> OwnAsync(int characterId, int stationId, int itemDefId)
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        Inventory hangar = await Inventories(context)
            .GetOrCreateStationHangarAsync(characterId, stationId);

        var instance = new ItemInstance
        {
            ItemDefId = itemDefId,
            InventoryId = hangar.Id,
            Condition = 100,
            AcquisitionValue = Credits.Zero,
            CreatedAt = DateTimeOffset.UtcNow,
        };

        context.ItemInstances.Add(instance);
        await context.SaveChangesAsync();

        return instance.Id;
    }

    private async Task DockAsync(int characterId, int? stationId)
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        Character character = await context.Characters.SingleAsync(c => c.Id == characterId);
        character.DockedStationId = stationId;

        await context.SaveChangesAsync();
    }

    [Fact]
    public async Task Summoning_a_hull_you_own_makes_it_yours_to_fly_and_gives_it_a_hold()
    {
        long hull = await OwnAsync(_pilotId, _spaceportId, _shuttleId);
        await DockAsync(_pilotId, _spaceportId);

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        Inventory hold = await Ships(context).SummonAsync(_pilotId, hull);

        Assert.Equal(InventoryKind.ShipHold, hold.Kind);
        Assert.Equal(hull, hold.ShipItemInstanceId);
        Assert.Equal(80.0, hold.CapacityM3, 3);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Character pilot = await verify.Characters.SingleAsync(c => c.Id == _pilotId);

        Assert.Equal(hull, pilot.ActiveShipItemInstanceId);
    }

    [Fact]
    public async Task Ships_are_not_summoned_at_a_market()
    {
        // Spaceports and the capital handle ships; a trading hub is an order book with a roof.
        long hull = await OwnAsync(_pilotId, _marketId, _shuttleId);
        await DockAsync(_pilotId, _marketId);

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        ShipSummonException refused = await Assert.ThrowsAsync<ShipSummonException>(
            () => Ships(context).SummonAsync(_pilotId, hull));

        Assert.Contains("TradingHub", refused.Message, StringComparison.Ordinal);
    }

    [Fact]
    public async Task Ships_are_not_summoned_out_of_thin_air()
    {
        long hull = await OwnAsync(_pilotId, _spaceportId, _shuttleId);
        await DockAsync(_pilotId, null);

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        await Assert.ThrowsAsync<ShipSummonException>(
            () => Ships(context).SummonAsync(_pilotId, hull));
    }

    [Fact]
    public async Task Somebody_elses_hull_is_not_yours_to_summon()
    {
        // The check a hostile client is testing: the request names an instance id, and ownership is
        // read from the inventory it sits in rather than from the request.
        long theirs = await OwnAsync(_strangerId, _spaceportId, _shuttleId);
        await DockAsync(_pilotId, _spaceportId);

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        await Assert.ThrowsAsync<ShipSummonException>(
            () => Ships(context).SummonAsync(_pilotId, theirs));

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Character pilot = await verify.Characters.SingleAsync(c => c.Id == _pilotId);

        Assert.Null(pilot.ActiveShipItemInstanceId);
    }

    [Fact]
    public async Task A_mining_laser_is_not_a_ship()
    {
        long laser = await OwnAsync(_pilotId, _spaceportId, _laserId);
        await DockAsync(_pilotId, _spaceportId);

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        await Assert.ThrowsAsync<ShipSummonException>(
            () => Ships(context).SummonAsync(_pilotId, laser));
    }

    [Fact]
    public async Task Summoning_somewhere_else_brings_the_ship_there()
    {
        // "It stays where you left it, and summoning elsewhere moves it", settled 31 August. In
        // rows that is the hull instance changing which hangar it sits in — so a ship is always
        // somewhere by construction, rather than by a coordinate somebody has to maintain.
        long hull = await OwnAsync(_pilotId, _marketId, _shuttleId);

        await DockAsync(_pilotId, _spaceportId);

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        await Ships(context).SummonAsync(_pilotId, hull);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        ItemInstance moved = await verify.ItemInstances
            .Include(i => i.Inventory)
            .SingleAsync(i => i.Id == hull);

        Assert.Equal(_spaceportId, moved.Inventory!.StationId);
    }

    [Fact]
    public async Task A_hold_is_reachable_where_the_ship_is_and_nowhere_else()
    {
        long hull = await OwnAsync(_pilotId, _spaceportId, _shuttleId);
        await DockAsync(_pilotId, _spaceportId);

        await using (SpaceMmoDbContext summon = _fixture.CreateContext())
        {
            Inventory hold = await Ships(summon).SummonAsync(_pilotId, hull);

            await Inventories(summon).AddAsync(hold.Id, _oreId, 100, Credits.Zero);
            await summon.SaveChangesAsync();
        }

        await using (SpaceMmoDbContext here = _fixture.CreateContext())
        {
            Inventory? reachable = await Ships(here).ReachableHoldAsync(_pilotId);

            Assert.NotNull(reachable);
            Assert.Equal(100, await Inventories(here).QuantityOfAsync(reachable!.Id, _oreId));
        }

        // Flown home and left the ship behind. The hold is exactly as out of reach as the hangar
        // beside it, which is the rule that keeps hauling a journey rather than a bank transfer.
        await DockAsync(_pilotId, _marketId);

        await using SpaceMmoDbContext elsewhere = _fixture.CreateContext();

        Assert.Null(await Ships(elsewhere).ReachableHoldAsync(_pilotId));
    }

    [Fact]
    public async Task Somebody_with_no_ship_reaches_no_hold()
    {
        await DockAsync(_pilotId, _spaceportId);

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        Assert.Null(await Ships(context).ReachableHoldAsync(_pilotId));
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

        var spaceport = new Station
        {
            Key = "station_terra_yard",
            Name = "Terra Yard",
            StarSystemId = system.Id,
            BodyId = body.Id,
            Kind = StationKind.Spaceport,
            DirectionX = -1.0,
            DockingRangeKilometres = 5.0,
        };

        var market = new Station
        {
            Key = "station_terra_market",
            Name = "Terra Market",
            StarSystemId = system.Id,
            BodyId = body.Id,
            Kind = StationKind.TradingHub,
            DirectionX = 1.0,
            DockingRangeKilometres = 5.0,
        };

        context.Stations.AddRange(spaceport, market);

        var shuttle = new ItemDef
        {
            Key = "hull_shuttle", Name = "Shuttle", Category = ItemCategory.Hull,
            VolumeM3 = 200.0, HoldCapacityM3 = 80.0,
        };

        var laser = new ItemDef
        {
            Key = "crude_mining_laser", Name = "Crude Mining Laser",
            Category = ItemCategory.Tool, VolumeM3 = 2.0,
        };

        var ore = new ItemDef
        {
            Key = "ferrite_ore", Name = "Ferrite Ore", Category = ItemCategory.Raw, VolumeM3 = 0.4,
        };

        context.ItemDefs.AddRange(shuttle, laser, ore);
        await context.SaveChangesAsync();

        var account = new Account
        {
            Email = "yard@local.test",
            PasswordHash = "x",
            CreatedAt = DateTimeOffset.UtcNow,
        };

        context.Accounts.Add(account);
        await context.SaveChangesAsync();

        var pilot = new Character
        {
            AccountId = account.Id,
            Name = "Pilot",
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

        context.Characters.AddRange(pilot, stranger);
        await context.SaveChangesAsync();

        _shuttleId = shuttle.Id;
        _laserId = laser.Id;
        _oreId = ore.Id;
        _pilotId = pilot.Id;
        _strangerId = stranger.Id;
        _spaceportId = spaceport.Id;
        _marketId = market.Id;
    }
}
