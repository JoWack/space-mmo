using Microsoft.EntityFrameworkCore;
using SpaceMMO.Data.Docking;
using SpaceMMO.Data.Entities;
using SpaceMMO.Data.Inventories;
using SpaceMMO.Domain.Characters;
using SpaceMMO.Domain.Economy;
using SpaceMMO.Domain.Items;
using SpaceMMO.Domain.Universe;
using Xunit;

namespace SpaceMMO.Data.Tests.Docking;

/// <summary>
/// Integration tests for docking.
/// </summary>
/// <remarks>
/// Docking is about to become the gate on the market, so the tests that matter are the ones about
/// refusing: a station with nowhere to dock, and a character docked somewhere else. Both would
/// otherwise present as a market that works from the wrong place.
/// </remarks>
[Collection(SharedDatabase.Name)]
public sealed class DockingServiceTests(DatabaseFixture fixture) : IAsyncLifetime
{
    private readonly DatabaseFixture _fixture = fixture;

    private int _placedStationId;
    private int _otherStationId;
    private int _unplacedStationId;
    private int _characterId;

    public async Task InitializeAsync()
    {
        await _fixture.ResetAsync();
        await SeedAsync();
    }

    public Task DisposeAsync() => Task.CompletedTask;

    private static DockingService Service(SpaceMmoDbContext context) =>
        new(context, new InventoryService(context));

    [Fact]
    public async Task Docking_records_where_the_character_is()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();
        await Service(context).DockAsync(_characterId, _placedStationId);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.Equal(
            _placedStationId,
            await Service(verify).DockedStationIdAsync(_characterId));
    }

    [Fact]
    public async Task Docking_gives_the_character_somewhere_to_put_things()
    {
        // Hangars used to appear only when goods did -- gathered, crafted, or bought. A station
        // nobody had traded at therefore had no container at all, which was invisible while goods
        // teleported to one station and became "I am docked here and cannot put anything down" the
        // moment a player could move goods by hand.
        await using (SpaceMmoDbContext context = _fixture.CreateContext())
        {
            Assert.False(
                await context.Inventories.AnyAsync(
                    i => i.CharacterId == _characterId && i.StationId == _placedStationId),
                "no hangar should exist before docking");
        }

        await using (SpaceMmoDbContext context = _fixture.CreateContext())
        {
            await Service(context).DockAsync(_characterId, _placedStationId);
        }

        await using (SpaceMmoDbContext verify = _fixture.CreateContext())
        {
            Assert.True(
                await verify.Inventories.AnyAsync(
                    i => i.CharacterId == _characterId
                        && i.StationId == _placedStationId
                        && i.Kind == InventoryKind.StationHangar),
                "docking should create the hangar");
        }

        // Docking again must not make a second one: the factory is get-or-create, and two hangars at
        // one station would split a player's goods across containers that look identical.
        await using (SpaceMmoDbContext again = _fixture.CreateContext())
        {
            await Service(again).DockAsync(_characterId, _placedStationId);
        }

        await using (SpaceMmoDbContext count = _fixture.CreateContext())
        {
            Assert.Equal(
                1,
                await count.Inventories.CountAsync(
                    i => i.CharacterId == _characterId && i.StationId == _placedStationId));
        }
    }

    [Fact]
    public async Task A_station_with_nowhere_to_dock_refuses()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        // Without this, an unplaced station is one every character in the system is next to,
        // because "nowhere" compares equal to wherever the caller says they are — and the ship
        // starts at the system origin.
        await Assert.ThrowsAsync<UnknownStationException>(
            () => Service(context).DockAsync(_characterId, _unplacedStationId));

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.Null(await Service(verify).DockedStationIdAsync(_characterId));
    }

    [Fact]
    public async Task Docked_somewhere_else_is_not_docked_here()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();
        await Service(context).DockAsync(_characterId, _placedStationId);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.True(await Service(verify).IsDockedAtAsync(_characterId, _placedStationId));

        // The check that matters once the market consults it. A character docked at Grimhold has
        // no business on a Terra order book, and "is docked at all" would let them.
        Assert.False(await Service(verify).IsDockedAtAsync(_characterId, _otherStationId));
    }

    [Fact]
    public async Task Docking_elsewhere_replaces_rather_than_refuses()
    {
        await using (SpaceMmoDbContext context = _fixture.CreateContext())
        {
            await Service(context).DockAsync(_characterId, _placedStationId);
        }

        await using (SpaceMmoDbContext context = _fixture.CreateContext())
        {
            // Flying from one station to another and docking is ordinary. Making it an error
            // would mean every client had to undock first, or handle a failure that means
            // nothing to a player.
            await Service(context).DockAsync(_characterId, _otherStationId);
        }

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.Equal(_otherStationId, await Service(verify).DockedStationIdAsync(_characterId));
    }

    [Fact]
    public async Task Undocking_twice_is_not_an_error()
    {
        await using (SpaceMmoDbContext context = _fixture.CreateContext())
        {
            await Service(context).DockAsync(_characterId, _placedStationId);
        }

        // A ship can leave in ways nobody sends a message about — a disconnect, a crash, a server
        // restart — and every one of those eventually produces a second undock.
        await using (SpaceMmoDbContext context = _fixture.CreateContext())
        {
            await Service(context).UndockAsync(_characterId);
            await Service(context).UndockAsync(_characterId);
        }

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.Null(await Service(verify).DockedStationIdAsync(_characterId));
    }

    [Fact]
    public async Task A_character_that_does_not_exist_is_refused()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        await Assert.ThrowsAsync<UnknownCharacterException>(
            () => Service(context).DockAsync(999_999, _placedStationId));
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

        var placed = new Station
        {
            Key = "station_terra_hub",
            Name = "Terra Outpost",
            StarSystemId = system.Id,
            BodyId = body.Id,
            Kind = StationKind.TradingHub,
            DirectionX = -1.0,
            DirectionY = 0.0,
            DirectionZ = 0.0,
            DockingRangeKilometres = 5.0,
        };

        var other = new Station
        {
            Key = "station_deepdock",
            Name = "Deepdock",
            StarSystemId = system.Id,
            Kind = StationKind.Spaceport,
            SystemX = 30.0,
            SystemY = 12.0,
            SystemZ = 4.0,
            DockingRangeKilometres = 8.0,
        };

        // Authored, but nobody has decided where it stands yet. Legitimate content, and the case
        // the refusal above exists for.
        var unplaced = new Station
        {
            Key = "station_planned",
            Name = "Planned",
            StarSystemId = system.Id,
            BodyId = body.Id,
            Kind = StationKind.TradingHub,
            DockingRangeKilometres = 5.0,
        };

        context.Stations.AddRange(placed, other, unplaced);

        var account = new Account
        {
            Email = "docker@local.test",
            PasswordHash = "x",
            CreatedAt = DateTimeOffset.UtcNow,
        };

        context.Accounts.Add(account);
        await context.SaveChangesAsync();

        var character = new Character
        {
            AccountId = account.Id,
            Name = "Docker",
            Race = Race.Humanoid,
            HomeBodyId = body.Id,
            Balance = Credits.FromWholeCredits(1_000),
            CreatedAt = DateTimeOffset.UtcNow,
        };

        context.Characters.Add(character);
        await context.SaveChangesAsync();

        _placedStationId = placed.Id;
        _otherStationId = other.Id;
        _unplacedStationId = unplaced.Id;
        _characterId = character.Id;
    }
}
