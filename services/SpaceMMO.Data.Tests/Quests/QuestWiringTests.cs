using Microsoft.EntityFrameworkCore;
using SpaceMMO.Data;
using SpaceMMO.Data.Entities;
using SpaceMMO.Data.Gathering;
using SpaceMMO.Data.Industry;
using SpaceMMO.Data.Quests;
using SpaceMMO.Domain.Characters;
using SpaceMMO.Domain.Economy;
using SpaceMMO.Domain.Gathering;
using SpaceMMO.Domain.Items;
using SpaceMMO.Domain.Progression;
using SpaceMMO.Domain.Quests;
using SpaceMMO.Domain.Universe;
using Xunit;

namespace SpaceMMO.Data.Tests.Quests;

/// <summary>
/// That doing the thing a quest asks for actually advances the quest.
/// </summary>
/// <remarks>
/// <para>
/// The quest engine was complete, tested and connected to nothing:
/// <c>RecordProgressAsync</c> was called only from its own tests, so a player could accept a quest
/// and then no action they took would ever move it. Every test here crosses a seam between two
/// services, which is precisely where that kind of gap hides — each side passing its own tests
/// while nothing joins them.
/// </para>
/// <para>
/// The verb cases matter most. Objectives match on type <em>and</em> target exactly, so a claim
/// reported as Craft against a step authored as Refine advances nothing and reports no error.
/// </para>
/// </remarks>
[Collection(SharedDatabase.Name)]
public sealed class QuestWiringTests(DatabaseFixture fixture) : IAsyncLifetime
{
    private readonly DatabaseFixture _fixture = fixture;

    private int _characterId;
    private int _stationId;
    private int _oreId;
    private int _plateId;
    private int _refiningId;
    private long _nodeId;
    private int _refineRecipeId;

    public async Task InitializeAsync()
    {
        await _fixture.ResetAsync();
        await SeedAsync();
    }

    public Task DisposeAsync() => Task.CompletedTask;

    [Fact]
    public async Task Gathering_advances_a_gather_objective()
    {
        await AcceptAsync("mine_ore", ObjectiveType.Gather, "ferrite_ore", 20);

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        GatherResult result = await new GatheringService(context)
            .GatherAsync(_characterId, _nodeId, _stationId);

        Assert.True(result.Quantity > 0);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        CharacterQuest quest = await verify.CharacterQuests
            .SingleAsync(q => q.QuestDef!.Key == "mine_ore");

        // The seam. Before this was wired, mining moved ore into a hangar and left the quest at
        // zero forever, with nothing anywhere saying why.
        Assert.Equal(result.Quantity, quest.StepProgress);
    }

    [Fact]
    public async Task Gathering_something_else_advances_nothing()
    {
        await AcceptAsync("mine_ore", ObjectiveType.Gather, "scrap_alloy", 5);

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        await new GatheringService(context).GatherAsync(_characterId, _nodeId, _stationId);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        // Ore is not scrap. Matching is exact on the target key, and a wiring that reported the
        // wrong item would quietly satisfy the wrong quest.
        Assert.Equal(
            0,
            (await verify.CharacterQuests.SingleAsync(q => q.QuestDef!.Key == "mine_ore"))
                .StepProgress);
    }

    [Fact]
    public async Task Claiming_a_refining_job_advances_a_refine_objective()
    {
        await AcceptAsync("make_plate", ObjectiveType.Refine, "ferrite_plate", 4);
        await StockAsync();

        await using SpaceMmoDbContext start = _fixture.CreateContext();

        StartJobResult job = await new IndustryService(start)
            .StartJobAsync(_characterId, _refineRecipeId, _stationId, 1);

        await FinishJobAsync(job.JobId);

        await using SpaceMmoDbContext claim = _fixture.CreateContext();

        await new IndustryService(claim).ClaimJobAsync(job.JobId, _characterId);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        CharacterQuest quest = await verify.CharacterQuests
            .SingleAsync(q => q.QuestDef!.Key == "make_plate");

        // Refining reports Refine, not Craft. The two are the same operation with different words
        // for the player, and reporting the wrong one advances nothing while erroring nowhere.
        Assert.Equal(4, quest.StepProgress);
        Assert.Equal(QuestState.Completed, quest.State);
    }

