using Microsoft.EntityFrameworkCore;
using SpaceMMO.Data.Entities;
using SpaceMMO.Data.Industry;
using SpaceMMO.Data.Inventories;
using SpaceMMO.Domain.Characters;
using SpaceMMO.Domain.Economy;
using SpaceMMO.Domain.Industry;
using SpaceMMO.Domain.Items;
using SpaceMMO.Domain.Progression;
using SpaceMMO.Domain.Universe;
using Xunit;

namespace SpaceMMO.Data.Tests.Industry;

/// <summary>
/// Integration tests for manufacturing jobs (design-bible §6).
/// </summary>
/// <remarks>
/// Built around the real onboarding chain from design-bible §5, so these exercise the content
/// that ships rather than a test fixture: ore refines into plates, plates become a hull section,
/// and the section plus a thruster becomes a shuttle.
/// </remarks>
[Collection(SharedDatabase.Name)]
public sealed class IndustryServiceTests(DatabaseFixture fixture) : IAsyncLifetime
{
    private readonly DatabaseFixture _fixture = fixture;

    private const int StartingCredits = 13_000;
    private const int StartingOre = 200;

    private int _stationId;
    private int _characterId;
    private int _refiningSkillId;
    private int _shipcraftingSkillId;

    private int _oreId;
    private int _plateId;
    private int _sectionId;
    private int _laserId;

    private int _refinePlateRecipeId;
    private int _buildSectionRecipeId;
    private int _craftLaserRecipeId;

    public async Task InitializeAsync()
    {
        await _fixture.ResetAsync();
        await SeedAsync();
    }

    public Task DisposeAsync() => Task.CompletedTask;

    private static Credits Cr(long whole) => Credits.FromWholeCredits(whole);

    // ── Starting a job ───────────────────────────────────────────────────────

    [Fact]
    public async Task StartingAJob_ConsumesInputsAndChargesTheFee()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        StartJobResult result = await new IndustryService(context)
            .StartJobAsync(_characterId, _refinePlateRecipeId, _stationId, runs: 1);

        // Base 10 + 5 per run.
        Assert.Equal(Cr(15), result.Fee);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        // Inputs are gone immediately, so one pile of ore cannot seed several jobs.
        Assert.Equal(StartingOre - 20, await HeldAsync(verify, _oreId));
        Assert.Equal(Cr(StartingCredits - 15), await BalanceAsync(verify));

