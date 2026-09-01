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
            .GatherAsync(_alice, _sharedNodeId);

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
    public async Task GatheredMaterial_GoesIntoTheCharactersHands()
    {
        // Not a station's storage. Gathering happens at a rock, on foot, nowhere near anywhere to
        // dock -- so there is no station to choose, and asking the client for one meant it invented
        // a fixed answer: every player's ore appeared at station 1 whatever planet they stood on,
        // which deleted the reason to fly anywhere (task 111, ADR-0012).
        //
        // Nothing caught that change when it was made, because no test said where ore lands. This
        // one does.
        await using SpaceMmoDbContext context = _fixture.CreateContext();
        await new GatheringService(context).GatherAsync(_alice, _sharedNodeId);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Inventory holding = await verify.Inventories
            .SingleAsync(i => i.StackedItems.Any(x => x.ItemDefId == _oreId));

        Assert.Equal(InventoryKind.CharacterCarried, holding.Kind);
        Assert.Equal(_alice, holding.CharacterId);
        Assert.Null(holding.StationId);
    }

    [Fact]
    public async Task GatheredMaterial_EntersAtZeroCostBasis()
    {
        // It took labour, not credits. That zero is what makes a hull built from self-gathered ore
        // cost only its manufacturing fees (ADR-0006).
        await using SpaceMmoDbContext context = _fixture.CreateContext();
        await new GatheringService(context).GatherAsync(_alice, _sharedNodeId);

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
        await new GatheringService(first).GatherAsync(_alice, _sharedNodeId);

        await using SpaceMmoDbContext second = _fixture.CreateContext();
        GatherResult result = await new GatheringService(second)
            .GatherAsync(_alice, _sharedNodeId);

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
            await new GatheringService(context).GatherAsync(_alice, _sharedNodeId);
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
            .GatherAsync(_alice, _sharedNodeId);

        // Level 50 takes 3 per tick against a beginner's 1.
        Assert.Equal(60, result.Quantity);
    }

    // ── Gates ────────────────────────────────────────────────────────────────

    [Fact]
    public async Task Gathering_WithoutTheRequiredTool_IsRejected()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        MissingToolException refused = await Assert.ThrowsAsync<MissingToolException>(() =>
            new GatheringService(context).GatherAsync(_alice, _toolGatedNodeId));

        // The message reaches the player's screen verbatim, so it is part of the behaviour rather
        // than a detail. It read "This recipe requires a crude_mining_laser" until deposits could
        // require tools too — telling somebody swinging at a rock about a recipe they were not
        // making, and naming it in snake case.
        Assert.Equal("crude_mining_laser", refused.ToolKey);
        Assert.Contains("Crude Mining Laser", refused.Message, StringComparison.Ordinal);
        Assert.DoesNotContain("recipe", refused.Message, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public async Task A_full_pack_takes_what_fits_and_leaves_the_rest_in_the_rock()
    {
        // The case ADR-0014 was amended for, the day it was written.
        //
        // Refusing the whole delivery is right for a purchase, where the goods already exist and
        // refusing part of one destroys the rest. Ore that will not fit is still in the ground, so
        // there is nothing to destroy by leaving it there -- and the first version of that rule made
        // mining impossible with anything at all in your hands.
        //
        // Filled to leave room for exactly three, rather than relying on a swing being bigger than a
        // pack: that happens to be true of the authored ore and is not what is under test.
        const int Room = 3;

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        Inventory pack = await new InventoryService(context).GetOrCreateCarriedAsync(_alice);

        await new InventoryService(context)
            .AddAsync(pack.Id, _oreId, PackHolds - Room, Credits.Zero);

        await context.SaveChangesAsync();

        int nodeSize = await context.ResourceNodes
            .Where(n => n.Id == _sharedNodeId)
            .Select(n => n.QuantityMax)
            .FirstAsync();

        GatherResult first = await new GatheringService(context).GatherAsync(_alice, _sharedNodeId);

        Assert.Equal(Room, first.Quantity);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        // <strong>The node is drawn down by what was taken, not by what was swung for.</strong> If
        // this ever reads twenty, a full pack is quietly deleting a node's contents.
        int nodeAfter = await verify.ResourceNodeStates
            .Where(s => s.ResourceNodeId == _sharedNodeId)
            .Select(s => s.QuantityRemaining)
            .FirstAsync();

        Assert.Equal(nodeSize - Room, nodeAfter);

        Assert.Equal(PackHolds, await HeldAsync(verify, _alice, _oreId));

        // And a swing into a pack with no room left takes nothing and leaves the node alone.
        //
        // The clock is reset first, or the cooldown answers before capacity ever gets asked -- which
        // is correct in the game and would make this test pass for the wrong reason. It is also the
        // order a player meets: a full pack behind a running cooldown still reads as "give it a
        // moment", because at that instant waiting genuinely is the next thing to do.
        await ResetGatherClockAsync(_alice);

        await using SpaceMmoDbContext again = _fixture.CreateContext();

        GatherResult none = await new GatheringService(again).GatherAsync(_alice, _sharedNodeId);

        Assert.Equal(0, none.Quantity);

        // And it says why. Zero has three reasons -- too soon, spent, no room -- and only two of
        // them are worth waiting out; a player told to give it a moment at a full pack will stand
        // there giving it a moment.
        Assert.True(none.NoRoom);

        await using SpaceMmoDbContext untouched = _fixture.CreateContext();

        Assert.Equal(
            nodeSize - Room,
            await untouched.ResourceNodeStates
                .Where(s => s.ResourceNodeId == _sharedNodeId)
                .Select(s => s.QuantityRemaining)
                .FirstAsync());
    }

    [Fact]
    public async Task Gathering_WithTheToolLeftInAHangar_IsRefused()
    {
        // Owning the laser is not holding it. Task 94 recorded that this check could not be
        // tightened, because nothing routed anything into a carried inventory and requiring the tool
        // on the character would have made mining impossible rather than stricter. Both halves of
        // that stopped being true once pockets existed and goods could be moved into them.
        await using (SpaceMmoDbContext seed = _fixture.CreateContext())
        {
            var inventories = new InventoryService(seed);

            Inventory hangar = await inventories.GetOrCreateStationHangarAsync(_alice, _stationId);

            seed.ItemInstances.Add(new ItemInstance
            {
                ItemDefId = _laserId,
                InventoryId = hangar.Id,
                Condition = 100,
                AcquisitionValue = Credits.FromWholeCredits(200),
                CreatedAt = DateTimeOffset.UtcNow,
            });

            await seed.SaveChangesAsync();
        }

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        await Assert.ThrowsAsync<MissingToolException>(() =>
            new GatheringService(context).GatherAsync(_alice, _toolGatedNodeId));
    }

    [Fact]
    public async Task Gathering_WithTheRequiredTool_IsAllowed()
    {
        await GiveToolAsync(_alice);

        await using SpaceMmoDbContext context = _fixture.CreateContext();
        GatherResult result = await new GatheringService(context)
            .GatherAsync(_alice, _toolGatedNodeId);

        Assert.False(result.IsEmpty);
    }

    [Fact]
    public async Task Gathering_BelowTheRequiredLevel_IsRejected()
    {
        long hardNodeId = await AddNodeAsync(
            NodeSharingModel.Shared, requiredLevel: 40, toolDefId: null);

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        SkillTooLowException error = await Assert.ThrowsAsync<SkillTooLowException>(() =>
            new GatheringService(context).GatherAsync(_alice, hardNodeId));

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
            .GatherAsync(_alice, smallNodeId);

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
            await new GatheringService(deplete).GatherAsync(_alice, smallNodeId);
        }

        await ResetGatherClockAsync(_alice);

        await using SpaceMmoDbContext context = _fixture.CreateContext();
        GatherResult result = await new GatheringService(context)
            .GatherAsync(_alice, smallNodeId);

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
            await new GatheringService(deplete).GatherAsync(_alice, smallNodeId);
        }

        await BackdateRespawnAsync(smallNodeId);
        await ResetGatherClockAsync(_alice);

        await using SpaceMmoDbContext context = _fixture.CreateContext();
        GatherResult result = await new GatheringService(context)
            .GatherAsync(_alice, smallNodeId);

        Assert.Equal(5, result.Quantity);
    }

    // ── Sharing models ───────────────────────────────────────────────────────

    [Fact]
    public async Task SharedNode_IsDrawnDownByEveryone()
    {
        await using (SpaceMmoDbContext first = _fixture.CreateContext())
        {
            await new GatheringService(first).GatherAsync(_alice, _sharedNodeId);
        }

        await using (SpaceMmoDbContext second = _fixture.CreateContext())
        {
            await new GatheringService(second).GatherAsync(_bob, _sharedNodeId);
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
            await new GatheringService(first).GatherAsync(_alice, _perCharacterNodeId);
        }

        await using (SpaceMmoDbContext second = _fixture.CreateContext())
        {
            await new GatheringService(second).GatherAsync(_bob, _perCharacterNodeId);
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
            await new GatheringService(shared).GatherAsync(_alice, _sharedNodeId);
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
            .GatherAsync(_bob, _sharedNodeId);

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
            Task.Run(() => aliceService.GatherAsync(_alice, smallNodeId)),
            Task.Run(() => bobService.GatherAsync(_bob, smallNodeId)),
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

                tasks.Add(Task.Run(() => service.GatherAsync(id, smallNodeId)));
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

        // On their person. Mining requires the tool carried, not merely owned: a laser in a hangar on
        // another planet is not a laser you are holding.
        Inventory carried = await inventories.GetOrCreateCarriedAsync(characterId);

        context.ItemInstances.Add(new ItemInstance
        {
            ItemDefId = _laserId,
            InventoryId = carried.Id,
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

        // Small on purpose.
        //
        // <strong>Capacity must not be the constraint in tests about gathering.</strong> Real
        // ferrite ore is 0.4 m3 and a swing is twenty units, which is more than a pack holds — so
        // with the authored volume every test here would be measuring ADR-0014 instead of yield,
        // XP, node depletion or sharing, and every failure would read as a gathering bug.
        //
        // The real volume is fed in by InventoryCapacityTests, which is where it belongs. Widening
        // the pack was tried first and cannot work: capacity is assigned on every access now, so a
        // test that sets it wide has it set back the moment the service looks.
        var ore = new ItemDef
        {
            Key = "ferrite_ore", Name = "Ferrite Ore", Category = ItemCategory.Raw,
            VolumeM3 = FixtureOreVolumeM3,
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

    /// <summary>How many units of this fixture's ore a pack holds.</summary>
    /// <remarks>
    /// Derived rather than written down, so tuning the carried capacity does not silently change
    /// what these tests mean. A literal would have to be edited every time somebody moved the
    /// number, which trains whoever is moving it to edit tests without reading them.
    /// </remarks>
    /// <summary>
    /// What this fixture's ore takes up, which is not what the authored one takes up.
    /// </summary>
    private const double FixtureOreVolumeM3 = 0.02;

    private static int PackHolds =>
        (int)Math.Floor(InventoryService.CarriedCapacityM3 / FixtureOreVolumeM3);
}
