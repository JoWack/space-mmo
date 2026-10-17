using Microsoft.EntityFrameworkCore;
using SpaceMMO.Data.Docking;
using SpaceMMO.Data.Entities;
using SpaceMMO.Data.Inventories;
using SpaceMMO.Data.Quests;
using SpaceMMO.Domain.Characters;
using SpaceMMO.Domain.Economy;
using SpaceMMO.Domain.Quests;
using SpaceMMO.Domain.Universe;
using Xunit;

namespace SpaceMMO.Data.Tests.Quests;

/// <summary>
/// A quest that asks you to dock somewhere is finished by docking there (found 16 October).
/// </summary>
/// <remarks>
/// <para>
/// <strong>Nothing raised a Dock objective until now.</strong> The quest engine accepted the event from M1 and
/// docking never sent one, so "Out of the Gravity Well", the last of the main story, could not be finished, and no
/// character could ever leave new-player protection. Joe found it with Ayla docked at Borlash and the quest stuck.
/// </para>
/// <para>
/// Every quest test fed its events in by hand, which is how it went unseen: they proved the engine counts a dock
/// event and never that anything sends one. These go through <see cref="DockingService"/>, the way the game server
/// does.
/// </para>
/// </remarks>
[Collection(SharedDatabase.Name)]
public sealed class DockObjectiveTests(DatabaseFixture fixture) : IAsyncLifetime
{
    private readonly DatabaseFixture _fixture = fixture;

    private int _characterId;
    private int _capitalId;
    private int _outpostId;

    public async Task InitializeAsync()
    {
        await _fixture.ResetAsync();
        await SeedAsync();
    }

    public Task DisposeAsync() => Task.CompletedTask;

    [Fact]
    public async Task Docking_where_a_quest_asks_finishes_it()
    {
        await AcceptAsync("fly");
        await DockAsync(_capitalId);

        Assert.Equal(QuestState.Completed, await StateOfAsync("fly"));

        // And it paid, as a main-story quest does without a turn-in.
        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.Equal(Credits.FromWholeCredits(5_100), (await verify.Characters.SingleAsync(c => c.Id == _characterId)).Balance);
    }

    [Fact]
    public async Task Docking_anywhere_else_counts_for_nothing()
    {
        await AcceptAsync("fly");
        await DockAsync(_outpostId);

        Assert.Equal(QuestState.InProgress, await StateOfAsync("fly"));
    }

    [Fact]
    public async Task Already_docked_there_when_the_quest_is_taken_counts()
    {
        // Task 145's expectation: a character who starts at the Capital finishes the last quest standing still.
        await DockAsync(_capitalId);
        await AcceptAsync("fly");

        Assert.Equal(QuestState.Completed, await StateOfAsync("fly"));
    }

    [Fact]
    public async Task Docked_there_when_the_step_before_it_finishes_counts()
    {
        await DockAsync(_capitalId);
        await AcceptAsync("gather_then_fly");

        Assert.Equal(QuestState.InProgress, await StateOfAsync("gather_then_fly"));

        await using (SpaceMmoDbContext context = _fixture.CreateContext())
        {
            await new QuestService(context).RecordProgressAsync(_characterId, new ObjectiveEvent(ObjectiveType.Gather, "ore", 5));
        }

        Assert.Equal(QuestState.Completed, await StateOfAsync("gather_then_fly"));
    }

    [Fact]
    public async Task The_seed_counts_for_somebody_who_docked_before_docking_counted()
    {
        // Ayla's case: docked at the target, quest in progress, and nothing will ever dock her again on its own.
        await AcceptAsync("fly");

        await using (SpaceMmoDbContext context = _fixture.CreateContext())
        {
            Character character = await context.Characters.SingleAsync(c => c.Id == _characterId);
            character.DockedStationId = _capitalId;
            await context.SaveChangesAsync();
        }

        await using (SpaceMmoDbContext context = _fixture.CreateContext())
        {
            Assert.Equal(1, await new QuestService(context).CountEveryoneDockedAsync());
        }

        Assert.Equal(QuestState.Completed, await StateOfAsync("fly"));

        // And again finds nothing: it is run on every seed.
        await using (SpaceMmoDbContext again = _fixture.CreateContext())
        {
            Assert.Equal(0, await new QuestService(again).CountEveryoneDockedAsync());
        }
    }

