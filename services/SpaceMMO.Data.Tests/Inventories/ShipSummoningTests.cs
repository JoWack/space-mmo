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

    /// <summary>
    /// The second half of ADR-0012 point 4: sitting in the ship opens its hold.
    /// </summary>
    /// <remarks>
    /// The case the first half cannot cover, and the one hauling is actually made of: a ship in
    /// flight is docked nowhere at all, so a rule that only asked "are you docked where your ship
    /// is parked" closed the hold the moment somebody left the station with cargo in it.
    /// </remarks>
    [Fact]
    public async Task Sitting_in_your_ship_opens_its_hold_from_anywhere()
    {
        long hull = await OwnAsync(_pilotId, _spaceportId, _shuttleId);
        await DockAsync(_pilotId, _spaceportId);

        await using (SpaceMmoDbContext summon = _fixture.CreateContext())
        {
            Inventory hold = await Ships(summon).SummonAsync(_pilotId, hull);

            await Inventories(summon).AddAsync(hold.Id, _oreId, 100, Credits.Zero);
            await summon.SaveChangesAsync();
        }

        await using (SpaceMmoDbContext board = _fixture.CreateContext())
        {
            await Ships(board).BoardAsync(_pilotId, hull);
        }

        // Undocked and away. Nothing about being docked is true any more, which is exactly the
        // state a ship spends most of its life in.
        await DockAsync(_pilotId, null);

        await using SpaceMmoDbContext flying = _fixture.CreateContext();

        Inventory? reachable = await Ships(flying).ReachableHoldAsync(_pilotId);

        Assert.NotNull(reachable);
        Assert.Equal(100, await Inventories(flying).QuantityOfAsync(reachable!.Id, _oreId));
    }

    /// <summary>
    /// Stepping out closes it again, wherever you are standing.
    /// </summary>
    /// <remarks>
    /// The pair to the test above, and the one that stops "aboard" being a latch. A flag that is
    /// only ever set opens a hold from a rock on a planet for the rest of a character's life,
    /// which is the exploit being undocked would have been.
    /// </remarks>
    [Fact]
    public async Task Stepping_out_on_a_planet_closes_the_hold_again()
    {
        long hull = await OwnAsync(_pilotId, _spaceportId, _shuttleId);
        await DockAsync(_pilotId, _spaceportId);

        await using (SpaceMmoDbContext summon = _fixture.CreateContext())
        {
            await Ships(summon).SummonAsync(_pilotId, hull);
        }

        await using (SpaceMmoDbContext board = _fixture.CreateContext())
        {
            await Ships(board).BoardAsync(_pilotId, hull);
        }

        await DockAsync(_pilotId, null);

        await using (SpaceMmoDbContext step = _fixture.CreateContext())
        {
            await Ships(step).DisembarkAsync(_pilotId);

            // Twice, because a ship is left in ways nobody sends a message about -- a disconnect,
            // a crash, a server restart -- and every one eventually produces a second one.
            await Ships(step).DisembarkAsync(_pilotId);
        }

        await using SpaceMmoDbContext onFoot = _fixture.CreateContext();

        Assert.Null(await Ships(onFoot).ReachableHoldAsync(_pilotId));
    }

    /// <summary>
    /// Boarding a hull that is not yours is refused, and changes nothing.
    /// </summary>
    /// <remarks>
    /// Only the game server may say who boarded what, so this guards against a fault rather than a
    /// hostile client -- a stale pawn, a mistaken cast, a hull sold out from under somebody. It is
    /// worth one row, because being aboard is what opens a hold.
    /// </remarks>
    [Fact]
    public async Task Boarding_a_hull_that_is_not_yours_is_refused()
    {
        long theirs = await OwnAsync(_strangerId, _spaceportId, _shuttleId);

        await using (SpaceMmoDbContext context = _fixture.CreateContext())
        {
            await Assert.ThrowsAsync<ShipSummonException>(
                () => Ships(context).BoardAsync(_pilotId, theirs));
        }

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Character pilot = await verify.Characters.SingleAsync(c => c.Id == _pilotId);

        Assert.Null(pilot.AboardShipItemInstanceId);

        // And the refusal did not quietly make it theirs either, which is the worse half of
        // getting this wrong: boarding sets the active ship, so a missing check would hand over
        // somebody elses shuttle rather than merely opening its hold.
        Assert.Null(pilot.ActiveShipItemInstanceId);
    }

    [Fact]
    public async Task Boarding_something_that_is_not_a_ship_is_refused()
    {
        long laser = await OwnAsync(_pilotId, _spaceportId, _laserId);

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        await Assert.ThrowsAsync<ShipSummonException>(
            () => Ships(context).BoardAsync(_pilotId, laser));
    }

    /// <summary>
    /// Climbing into the other one changes which ship is yours.
    /// </summary>
    /// <remarks>
    /// The ship you are flying is the ship you are flying. Making somebody summon it again to say
    /// so would be ceremony, and it would leave the hold of a shuttle they are not in open while
    /// the one they are sitting in stayed shut.
    /// </remarks>
    [Fact]
    public async Task Boarding_the_other_hull_makes_that_one_the_active_ship()
    {
        long first = await OwnAsync(_pilotId, _spaceportId, _shuttleId);
        long second = await OwnAsync(_pilotId, _spaceportId, _shuttleId);

        await DockAsync(_pilotId, _spaceportId);

        await using (SpaceMmoDbContext summon = _fixture.CreateContext())
        {
            await Ships(summon).SummonAsync(_pilotId, first);
        }

        await using (SpaceMmoDbContext board = _fixture.CreateContext())
        {
            await Ships(board).BoardAsync(_pilotId, second);
        }

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        ShipService.ActiveShip? active = await Ships(verify).ActiveShipAsync(_pilotId);

        Assert.NotNull(active);
        Assert.Equal(second, active!.HullItemInstanceId);
        Assert.True(active.Aboard);
    }

    /// <summary>
    /// Where the game server is told to put a ship in the world.
    /// </summary>
    /// <remarks>
    /// The station is the load-bearing field: a pawn spawned without it has nowhere to go, and one
    /// spawned at the wrong station is a ship a player walks out of a hub and cannot find.
    /// </remarks>
    [Fact]
    public async Task An_active_ship_says_where_it_is_parked()
    {
        long hull = await OwnAsync(_pilotId, _marketId, _shuttleId);
        await DockAsync(_pilotId, _spaceportId);

        await using (SpaceMmoDbContext summon = _fixture.CreateContext())
        {
            await Ships(summon).SummonAsync(_pilotId, hull);
        }

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        ShipService.ActiveShip? active = await Ships(verify).ActiveShipAsync(_pilotId);

        Assert.NotNull(active);

        // Summoned to the spaceport, so it is parked there rather than at the market it was left
        // at. "Summoning elsewhere moves it" is the whole of that sentence.
        Assert.Equal(_spaceportId, active!.StationId);

        // Named, because the message a player reads says which ship arrived, and "Ship summoned"
        // is what this field existing prevents.
        Assert.False(string.IsNullOrWhiteSpace(active.Name));

        // Summoned is not boarded. A ship waiting outside is not one you are sitting in, and the
        // hold rule turns on the difference.
        Assert.False(active.Aboard);
    }

    /// <summary>
    /// Sitting in one ship does not open the hold of a different one.
    /// </summary>
    /// <remarks>
    /// Reachable without doing anything strange: board the shuttle, walk back into the station and
    /// summon the freighter. The active ship is now the freighter and the body is still in the
    /// shuttle, so "aboard" on its own is not the question -- "aboard the ship whose hold this is"
    /// is, and that is why the two ids are compared rather than one being read for truth.
    /// </remarks>
    [Fact]
    public async Task Sitting_in_one_ship_does_not_open_the_others_hold()
    {
        long shuttle = await OwnAsync(_pilotId, _spaceportId, _shuttleId);
        long freighter = await OwnAsync(_pilotId, _spaceportId, _shuttleId);

        await DockAsync(_pilotId, _spaceportId);

        await using (SpaceMmoDbContext board = _fixture.CreateContext())
        {
            await Ships(board).SummonAsync(_pilotId, shuttle);
            await Ships(board).BoardAsync(_pilotId, shuttle);
        }

        await using (SpaceMmoDbContext swap = _fixture.CreateContext())
        {
            await Ships(swap).SummonAsync(_pilotId, freighter);
        }

        // Away from the station, so the docked half of the rule cannot answer either.
        await DockAsync(_pilotId, null);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.Null(await Ships(verify).ReachableHoldAsync(_pilotId));
    }

    [Fact]
    public async Task Somebody_with_no_ship_has_no_active_ship()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        Assert.Null(await Ships(context).ActiveShipAsync(_pilotId));
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