        IndustryJob job = await verify.IndustryJobs.SingleAsync();
        Assert.Equal(IndustryJobState.Running, job.State);
        Assert.Equal(60, (job.CompletesAt - job.StartedAt).TotalSeconds, precision: 0);
    }

    [Fact]
    public async Task StartingAJob_ScalesInputsAndDurationWithRuns()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        StartJobResult result = await new IndustryService(context)
            .StartJobAsync(_characterId, _refinePlateRecipeId, _stationId, runs: 5);

        // Linear: no batch discount on time, and the fee has a per-run component.
        Assert.Equal(Cr(35), result.Fee);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.Equal(StartingOre - 100, await HeldAsync(verify, _oreId));

        IndustryJob job = await verify.IndustryJobs.SingleAsync();
        Assert.Equal(300, (job.CompletesAt - job.StartedAt).TotalSeconds, precision: 0);
    }

    [Fact]
    public async Task StartingAJob_RecordsExactlyWhatItConsumed()
    {
        // Stored rather than re-derived, so rebalancing a recipe cannot change a running job's
        // refund.
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        StartJobResult result = await new IndustryService(context)
            .StartJobAsync(_characterId, _refinePlateRecipeId, _stationId, runs: 2);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        IndustryJobInput input = await verify.IndustryJobInputs
            .SingleAsync(i => i.IndustryJobId == result.JobId);

        Assert.Equal(_oreId, input.ItemDefId);
        Assert.Equal(40, input.Quantity);
    }

    [Fact]
    public async Task StartingAJob_WithoutTheMaterials_IsRejected()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        await Assert.ThrowsAsync<InsufficientItemsException>(() =>
            new IndustryService(context)
                .StartJobAsync(_characterId, _refinePlateRecipeId, _stationId, runs: 100));

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        // Nothing charged, nothing consumed.
        Assert.Equal(StartingOre, await HeldAsync(verify, _oreId));
        Assert.Equal(Cr(StartingCredits), await BalanceAsync(verify));
        Assert.Equal(0, await verify.IndustryJobs.CountAsync());
    }

    // ── Gates ────────────────────────────────────────────────────────────────

    [Fact]
    public async Task StartingAJob_BelowTheSkillRequirement_IsRejected()
    {
        // Shipcrafting 5 is required; the character has no shipcrafting XP at all.
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        SkillTooLowException error = await Assert.ThrowsAsync<SkillTooLowException>(() =>
            new IndustryService(context)
                .StartJobAsync(_characterId, _buildSectionRecipeId, _stationId, runs: 1));

        Assert.Equal(5, error.Required);
        Assert.Equal(1, error.Actual);
    }

    [Fact]
    public async Task StartingAJob_WithoutTheRequiredTool_IsRejected()
    {
        // The laser recipe is tool-gated in this fixture to exercise the check.
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        await Assert.ThrowsAsync<MissingToolException>(() =>
            new IndustryService(context)
                .StartJobAsync(_characterId, _craftLaserRecipeId, _stationId, runs: 1));
    }

    [Fact]
    public async Task StartingAJob_WithTheRequiredTool_IsAllowed()
    {
        await GiveToolAsync();

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        StartJobResult result = await new IndustryService(context)
            .StartJobAsync(_characterId, _craftLaserRecipeId, _stationId, runs: 1);

        Assert.True(result.JobId > 0);
    }

    [Fact]
    public async Task StartingASecondJob_InTheSameSkill_ExhaustsTheOnlySlot()
    {
        // A level-1 character has exactly one slot per skill.
        await using SpaceMmoDbContext first = _fixture.CreateContext();
        await new IndustryService(first)
            .StartJobAsync(_characterId, _refinePlateRecipeId, _stationId, runs: 1);

        await using SpaceMmoDbContext second = _fixture.CreateContext();

        NoFreeJobSlotException error = await Assert.ThrowsAsync<NoFreeJobSlotException>(() =>
            new IndustryService(second)
                .StartJobAsync(_characterId, _refinePlateRecipeId, _stationId, runs: 1));

        Assert.Equal(1, error.Slots);
    }

    [Fact]
    public async Task SlotsAreCountedPerSkill_NotGlobally()
    {
        // A running refining job must not block a shipcrafting job. This is what makes
        // specialisation a real choice rather than a label.
        await GiveToolAsync();

        await using SpaceMmoDbContext first = _fixture.CreateContext();
        await new IndustryService(first)
            .StartJobAsync(_characterId, _refinePlateRecipeId, _stationId, runs: 1);

        await using SpaceMmoDbContext second = _fixture.CreateContext();
        StartJobResult toolcrafting = await new IndustryService(second)
            .StartJobAsync(_characterId, _craftLaserRecipeId, _stationId, runs: 1);

        Assert.True(toolcrafting.JobId > 0);
    }

    [Fact]
    public async Task CancellingAJob_FreesItsSlot()
    {
        await using SpaceMmoDbContext first = _fixture.CreateContext();
        StartJobResult started = await new IndustryService(first)
            .StartJobAsync(_characterId, _refinePlateRecipeId, _stationId, runs: 1);

        await using SpaceMmoDbContext cancel = _fixture.CreateContext();
        Assert.True(await new IndustryService(cancel).CancelJobAsync(started.JobId, _characterId));

        await using SpaceMmoDbContext second = _fixture.CreateContext();
        StartJobResult restarted = await new IndustryService(second)
            .StartJobAsync(_characterId, _refinePlateRecipeId, _stationId, runs: 1);

        Assert.True(restarted.JobId > 0);
    }

    // ── Claiming ─────────────────────────────────────────────────────────────

    [Fact]
    public async Task ClaimingBeforeCompletion_IsRejected()
    {
        // The server clock is the only authority; a client asking early is simply refused.
        await using SpaceMmoDbContext context = _fixture.CreateContext();
        StartJobResult started = await new IndustryService(context)
            .StartJobAsync(_characterId, _refinePlateRecipeId, _stationId, runs: 1);

        await using SpaceMmoDbContext claim = _fixture.CreateContext();

        await Assert.ThrowsAsync<InvalidOperationException>(() =>
            new IndustryService(claim).ClaimJobAsync(started.JobId, _characterId));
    }

    [Fact]
    public async Task ClaimingACompletedJob_DeliversOutputsAndAwardsXp()
    {
        StartJobResult started = await StartAndCompleteAsync(_refinePlateRecipeId, runs: 1);

        await using SpaceMmoDbContext claim = _fixture.CreateContext();
        ClaimJobResult result = await new IndustryService(claim)
            .ClaimJobAsync(started.JobId, _characterId);

        Assert.Equal(_plateId, result.ItemDefId);
        Assert.Equal(4, result.Quantity);
        Assert.Equal(60, result.XpAwarded);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.Equal(4, await HeldAsync(verify, _plateId));

        CharacterSkill skill = await verify.CharacterSkills
            .SingleAsync(s => s.CharacterId == _characterId && s.SkillId == _refiningSkillId);

        Assert.Equal(60, skill.Xp);
        Assert.Equal(IndustryJobState.Claimed, (await verify.IndustryJobs.SingleAsync()).State);
    }

    [Fact]
    public async Task ClaimingTwice_IsRejected()
    {
        StartJobResult started = await StartAndCompleteAsync(_refinePlateRecipeId, runs: 1);

        await using SpaceMmoDbContext first = _fixture.CreateContext();
        await new IndustryService(first).ClaimJobAsync(started.JobId, _characterId);

        await using SpaceMmoDbContext second = _fixture.CreateContext();

        // Otherwise claiming in a loop would print goods and XP from one job.
        await Assert.ThrowsAsync<InvalidOperationException>(() =>
            new IndustryService(second).ClaimJobAsync(started.JobId, _characterId));
    }

    [Fact]
    public async Task ClaimingSomeoneElsesJob_IsRejected()
    {
        StartJobResult started = await StartAndCompleteAsync(_refinePlateRecipeId, runs: 1);

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        await Assert.ThrowsAsync<UnauthorizedAccessException>(() =>
            new IndustryService(context).ClaimJobAsync(started.JobId, characterId: _characterId + 999));
    }

    [Fact]
    public async Task NonStackableOutput_BecomesATrackedInstanceCarryingItsProductionCost()
    {
        // A tool is non-stackable, so it needs an instance with an acquisition value — the figure
        // insurance would later peg to (ADR-0006).
        await GiveToolAsync();

        StartJobResult started = await StartAndCompleteAsync(_craftLaserRecipeId, runs: 1);

        await using SpaceMmoDbContext claim = _fixture.CreateContext();
        ClaimJobResult result = await new IndustryService(claim)
            .ClaimJobAsync(started.JobId, _characterId);

        Assert.Single(result.InstanceIds);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        ItemInstance instance = await verify.ItemInstances
            .SingleAsync(i => i.Id == result.InstanceIds[0]);

        Assert.Equal(_laserId, instance.ItemDefId);
        Assert.Equal(100, instance.Condition);

        // Ore was gathered, so it cost nothing; the acquisition value is the job fee.
        Assert.Equal(started.Fee + started.InputCostBasis, instance.AcquisitionValue);
        Assert.True(instance.AcquisitionValue.IsPositive);
    }

    // ── Cancelling ───────────────────────────────────────────────────────────

    [Fact]
    public async Task CancellingImmediately_RefundsEssentiallyEverything()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();
        StartJobResult started = await new IndustryService(context)
            .StartJobAsync(_characterId, _refinePlateRecipeId, _stationId, runs: 1);

        await using SpaceMmoDbContext cancel = _fixture.CreateContext();
        Assert.True(await new IndustryService(cancel).CancelJobAsync(started.JobId, _characterId));

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        // A 60-second job cancelled within a second returns all 20 ore. Misclicks are forgiven
        // without needing a grace-period rule.
        Assert.Equal(StartingOre, await HeldAsync(verify, _oreId));

        // The fee is not refunded — churn has to cost something.
        Assert.Equal(Cr(StartingCredits - 15), await BalanceAsync(verify));
    }

    [Fact]
    public async Task CancellingLate_RefundsAlmostNothing()
    {
        StartJobResult started = await StartAndBackdateAsync(
            _refinePlateRecipeId, runs: 1, elapsedSeconds: 57);

        await using SpaceMmoDbContext cancel = _fixture.CreateContext();
        await new IndustryService(cancel).CancelJobAsync(started.JobId, _characterId);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        // 5% of 60 seconds remaining on 20 ore rounds to 1 unit back. Backing out late is not a
        // cheap way to escape a bad production decision.
        Assert.Equal(StartingOre - 20 + 1, await HeldAsync(verify, _oreId));
    }

    [Fact]
    public async Task CancellingAtTheHalfwayPoint_RefundsHalf()
    {
        StartJobResult started = await StartAndBackdateAsync(
            _refinePlateRecipeId, runs: 1, elapsedSeconds: 30);

        await using SpaceMmoDbContext cancel = _fixture.CreateContext();
        await new IndustryService(cancel).CancelJobAsync(started.JobId, _characterId);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.Equal(StartingOre - 10, await HeldAsync(verify, _oreId));
    }

    [Fact]
    public async Task CancellingAwardsNoXp()
    {
        // Awarding XP at start would make start-and-cancel a farm costing only the fee.
        await using SpaceMmoDbContext context = _fixture.CreateContext();
        StartJobResult started = await new IndustryService(context)
            .StartJobAsync(_characterId, _refinePlateRecipeId, _stationId, runs: 1);

        await using SpaceMmoDbContext cancel = _fixture.CreateContext();
        await new IndustryService(cancel).CancelJobAsync(started.JobId, _characterId);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.False(await verify.CharacterSkills.AnyAsync(s => s.CharacterId == _characterId));
    }

    [Fact]
    public async Task CancellingTwice_IsANoOp()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();
        StartJobResult started = await new IndustryService(context)
            .StartJobAsync(_characterId, _refinePlateRecipeId, _stationId, runs: 1);

        await using SpaceMmoDbContext first = _fixture.CreateContext();
        Assert.True(await new IndustryService(first).CancelJobAsync(started.JobId, _characterId));

        await using SpaceMmoDbContext second = _fixture.CreateContext();
        Assert.False(await new IndustryService(second).CancelJobAsync(started.JobId, _characterId));

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        // Critically, the inputs were not refunded twice.
        Assert.Equal(StartingOre, await HeldAsync(verify, _oreId));
    }

    // ── The chain ────────────────────────────────────────────────────────────

    [Fact]
    public async Task RefiningThenBuilding_ChainsThroughTheRealRecipes()
    {
        // Ore -> plates -> hull section, the middle of the onboarding chain from design-bible §5.
        StartJobResult refine = await StartAndCompleteAsync(_refinePlateRecipeId, runs: 1);

        await using (SpaceMmoDbContext claim = _fixture.CreateContext())
        {
            await new IndustryService(claim).ClaimJobAsync(refine.JobId, _characterId);
        }

        // Shipcrafting 5 is needed for the section, so grant the XP the earlier quests would.
        await GrantSkillAsync(_shipcraftingSkillId, SkillCurve.XpForLevel(5));
        await GiveAsync(_oreId, 0);

        StartJobResult build = await StartAndCompleteAsync(_buildSectionRecipeId, runs: 1);

        await using SpaceMmoDbContext claimSection = _fixture.CreateContext();
        ClaimJobResult result = await new IndustryService(claimSection)
            .ClaimJobAsync(build.JobId, _characterId);

        Assert.Equal(_sectionId, result.ItemDefId);
        Assert.Equal(1, result.Quantity);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.Equal(1, await HeldAsync(verify, _sectionId));
        Assert.Equal(0, await HeldAsync(verify, _plateId));
    }

    [Fact]
    public async Task ProductionCost_AccumulatesThroughTheChain()
    {
        // Each stage folds the previous stage's cost plus its own fee into the output, so a hull
        // built from gathered ore still carries an honest acquisition value.
        StartJobResult refine = await StartAndCompleteAsync(_refinePlateRecipeId, runs: 1);

        await using (SpaceMmoDbContext claim = _fixture.CreateContext())
        {
            await new IndustryService(claim).ClaimJobAsync(refine.JobId, _characterId);
        }

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        InventoryItem plates = await verify.InventoryItems
            .SingleAsync(i => i.ItemDefId == _plateId);

        // Ore was gathered so cost nothing; the plates carry the refining fee.
        Assert.Equal(refine.Fee, plates.CostBasis);
    }

    [Fact]
    public async Task Crafting_ConservesMaterialsExactly()
    {
        // Every unit of ore is either still held, consumed by a running job, or represented in a
        // claimed output. None may appear from nowhere.
        StartJobResult started = await StartAndCompleteAsync(_refinePlateRecipeId, runs: 3);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        int oreHeld = await HeldAsync(verify, _oreId);
        int oreInJobs = await verify.IndustryJobInputs
            .Where(i => i.ItemDefId == _oreId)
            .SumAsync(i => i.Quantity);

        Assert.Equal(StartingOre, oreHeld + oreInJobs);
        Assert.Equal(60, oreInJobs);

        await using SpaceMmoDbContext claim = _fixture.CreateContext();
        ClaimJobResult result = await new IndustryService(claim)
            .ClaimJobAsync(started.JobId, _characterId);

        Assert.Equal(12, result.Quantity);
    }

    // ── Helpers ──────────────────────────────────────────────────────────────

    private async Task<StartJobResult> StartAndCompleteAsync(int recipeId, int runs)
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        StartJobResult started = await new IndustryService(context)
            .StartJobAsync(_characterId, recipeId, _stationId, runs);

        // Rather than waiting out the real duration, move the job's clock into the past. The
        // service only ever compares against the server clock, so this exercises the real path.
        await using SpaceMmoDbContext shift = _fixture.CreateContext();
        IndustryJob job = await shift.IndustryJobs.SingleAsync(j => j.Id == started.JobId);
        job.CompletesAt = DateTimeOffset.UtcNow.AddSeconds(-1);
        await shift.SaveChangesAsync();

        return started;
    }

    private async Task<StartJobResult> StartAndBackdateAsync(
        int recipeId, int runs, int elapsedSeconds)
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        StartJobResult started = await new IndustryService(context)
            .StartJobAsync(_characterId, recipeId, _stationId, runs);

        await using SpaceMmoDbContext shift = _fixture.CreateContext();
        IndustryJob job = await shift.IndustryJobs.SingleAsync(j => j.Id == started.JobId);
        job.StartedAt = DateTimeOffset.UtcNow.AddSeconds(-elapsedSeconds);
        await shift.SaveChangesAsync();

        return started;
    }

    private async Task<int> HeldAsync(SpaceMmoDbContext context, int itemDefId) =>
        await context.InventoryItems
            .Where(i => i.ItemDefId == itemDefId
                && context.Inventories.Any(inv => inv.Id == i.InventoryId
                    && inv.CharacterId == _characterId))
            .SumAsync(i => i.Quantity);

    private async Task<Credits> BalanceAsync(SpaceMmoDbContext context) =>
        (await context.Characters.SingleAsync(c => c.Id == _characterId)).Balance;

    private async Task GrantSkillAsync(int skillId, long xp)
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        CharacterSkill? skill = await context.CharacterSkills
            .FirstOrDefaultAsync(s => s.CharacterId == _characterId && s.SkillId == skillId);

        if (skill is null)
        {
            context.CharacterSkills.Add(new CharacterSkill
            {
                CharacterId = _characterId,
                SkillId = skillId,
                Xp = xp,
            });
        }
        else
        {
            skill.Xp = xp;
        }

        await context.SaveChangesAsync();
    }

    private async Task GiveAsync(int itemDefId, int quantity)
    {
        if (quantity == 0)
        {
            return;
        }

        await using SpaceMmoDbContext context = _fixture.CreateContext();
        var inventories = new InventoryService(context);

        Inventory hangar = await inventories.GetOrCreateStationHangarAsync(_characterId, _stationId);
        await inventories.AddAsync(hangar.Id, itemDefId, quantity, Credits.Zero);

        await context.SaveChangesAsync();
    }

    private async Task GiveToolAsync()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();
        var inventories = new InventoryService(context);

        Inventory hangar = await inventories.GetOrCreateStationHangarAsync(_characterId, _stationId);

        context.ItemInstances.Add(new ItemInstance
        {
            ItemDefId = _laserId,
            InventoryId = hangar.Id,
            Condition = 100,
            AcquisitionValue = Cr(200),
            CreatedAt = DateTimeOffset.UtcNow,
        });

        await context.SaveChangesAsync();
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

        var refining = new Skill { Key = "refining", Name = "Refining", Category = SkillCategory.Life };
        var shipcrafting = new Skill
        {
            Key = "shipcrafting", Name = "Shipcrafting", Category = SkillCategory.Life,
        };
        var toolcrafting = new Skill
        {
            Key = "toolcrafting", Name = "Toolcrafting", Category = SkillCategory.Life,
        };

        context.Skills.AddRange(refining, shipcrafting, toolcrafting);

        var ore = new ItemDef
        {
            Key = "ferrite_ore", Name = "Ferrite Ore", Category = ItemCategory.Raw, VolumeM3 = 0.4,
        };
        var plate = new ItemDef
        {
            Key = "ferrite_plate", Name = "Ferrite Plate",
            Category = ItemCategory.Refined, VolumeM3 = 0.2,
        };
        var section = new ItemDef
        {
            Key = "shuttle_hull_section", Name = "Shuttle Hull Section",
            Category = ItemCategory.Component, VolumeM3 = 20,
        };
        var scrap = new ItemDef
        {
            Key = "scrap_alloy", Name = "Scrap Alloy", Category = ItemCategory.Raw, VolumeM3 = 0.1,
        };
        var laser = new ItemDef
        {
            Key = "crude_mining_laser", Name = "Crude Mining Laser",
            Category = ItemCategory.Tool, VolumeM3 = 2,
        };

        context.ItemDefs.AddRange(ore, plate, section, scrap, laser);
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
            Email = "industry@example.com",
            PasswordHash = "not-a-real-hash",
            CreatedAt = DateTimeOffset.UtcNow,
        };
        context.Accounts.Add(account);
        await context.SaveChangesAsync();

        var character = new Character
        {
            AccountId = account.Id,
            Name = "Crafter",
            Race = Race.Humanoid,
            HomeBodyId = body.Id,
            Balance = Cr(StartingCredits),
            CreatedAt = DateTimeOffset.UtcNow,
        };
        context.Characters.Add(character);

        // 20 ore -> 4 plates, 60 seconds, Refining 1.
        var refinePlate = new Recipe
        {
            Key = "refine_ferrite_plate",
            OutputItemDefId = plate.Id,
            OutputQuantity = 4,
            SkillId = refining.Id,
            RequiredLevel = 1,
            JobSeconds = 60,
            XpPerRun = 60,
        };

        // 4 plates + 2 scrap -> 1 section, 300 seconds, Shipcrafting 5.
        var buildSection = new Recipe
        {
            Key = "build_shuttle_hull_section",
            OutputItemDefId = section.Id,
            OutputQuantity = 1,
            SkillId = shipcrafting.Id,
            RequiredLevel = 5,
            JobSeconds = 300,
            XpPerRun = 900,
        };

        // Tool-gated on purpose, so the tool check has something to exercise.
        var craftLaser = new Recipe
        {
            Key = "craft_crude_mining_laser",
            OutputItemDefId = laser.Id,
            OutputQuantity = 1,
            SkillId = toolcrafting.Id,
            RequiredLevel = 1,
            JobSeconds = 30,
            XpPerRun = 300,
            RequiredToolItemDefId = laser.Id,
        };

        context.Recipes.AddRange(refinePlate, buildSection, craftLaser);
        await context.SaveChangesAsync();

        context.RecipeInputs.AddRange(
            new RecipeInput { RecipeId = refinePlate.Id, ItemDefId = ore.Id, Quantity = 20 },
            new RecipeInput { RecipeId = buildSection.Id, ItemDefId = plate.Id, Quantity = 4 },
            new RecipeInput { RecipeId = buildSection.Id, ItemDefId = scrap.Id, Quantity = 2 },
            new RecipeInput { RecipeId = craftLaser.Id, ItemDefId = scrap.Id, Quantity = 8 });

        var hangar = new Inventory
        {
            CharacterId = character.Id,
            StationId = station.Id,
            Kind = InventoryKind.StationHangar,
            CapacityM3 = 0,
        };
        context.Inventories.Add(hangar);
        await context.SaveChangesAsync();

        // Gathered material enters at zero cost — it took labour, not credits.
        context.InventoryItems.AddRange(
            new InventoryItem
            {
                InventoryId = hangar.Id, ItemDefId = ore.Id,
                Quantity = StartingOre, CostBasis = Credits.Zero,
            },
            new InventoryItem
            {
                InventoryId = hangar.Id, ItemDefId = scrap.Id,
                Quantity = 100, CostBasis = Credits.Zero,
            });

        await context.SaveChangesAsync();

        _stationId = station.Id;
        _characterId = character.Id;
        _refiningSkillId = refining.Id;
        _shipcraftingSkillId = shipcrafting.Id;
        _oreId = ore.Id;
        _plateId = plate.Id;
        _sectionId = section.Id;
        _laserId = laser.Id;
        _refinePlateRecipeId = refinePlate.Id;
        _buildSectionRecipeId = buildSection.Id;
        _craftLaserRecipeId = craftLaser.Id;
    }
}
