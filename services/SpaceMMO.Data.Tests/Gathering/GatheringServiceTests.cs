using Microsoft.EntityFrameworkCore;
using SpaceMMO.Data.Entities;
using SpaceMMO.Data.Gathering;
using SpaceMMO.Data.Industry;
using SpaceMMO.Data.Inventories;
using SpaceMMO.Domain.Characters;
using SpaceMMO.Domain.Economy;
using SpaceMMO.Domain.Gathering;
using SpaceMMO.Domain.Items;
using SpaceMMO.Domain.Progression;
using SpaceMMO.Domain.Universe;
using Xunit;

namespace SpaceMMO.Data.Tests.Gathering;

/// <summary>
/// Integration tests for resource gathering.
/// </summary>
/// <remarks>
/// The contention tests are the point. Gathering is the only place material enters the economy,
/// so a shared node that can be over-extracted is a material faucet — the one bug class that
/// breaks a player-driven economy outright.
/// </remarks>
[Collection(SharedDatabase.Name)]
public sealed class GatheringServiceTests(DatabaseFixture fixture) : IAsyncLifetime
{
    private readonly DatabaseFixture _fixture = fixture;

    private const int NodeCapacity = 200;

    private int _stationId;
    private int _miningSkillId;
    private int _oreId;
    private int _laserId;
    private int _alice;
    private int _bob;

    private long _sharedNodeId;
    private long _perCharacterNodeId;
    private long _toolGatedNodeId;

    public async Task InitializeAsync()
    {
        await _fixture.ResetAsync();
        await SeedAsync();
    }

    public Task DisposeAsync() => Task.CompletedTask;

    // ── Basic extraction ─────────────────────────────────────────────────────

    [Fact]
    public async Task Gathering_ExtractsMaterialAndAwardsXp()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        GatherResult result = await new GatheringService(context)
            .GatherAsync(_alice, _sharedNodeId, _stationId);

        // A character who has never gathered starts fully rested: 20 banked ticks at 1 unit each.
        Assert.Equal(20, result.Quantity);
        Assert.Equal(20 * GatheringYield.XpPerUnit, result.XpAwarded);
        Assert.Equal(NodeCapacity - 20, result.NodeRemaining);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.Equal(20, await HeldAsync(verify, _alice, _oreId));

        CharacterSkill skill = await verify.CharacterSkills
            .SingleAsync(s => s.CharacterId == _alice);