    [Fact]
    public async Task Starting_a_job_advances_nothing_until_it_is_claimed()
    {
        await AcceptAsync("make_plate", ObjectiveType.Refine, "ferrite_plate", 4);
        await StockAsync();

        await using SpaceMmoDbContext start = _fixture.CreateContext();

        await new IndustryService(start).StartJobAsync(_characterId, _refineRecipeId, _stationId, 1);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        // Inputs are consumed at start but nothing has been produced, and the job can still be
        // cancelled. Crediting the start would pay a step for output that may never exist.
        Assert.Equal(
            0,
            (await verify.CharacterQuests.SingleAsync(q => q.QuestDef!.Key == "make_plate"))
                .StepProgress);
    }

    private async Task AcceptAsync(
        string key, ObjectiveType objective, string target, int quantity)
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        var quest = new QuestDef
        {
            Key = key,
            Name = key,
            Kind = QuestKind.MainStory,
            RewardCredits = Credits.FromWholeCredits(10),
        };

        context.QuestDefs.Add(quest);
        await context.SaveChangesAsync();

        context.QuestSteps.Add(new QuestStep
        {
            QuestDefId = quest.Id,
            Ordinal = 1,
            ObjectiveType = objective,
            TargetKey = target,
            Quantity = quantity,
            Description = key,
        });

        await context.SaveChangesAsync();

        await new QuestService(context).AcceptAsync(_characterId, key);
    }

    /// <summary>Puts ore and credits where a refining job can reach them.</summary>
    private async Task StockAsync()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        Character character = context.Characters.Single(c => c.Id == _characterId);
        character.Balance = Credits.FromWholeCredits(100);

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
            Quantity = 40,
            CostBasis = Credits.Zero,
        });

        await context.SaveChangesAsync();
    }

    /// <summary>Winds a job's completion time into the past so it can be claimed.</summary>
    private async Task FinishJobAsync(long jobId)
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        IndustryJob job = context.IndustryJobs.Single(j => j.Id == jobId);
        job.CompletesAt = DateTimeOffset.UtcNow.AddSeconds(-1);

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
            Email = "wiring@example.com",
            PasswordHash = "not-a-real-hash",
            CreatedAt = DateTimeOffset.UtcNow,
        };

        var mining = new Skill { Key = "mining", Name = "Mining", Category = SkillCategory.Life };
        var refining = new Skill { Key = "refining", Name = "Refining", Category = SkillCategory.Life };

        var ore = new ItemDef
        {
            Key = "ferrite_ore",
            Name = "Ferrite Ore",
            Category = ItemCategory.Raw,
            VolumeM3 = 0.4,
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
        context.Skills.AddRange(mining, refining);
        context.ItemDefs.AddRange(ore, plate);
        await context.SaveChangesAsync();

        var character = new Character
        {
            AccountId = account.Id,
            Name = "Wirer",
            Race = Race.Humanoid,
            HomeBodyId = body.Id,
            CreatedAt = DateTimeOffset.UtcNow,
        };

        var node = new ResourceNode
        {
            Key = "node_terra_ferrite",
            StarSystemId = system.Id,
            BodyId = body.Id,
            ItemDefId = ore.Id,
            SkillId = mining.Id,
            RequiredLevel = 1,
            QuantityMax = 200,
            RespawnSeconds = 1200,
            DirectionX = 1.0,
            DirectionY = 0.0,
            DirectionZ = 0.0,
            SharingModel = NodeSharingModel.Shared,
        };

        var recipe = new Recipe
        {
            Key = "refine_ferrite_plate",
            OutputItemDefId = plate.Id,
            OutputQuantity = 4,
            SkillId = refining.Id,
            RequiredLevel = 1,
            JobSeconds = 60,
            XpPerRun = 600,
        };

        context.Characters.Add(character);
        context.ResourceNodes.Add(node);
        context.Recipes.Add(recipe);
        await context.SaveChangesAsync();

        context.RecipeInputs.Add(new RecipeInput
        {
            RecipeId = recipe.Id,
            ItemDefId = ore.Id,
            Quantity = 20,
        });

        await context.SaveChangesAsync();

        _characterId = character.Id;
        _stationId = station.Id;
        _oreId = ore.Id;
        _plateId = plate.Id;
        _refiningId = refining.Id;
        _nodeId = node.Id;
        _refineRecipeId = recipe.Id;
    }
}