    private async Task AcceptAsync(string questKey)
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        await new QuestService(context).AcceptAsync(_characterId, questKey);
    }

    private async Task DockAsync(int stationId)
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        await new DockingService(context, new InventoryService(context)).DockAsync(_characterId, stationId);
    }

    private async Task<QuestState> StateOfAsync(string questKey)
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        return await context.CharacterQuests
            .Where(q => q.CharacterId == _characterId && q.QuestDef!.Key == questKey)
            .Select(q => q.State)
            .SingleAsync();
    }

    private async Task SeedAsync()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        var system = new StarSystem
        {
            Key = "system_origin", Name = "Origin", Seed = 42, GeneratorVersion = 1, SecurityLevel = SecurityLevel.Secure,
        };

        context.StarSystems.Add(system);
        await context.SaveChangesAsync();

        var body = new Body
        {
            Key = "body_capital", Name = "Capital", StarSystemId = system.Id, Kind = BodyKind.Planet,
            SecurityLevel = SecurityLevel.Secure, RadiusKm = 20,
        };

        context.Bodies.Add(body);
        await context.SaveChangesAsync();

        var capital = new Station
        {
            Key = "station_capital_hub", Name = "Borlash", StarSystemId = system.Id, BodyId = body.Id,
            Kind = StationKind.Capital, DirectionX = 1.0, DockingRangeKilometres = 5.0,
        };

        var outpost = new Station
        {
            Key = "station_outpost", Name = "Outpost", StarSystemId = system.Id, BodyId = body.Id,
            Kind = StationKind.TradingHub, DirectionX = -1.0, DockingRangeKilometres = 5.0,
        };

        context.Stations.AddRange(capital, outpost);

        var fly = new QuestDef
        {
            Key = "fly", Name = "Out of the Gravity Well", Kind = QuestKind.MainStory,
            RewardCredits = Credits.FromWholeCredits(5_000),
        };

        var gatherThenFly = new QuestDef
        {
            Key = "gather_then_fly", Name = "Two Steps", Kind = QuestKind.MainStory,
            RewardCredits = Credits.FromWholeCredits(10),
        };

        context.QuestDefs.AddRange(fly, gatherThenFly);
        await context.SaveChangesAsync();

        context.QuestSteps.AddRange(
            new QuestStep
            {
                QuestDefId = fly.Id, Ordinal = 1, Description = "Dock at the capital.",
                ObjectiveType = ObjectiveType.Dock, TargetKey = "station_capital_hub", Quantity = 1,
            },
            new QuestStep
            {
                QuestDefId = gatherThenFly.Id, Ordinal = 1, Description = "Gather ore.",
                ObjectiveType = ObjectiveType.Gather, TargetKey = "ore", Quantity = 5,
            },
            new QuestStep
            {
                QuestDefId = gatherThenFly.Id, Ordinal = 2, Description = "Dock at the capital.",
                ObjectiveType = ObjectiveType.Dock, TargetKey = "station_capital_hub", Quantity = 1,
            });

        var account = new Account { Email = "pilot@local.test", PasswordHash = "x", CreatedAt = DateTimeOffset.UtcNow };

        context.Accounts.Add(account);
        await context.SaveChangesAsync();

        var character = new Character
        {
            AccountId = account.Id, Name = "Ayla", Race = Race.Humanoid, HomeBodyId = body.Id,
            Balance = Credits.FromWholeCredits(100), CreatedAt = DateTimeOffset.UtcNow,
        };

        context.Characters.Add(character);
        await context.SaveChangesAsync();

        _characterId = character.Id;
        _capitalId = capital.Id;
        _outpostId = outpost.Id;
    }
}