        Assert.Equal(100, skill.Xp);
    }

    [Fact]
    public async Task GatheredMaterial_EntersAtZeroCostBasis()
    {
        // It took labour, not credits. That zero is what makes a hull built from self-gathered ore
        // cost only its manufacturing fees (ADR-0006).
        await using SpaceMmoDbContext context = _fixture.CreateContext();
        await new GatheringService(context).GatherAsync(_alice, _sharedNodeId, _stationId);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        InventoryItem stack = await verify.InventoryItems.SingleAsync(i => i.ItemDefId == _oreId);

        Assert.True(stack.CostBasis.IsZero);
    }

    [Fact]
    public async Task GatheringAgainImmediately_YieldsNothing()
    {
        // The server rate limit. A client in a tight loop extracts no more than one calling at the
        // tick interval.
        await using SpaceMmoDbContext first = _fixture.CreateContext();
        await new GatheringService(first).GatherAsync(_alice, _sharedNodeId, _stationId);

        await using SpaceMmoDbContext second = _fixture.CreateContext();
        GatherResult result = await new GatheringService(second)
            .GatherAsync(_alice, _sharedNodeId, _stationId);

        Assert.True(result.IsEmpty);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();
        Assert.Equal(20, await HeldAsync(verify, _alice, _oreId));
    }

    [Fact]
    public async Task RepeatedCalls_CannotSpendTheSameBankedTimeTwice()
    {
        // Ten calls in a row must extract exactly what one would.
        for (int i = 0; i < 10; i++)
        {
            await using SpaceMmoDbContext context = _fixture.CreateContext();
            await new GatheringService(context).GatherAsync(_alice, _sharedNodeId, _stationId);
        }

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.Equal(20, await HeldAsync(verify, _alice, _oreId));
        Assert.Equal(NodeCapacity - 20, await RemainingAsync(verify, _sharedNodeId, owner: null));
    }

    [Fact]
    public async Task HigherSkill_ExtractsMorePerTick()
    {
        await GrantSkillAsync(_alice, SkillCurve.XpForLevel(50));

        await using SpaceMmoDbContext context = _fixture.CreateContext();
        GatherResult result = await new GatheringService(context)
            .GatherAsync(_alice, _sharedNodeId, _stationId);

        // Level 50 takes 3 per tick against a beginner's 1.
        Assert.Equal(60, result.Quantity);
    }

    // ── Gates ────────────────────────────────────────────────────────────────

    [Fact]
    public async Task Gathering_WithoutTheRequiredTool_IsRejected()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        MissingToolException refused = await Assert.ThrowsAsync<MissingToolException>(() =>
            new GatheringService(context).GatherAsync(_alice, _toolGatedNodeId, _stationId));

        // The message reaches the player's screen verbatim, so it is part of the behaviour rather
        // than a detail. It read "This recipe requires a crude_mining_laser" until deposits could
        // require tools too — telling somebody swinging at a rock about a recipe they were not
        // making, and naming it in snake case.
        Assert.Equal("crude_mining_laser", refused.ToolKey);
        Assert.Contains("Crude Mining Laser", refused.Message, StringComparison.Ordinal);
        Assert.DoesNotContain("recipe", refused.Message, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public async Task Gathering_WithTheRequiredTool_IsAllowed()
    {
        await GiveToolAsync(_alice);

        await using SpaceMmoDbContext context = _fixture.CreateContext();
        GatherResult result = await new GatheringService(context)
            .GatherAsync(_alice, _toolGatedNodeId, _stationId);

        Assert.False(result.IsEmpty);
    }

    [Fact]
    public async Task Gathering_BelowTheRequiredLevel_IsRejected()
    {
        long hardNodeId = await AddNodeAsync(
            NodeSharingModel.Shared, requiredLevel: 40, toolDefId: null);

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        SkillTooLowException error = await Assert.ThrowsAsync<SkillTooLowException>(() =>
            new GatheringService(context).GatherAsync(_alice, hardNodeId, _stationId));

        Assert.Equal(40, error.Required);
    }

    // ── Depletion and respawn ────────────────────────────────────────────────

    [Fact]
    public async Task DepletingANode_SetsARespawnTime()
    {
        long smallNodeId = await AddNodeAsync(
            NodeSharingModel.Shared, requiredLevel: 1, toolDefId: null, capacity: 5);

        await using SpaceMmoDbContext context = _fixture.CreateContext();
        GatherResult result = await new GatheringService(context)
            .GatherAsync(_alice, smallNodeId, _stationId);

        // Capped by what the node holds, not by the entitlement.
        Assert.Equal(5, result.Quantity);
        Assert.True(result.Depleted);
        Assert.NotNull(result.RespawnAt);
    }

    [Fact]
    public async Task GatheringFromADepletedNode_YieldsNothing()
    {
        long smallNodeId = await AddNodeAsync(
            NodeSharingModel.Shared, requiredLevel: 1, toolDefId: null, capacity: 5);

        await using (SpaceMmoDbContext deplete = _fixture.CreateContext())
        {
            await new GatheringService(deplete).GatherAsync(_alice, smallNodeId, _stationId);
        }

        await ResetGatherClockAsync(_alice);

        await using SpaceMmoDbContext context = _fixture.CreateContext();
        GatherResult result = await new GatheringService(context)
            .GatherAsync(_alice, smallNodeId, _stationId);

        Assert.True(result.IsEmpty);
    }

    [Fact]
    public async Task ADepletedNode_RefillsLazilyOnceItsRespawnPasses()
    {
        // No background sweeper: the node refills when someone next tries to work it.
        long smallNodeId = await AddNodeAsync(
            NodeSharingModel.Shared, requiredLevel: 1, toolDefId: null, capacity: 5);

        await using (SpaceMmoDbContext deplete = _fixture.CreateContext())
        {
            await new GatheringService(deplete).GatherAsync(_alice, smallNodeId, _stationId);
        }

        await BackdateRespawnAsync(smallNodeId);
        await ResetGatherClockAsync(_alice);

        await using SpaceMmoDbContext context = _fixture.CreateContext();
        GatherResult result = await new GatheringService(context)
            .GatherAsync(_alice, smallNodeId, _stationId);

        Assert.Equal(5, result.Quantity);
    }

    // ── Sharing models ───────────────────────────────────────────────────────

    [Fact]
    public async Task SharedNode_IsDrawnDownByEveryone()
    {
        await using (SpaceMmoDbContext first = _fixture.CreateContext())
        {
            await new GatheringService(first).GatherAsync(_alice, _sharedNodeId, _stationId);
        }

        await using (SpaceMmoDbContext second = _fixture.CreateContext())
        {
            await new GatheringService(second).GatherAsync(_bob, _sharedNodeId, _stationId);
        }

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        // Both drew from one pool: 200 - 20 - 20.
        Assert.Equal(NodeCapacity - 40, await RemainingAsync(verify, _sharedNodeId, owner: null));

        // And exactly one shared state row exists.
        Assert.Equal(
            1,
            await verify.ResourceNodeStates.CountAsync(s => s.ResourceNodeId == _sharedNodeId));
    }

    [Fact]
    public async Task PerCharacterNode_GivesEachGathererTheirOwnPool()
    {
        await using (SpaceMmoDbContext first = _fixture.CreateContext())
        {
            await new GatheringService(first).GatherAsync(_alice, _perCharacterNodeId, _stationId);
        }

        await using (SpaceMmoDbContext second = _fixture.CreateContext())
        {
            await new GatheringService(second).GatherAsync(_bob, _perCharacterNodeId, _stationId);
        }

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        // Neither depleted the other's share.
        Assert.Equal(
            NodeCapacity - 20, await RemainingAsync(verify, _perCharacterNodeId, owner: _alice));
        Assert.Equal(
            NodeCapacity - 20, await RemainingAsync(verify, _perCharacterNodeId, owner: _bob));

        Assert.Equal(
            2,
            await verify.ResourceNodeStates.CountAsync(
                s => s.ResourceNodeId == _perCharacterNodeId));
    }

    [Fact]
    public async Task SwitchingANodeBetweenModels_NeedsOnlyAColumnUpdate()
    {
        // The escape hatch: if shared nodes make the starting planets miserable, those specific
        // nodes flip to per-character with an UPDATE and no migration.
        await using (SpaceMmoDbContext shared = _fixture.CreateContext())
        {
            await new GatheringService(shared).GatherAsync(_alice, _sharedNodeId, _stationId);
        }

        await using (SpaceMmoDbContext flip = _fixture.CreateContext())
        {
            ResourceNode node = await flip.ResourceNodes.SingleAsync(n => n.Id == _sharedNodeId);
            node.SharingModel = NodeSharingModel.PerCharacter;
            await flip.SaveChangesAsync();
        }

        await ResetGatherClockAsync(_bob);

        await using SpaceMmoDbContext context = _fixture.CreateContext();
        GatherResult result = await new GatheringService(context)
            .GatherAsync(_bob, _sharedNodeId, _stationId);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        // Bob now draws from a fresh pool of his own, leaving the old shared pool untouched.
        Assert.Equal(20, result.Quantity);
        Assert.Equal(NodeCapacity - 20, result.NodeRemaining);
        Assert.Equal(NodeCapacity - 20, await RemainingAsync(verify, _sharedNodeId, owner: null));
    }

    // ── Contention ───────────────────────────────────────────────────────────

    [Fact]
    public async Task ConcurrentGatherers_CannotOverExtractASharedNode()
    {
        // The material-faucet case. Both read "5 remaining", both extract 5, and ten units of ore
        // enter an economy that only had five.
        long smallNodeId = await AddNodeAsync(
            NodeSharingModel.Shared, requiredLevel: 1, toolDefId: null, capacity: 5);

        await using SpaceMmoDbContext aliceContext = _fixture.CreateContext();
        await using SpaceMmoDbContext bobContext = _fixture.CreateContext();

        var aliceService = new GatheringService(aliceContext);
        var bobService = new GatheringService(bobContext);

        Task<GatherResult>[] tasks =
        [
            Task.Run(() => aliceService.GatherAsync(_alice, smallNodeId, _stationId)),
            Task.Run(() => bobService.GatherAsync(_bob, smallNodeId, _stationId)),
        ];

        GatherResult[] results = await Task.WhenAll(tasks);

        Assert.Equal(5, results.Sum(r => r.Quantity));

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.Equal(0, await RemainingAsync(verify, smallNodeId, owner: null));

        int oreInWorld = await verify.InventoryItems
            .Where(i => i.ItemDefId == _oreId)
            .SumAsync(i => i.Quantity);

        Assert.Equal(5, oreInWorld);
    }

    [Fact]
    public async Task ManyConcurrentGatherers_ExtractExactlyWhatTheNodeHeld()
    {
        long smallNodeId = await AddNodeAsync(
            NodeSharingModel.Shared, requiredLevel: 1, toolDefId: null, capacity: 30);

        var characterIds = new List<int>();

        for (int i = 0; i < 6; i++)
        {
            characterIds.Add(await AddCharacterAsync($"Gatherer{i}"));
        }

        var contexts = new List<SpaceMmoDbContext>();
        var tasks = new List<Task<GatherResult>>();

        try
        {
            foreach (int characterId in characterIds)
            {
                SpaceMmoDbContext context = _fixture.CreateContext();
                contexts.Add(context);

                var service = new GatheringService(context);
                int id = characterId;

                tasks.Add(Task.Run(() => service.GatherAsync(id, smallNodeId, _stationId)));
            }

            GatherResult[] results = await Task.WhenAll(tasks);

            // Six gatherers entitled to 20 each, chasing 30 units.
            Assert.Equal(30, results.Sum(r => r.Quantity));
        }
        finally
        {
            foreach (SpaceMmoDbContext context in contexts)
            {
                await context.DisposeAsync();
            }
        }

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.Equal(0, await RemainingAsync(verify, smallNodeId, owner: null));
        Assert.Equal(
            30,
            await verify.InventoryItems.Where(i => i.ItemDefId == _oreId).SumAsync(i => i.Quantity));
    }

    // ── Helpers ──────────────────────────────────────────────────────────────

    private static async Task<int> HeldAsync(
        SpaceMmoDbContext context, int characterId, int itemDefId) =>
        await context.InventoryItems
            .Where(i => i.ItemDefId == itemDefId
                && context.Inventories.Any(inv => inv.Id == i.InventoryId
                    && inv.CharacterId == characterId))
            .SumAsync(i => i.Quantity);

    private static async Task<int> RemainingAsync(
        SpaceMmoDbContext context, long nodeId, int? owner) =>
        await context.ResourceNodeStates
            .Where(s => s.ResourceNodeId == nodeId && s.CharacterId == owner)
            .Select(s => s.QuantityRemaining)
            .SingleAsync();

    private async Task ResetGatherClockAsync(int characterId)
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        Character character = await context.Characters.SingleAsync(c => c.Id == characterId);
        character.LastGatheredAt = null;

        await context.SaveChangesAsync();
    }

    private async Task BackdateRespawnAsync(long nodeId)
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        ResourceNodeState state = await context.ResourceNodeStates
            .SingleAsync(s => s.ResourceNodeId == nodeId);

        state.RespawnAt = DateTimeOffset.UtcNow.AddSeconds(-1);

        await context.SaveChangesAsync();
    }

    private async Task GrantSkillAsync(int characterId, long xp)
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        context.CharacterSkills.Add(new CharacterSkill
        {
            CharacterId = characterId,
            SkillId = _miningSkillId,
            Xp = xp,
        });

        await context.SaveChangesAsync();
    }

    private async Task GiveToolAsync(int characterId)
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();
        var inventories = new InventoryService(context);

        Inventory hangar = await inventories.GetOrCreateStationHangarAsync(characterId, _stationId);

        context.ItemInstances.Add(new ItemInstance
        {
            ItemDefId = _laserId,
            InventoryId = hangar.Id,
            Condition = 100,
            AcquisitionValue = Credits.FromWholeCredits(200),
            CreatedAt = DateTimeOffset.UtcNow,
        });

        await context.SaveChangesAsync();
    }

    private async Task<long> AddNodeAsync(
        NodeSharingModel model, int requiredLevel, int? toolDefId, int capacity = NodeCapacity)
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        int bodyId = await context.Bodies.Select(b => b.Id).FirstAsync();
        int systemId = await context.StarSystems.Select(s => s.Id).FirstAsync();

        var node = new ResourceNode
        {
            // Unique per call: this helper is used several times in a single test, and content
            // keys are unique by design so authored deposits upsert rather than duplicating.
            Key = $"node_test_{Guid.NewGuid():N}",

            // Every deposit needs somewhere on its body to be, and the schema now enforces it.
            DirectionX = 1.0,
            StarSystemId = systemId,
            BodyId = bodyId,
            ItemDefId = _oreId,
            QuantityMax = capacity,
            RespawnSeconds = 1_200,
            SkillId = _miningSkillId,
            RequiredLevel = requiredLevel,
            RequiredToolItemDefId = toolDefId,
            SharingModel = model,
        };

        context.ResourceNodes.Add(node);
        await context.SaveChangesAsync();

        return node.Id;
    }

    private async Task<int> AddCharacterAsync(string name)
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        int accountId = await context.Accounts.Select(a => a.Id).FirstAsync();
        int bodyId = await context.Bodies.Select(b => b.Id).FirstAsync();

        var character = new Character
        {
            AccountId = accountId,
            Name = name,
            Race = Race.Humanoid,
            HomeBodyId = bodyId,
            Balance = Credits.FromWholeCredits(13_000),
            CreatedAt = DateTimeOffset.UtcNow,
        };

        context.Characters.Add(character);
        await context.SaveChangesAsync();

        return character.Id;
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

        var mining = new Skill { Key = "mining", Name = "Mining", Category = SkillCategory.Life };
        context.Skills.Add(mining);

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
            Email = "gathering@example.com",
            PasswordHash = "not-a-real-hash",
            CreatedAt = DateTimeOffset.UtcNow,
        };
        context.Accounts.Add(account);
        await context.SaveChangesAsync();

        var alice = new Character
        {
            AccountId = account.Id,
            Name = "Alice",
            Race = Race.Humanoid,
            HomeBodyId = body.Id,
            Balance = Credits.FromWholeCredits(13_000),
            CreatedAt = DateTimeOffset.UtcNow,
        };

        var bob = new Character
        {
            AccountId = account.Id,
            Name = "Bob",
            Race = Race.Martian,
            HomeBodyId = body.Id,
            Balance = Credits.FromWholeCredits(13_000),
            CreatedAt = DateTimeOffset.UtcNow,
        };

        context.Characters.AddRange(alice, bob);
        await context.SaveChangesAsync();

        _stationId = station.Id;
        _miningSkillId = mining.Id;
        _oreId = ore.Id;
        _laserId = laser.Id;
        _alice = alice.Id;
        _bob = bob.Id;

        _sharedNodeId = await AddNodeAsync(NodeSharingModel.Shared, 1, null);
        _perCharacterNodeId = await AddNodeAsync(NodeSharingModel.PerCharacter, 1, null);
        _toolGatedNodeId = await AddNodeAsync(NodeSharingModel.Shared, 1, laser.Id);
    }
}
