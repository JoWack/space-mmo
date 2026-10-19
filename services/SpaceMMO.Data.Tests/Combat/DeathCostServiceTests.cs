using Microsoft.EntityFrameworkCore;
using SpaceMMO.Data.Combat;
using SpaceMMO.Data.Docking;
using SpaceMMO.Data.Entities;
using SpaceMMO.Data.Inventories;
using SpaceMMO.Domain.Characters;
using SpaceMMO.Domain.Combat;
using SpaceMMO.Domain.Economy;
using SpaceMMO.Domain.Items;
using SpaceMMO.Domain.Loss;
using SpaceMMO.Domain.Progression;
using SpaceMMO.Domain.Quests;
using SpaceMMO.Domain.Universe;
using Xunit;

namespace SpaceMMO.Data.Tests.Combat;

/// <summary>
/// What dying costs, and taking what was dropped (ADR-0016, task 103), against a database.
/// </summary>
/// <remarks>
/// <para>
/// <strong>Every death here is a real kill</strong>, through <see cref="CombatService.HitAsync"/>: a cost service
/// tested against hand-made death records would pass while the kill recorded something it does not read.
/// </para>
/// <para>
/// Ayla (Humanoid, Faction A) is killed by Orc (Space Orc, Faction B) on Orc's own side, where the local may fire
/// first. She carries 6 Ferrite Ore that cost her 30 credits, 5 Martian Structural Plate and a spare mining laser
/// (5.4 of her 6 m³), and wears a rifle at 100 and a mining laser at 70.
/// </para>
/// </remarks>
[Collection(SharedDatabase.Name)]
public sealed class DeathCostServiceTests(DatabaseFixture fixture) : IAsyncLifetime
{
    private static readonly DateTimeOffset T0 = new(2026, 10, 19, 20, 0, 0, TimeSpan.Zero);

    private static readonly SystemPosition InBsHalf = new(-200, 0, 0);

    /// <summary>Where the game server says the body fell: beside it, in system kilometres.</summary>
    private static readonly SystemPosition WhereSheFell = new(-180.25, 3.5, -1.125);

    private readonly DatabaseFixture _fixture = fixture;

    private int _ayla;
    private int _orc;
    private int _oreId;
    private int _plateId;
    private int _bStationId;
    private long _rifle;
    private long _wornLaser;
    private long _spareLaser;

    public async Task InitializeAsync()
    {
        await _fixture.ResetAsync();
        await SeedAsync();
    }

    public Task DisposeAsync() => Task.CompletedTask;

    // ── Dying ────────────────────────────────────────────────────────────────

    [Fact]
    public async Task A_kill_owes_its_costs_and_takes_none_of_them_under_the_hits_lock()
    {
        await KillAylaAsync();

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        DeathRecord death = await verify.DeathRecords.SingleAsync();

        Assert.Equal(DeathCause.PersonalCombatPlanet, death.Cause);
        Assert.Null(death.CostsAppliedAt);

        // Still carried: the kill takes nothing, so a killing hit holds its lock no longer than any other.
        Assert.Equal(6, await QuantityAsync(verify, _ayla, _oreId));
        Assert.Equal(100, (await verify.ItemInstances.SingleAsync(i => i.Id == _rifle)).Condition);
    }

    [Fact]
    public async Task Dying_drops_half_of_every_material_destroys_the_rest_and_wears_what_is_worn()
    {
        await KillAylaAsync();

        DeathCostResult result = await ApplyAsync(_ayla, WhereSheFell, T0.AddSeconds(1));

        Assert.True(result.Applied);
        Assert.False(result.AtStation);

        // Half drops and half is destroyed; plate's odd unit drops (Joe, 19 October). Plate shows its stack's world,
        // ore its own.
        Assert.Equal(
            [
                new DeathLossLine(_oreId, "ferrite_ore", "Ferrite Ore", ItemStyle.Terran, 3, 3),
                new DeathLossLine(_plateId, "ferrite_plate", "Structural Plate", ItemStyle.Martian, 3, 2),
            ],
            result.Lost.OrderBy(l => l.ItemDefId));

        Assert.Equal(
            [
                new WornLine(_rifle, "laser_rifle", "Laser Rifle", 100, 90),
                new WornLine(_wornLaser, "crude_mining_laser", "Crude Mining Laser", 70, 60),
            ],
            result.Worn);

        LootContainerView container = Assert.IsType<LootContainerView>(result.Container);

        Assert.Equal(WhereSheFell, container.Position);
        Assert.Equal("Ayla", container.DroppedBy);
        Assert.Equal(T0.AddSeconds(1) + TimeSpan.FromMinutes(10), container.ExpiresAt);
        Assert.Equal(
            [("Ferrite Ore", 3), ("Structural Plate", 3)],
            container.Items.Select(i => (i.Name, i.Quantity)));

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        // Every material has left her pockets; the spare laser has not, and has not worn: only what is worn wears.
        Assert.Equal(0, await QuantityAsync(verify, _ayla, _oreId));
        Assert.Equal(0, await QuantityAsync(verify, _ayla, _plateId));

        ItemInstance spare = await verify.ItemInstances.Include(i => i.Inventory).SingleAsync(i => i.Id == _spareLaser);

        Assert.Equal(InventoryKind.CharacterCarried, spare.Inventory!.Kind);
        Assert.Equal(80, spare.Condition);

        Assert.Equal(90, (await verify.ItemInstances.SingleAsync(i => i.Id == _rifle)).Condition);
        Assert.Equal(60, (await verify.ItemInstances.SingleAsync(i => i.Id == _wornLaser)).Condition);

        // The dropped half carries its share of what the ore cost; the destroyed half's is gone with it.
        LootContainerItem ore = await verify.LootContainerItems.SingleAsync(i => i.ItemDefId == _oreId);

        Assert.Equal(Credits.FromWholeCredits(15), ore.CostBasis);

        // The sink, recorded.
        Assert.Equal(
            [(_oreId, 3, 3), (_plateId, 3, 2)],
            (await verify.DeathLosses.OrderBy(l => l.ItemDefId).ToListAsync()).Select(l => (l.ItemDefId, l.Dropped, l.Destroyed)));

        Assert.NotNull((await verify.DeathRecords.SingleAsync()).CostsAppliedAt);
    }

    [Fact]
    public async Task A_death_costs_once()
    {
        await KillAylaAsync();
        await ApplyAsync(_ayla, WhereSheFell, T0.AddSeconds(1));

        DeathCostResult again = await ApplyAsync(_ayla, WhereSheFell, T0.AddSeconds(2));

        Assert.False(again.Applied);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.Equal(1, await verify.LootContainers.CountAsync());
        Assert.Equal(90, (await verify.ItemInstances.SingleAsync(i => i.Id == _rifle)).Condition);
    }

    [Fact]
    public async Task Dying_docked_at_a_station_costs_nothing()
    {
        await using (SpaceMmoDbContext dock = _fixture.CreateContext())
        {
            await new DockingService(dock, new InventoryService(dock)).DockAsync(_ayla, _bStationId);
        }

        await KillAylaAsync();

        DeathCostResult result = await ApplyAsync(_ayla, WhereSheFell, T0.AddSeconds(1));

        Assert.True(result.Applied);
        Assert.True(result.AtStation);
        Assert.Empty(result.Lost);
        Assert.Empty(result.Worn);
        Assert.Null(result.Container);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.Equal(DeathCause.PersonalCombatStation, (await verify.DeathRecords.SingleAsync()).Cause);
        Assert.Equal(6, await QuantityAsync(verify, _ayla, _oreId));
        Assert.Equal(100, (await verify.ItemInstances.SingleAsync(i => i.Id == _rifle)).Condition);
        Assert.Equal(0, await verify.LootContainers.CountAsync());
    }

    [Fact]
    public async Task Carrying_no_materials_drops_no_container_and_still_wears_the_gear()
    {
        await using (SpaceMmoDbContext context = _fixture.CreateContext())
        {
            await context.InventoryItems.Where(i => i.Inventory!.CharacterId == _ayla).ExecuteDeleteAsync();
        }

        await KillAylaAsync();

        DeathCostResult result = await ApplyAsync(_ayla, WhereSheFell, T0.AddSeconds(1));

        Assert.Empty(result.Lost);
        Assert.Null(result.Container);
        Assert.Equal([90, 60], result.Worn.Select(w => w.After));
    }

    [Fact]
    public async Task A_death_the_game_server_never_costed_is_costed_where_they_were_last_seen()
    {
        await using (SpaceMmoDbContext context = _fixture.CreateContext())
        {
            Character ayla = await context.Characters.SingleAsync(c => c.Id == _ayla);
            ayla.LastSystemX = -190.5;
            ayla.LastSystemY = 1.0;
            ayla.LastSystemZ = 2.0;

            await context.SaveChangesAsync();
        }

        await KillAylaAsync();

        // As the respawn endpoint asks, with no position.
        DeathCostResult result = await ApplyAsync(_ayla, null, T0.AddMinutes(3));

        Assert.True(result.Applied);
        Assert.Equal(new SystemPosition(-190.5, 1.0, 2.0), result.Container!.Position);
    }

    // ── Looting ──────────────────────────────────────────────────────────────

    [Fact]
    public async Task Anyone_may_take_what_fits_and_the_rest_stays()
    {
        long container = await AylaDiesAsync();

        // Orc's pockets hold 13 ore, 5.2 of their 6 m³: room for two more, and then none for plate.
        await GiveAsync(_orc, _oreId, 13, Credits.FromWholeCredits(7));

        LootResult result = await LootAsync(container, _orc, null, T0.AddMinutes(1));

        Assert.Null(result.Refusal);
        Assert.Equal([("Ferrite Ore", 2)], result.Taken.Select(t => (t.Name, t.Quantity)));
        Assert.Equal([("Ferrite Ore", 1), ("Structural Plate", 3)], result.Left!.Items.Select(i => (i.Name, i.Quantity)));

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        // Somebody else's goods cost a looter nothing.
        InventoryItem orcs = await CarriedStackAsync(verify, _orc, _oreId);

        Assert.Equal(15, orcs.Quantity);
        Assert.Equal(Credits.FromWholeCredits(7), orcs.CostBasis);
    }

    [Fact]
    public async Task Taking_one_stack_leaves_the_others()
    {
        long container = await AylaDiesAsync();

        LootResult result = await LootAsync(
            container, _orc, [new LootWanted(_plateId, ItemStyle.Martian)], T0.AddMinutes(1));

        Assert.Equal([("Structural Plate", 3)], result.Taken.Select(t => (t.Name, t.Quantity)));
        Assert.Equal([("Ferrite Ore", 3)], result.Left!.Items.Select(i => (i.Name, i.Quantity)));

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        // In its own style, as it fell.
        Assert.Equal(ItemStyle.Martian, (await CarriedStackAsync(verify, _orc, _plateId)).Style);
    }

    [Fact]
    public async Task The_owner_takes_back_what_it_cost_them()
    {
        long container = await AylaDiesAsync();

        await RespawnAylaAsync();

        await LootAsync(container, _ayla, null, T0.AddMinutes(4));

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        InventoryItem hers = await CarriedStackAsync(verify, _ayla, _oreId);

        Assert.Equal(3, hers.Quantity);
        Assert.Equal(Credits.FromWholeCredits(15), hers.CostBasis);
    }

    [Fact]
    public async Task An_emptied_container_is_gone()
    {
        long container = await AylaDiesAsync();

        LootResult all = await LootAsync(container, _orc, null, T0.AddMinutes(1));

        Assert.Null(all.Left);
        Assert.Equal("gone", (await LootAsync(container, _orc, null, T0.AddMinutes(2))).Refusal);

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        Assert.Empty(await new DeathCostService(context).LiveContainersAsync(new Clock(T0.AddMinutes(2))));
    }

    [Fact]
    public async Task After_ten_minutes_it_is_gone_and_what_lay_in_it_is_destroyed()
    {
        long container = await AylaDiesAsync();

        await using (SpaceMmoDbContext context = _fixture.CreateContext())
        {
            var deaths = new DeathCostService(context);

            Assert.Single(await deaths.LiveContainersAsync(new Clock(T0.AddMinutes(10).AddSeconds(-1))));
            Assert.Empty(await deaths.LiveContainersAsync(new Clock(T0.AddMinutes(10))));
        }

        Assert.Equal("gone", (await LootAsync(container, _orc, null, T0.AddMinutes(10))).Refusal);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        // Never handed out: what it held is the record of what expiry destroyed.
        Assert.Equal(6, await verify.LootContainerItems.SumAsync(i => i.Quantity));
        Assert.Equal(0, await QuantityAsync(verify, _orc, _oreId));
    }

    [Fact]
    public async Task The_dead_take_nothing()
    {
        long container = await AylaDiesAsync();

        Assert.Equal("dead", (await LootAsync(container, _ayla, null, T0.AddMinutes(1))).Refusal);
    }

    [Fact]
    public async Task Full_pockets_take_nothing_and_change_nothing()
    {
        long container = await AylaDiesAsync();

        await GiveAsync(_orc, _oreId, 15, Credits.Zero);

        LootResult result = await LootAsync(container, _orc, null, T0.AddMinutes(1));

        Assert.Equal("no_room", result.Refusal);
        Assert.Equal(6, result.Left!.Items.Sum(i => i.Quantity));
    }

    [Fact]
    public async Task Two_looters_at_once_never_take_the_same_ore_twice()
    {
        long container = await AylaDiesAsync();

        await RespawnAylaAsync();

        LootResult[] both = await Task.WhenAll(
            LootAsync(container, _orc, null, T0.AddMinutes(4)),
            LootAsync(container, _ayla, null, T0.AddMinutes(4)));

        // Whoever went first took it all; the other found it gone.
        Assert.Equal(6, both.Sum(r => r.Taken.Sum(t => t.Quantity)));
        Assert.Contains(both, r => r.Refusal == "gone");

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.Equal(
            3,
            await QuantityAsync(verify, _ayla, _oreId) + await QuantityAsync(verify, _orc, _oreId));
    }

    // ── Helpers ──────────────────────────────────────────────────────────────

    private async Task KillAylaAsync()
    {
        await using (SpaceMmoDbContext context = _fixture.CreateContext())
        {
            Character ayla = await context.Characters.SingleAsync(c => c.Id == _ayla);
            ayla.Health = 30;
            ayla.LastCombatAt = T0.AddSeconds(-1);

            await context.SaveChangesAsync();
        }

        await using SpaceMmoDbContext hit = _fixture.CreateContext();

        HitResult kill = await new CombatService(hit).HitAsync(
            new HitRequest(_orc, _ayla, HitZone.Body, HitFacing.Front, InBsHalf), new Clock(T0));

        Assert.True(kill.Killed);
    }

    private async Task<long> AylaDiesAsync()
    {
        await KillAylaAsync();

        return (await ApplyAsync(_ayla, WhereSheFell, T0)).Container!.Id;
    }

    private async Task RespawnAylaAsync()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        await new CombatService(context).RespawnAsync(_ayla, T0.AddMinutes(3));
    }

    private async Task<DeathCostResult> ApplyAsync(int characterId, SystemPosition? where, DateTimeOffset when)
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        return await new DeathCostService(context).ApplyAsync(characterId, where, new Clock(when));
    }

    private async Task<LootResult> LootAsync(
        long container, int looter, IReadOnlyList<LootWanted>? wanted, DateTimeOffset when)
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        return await new DeathCostService(context).LootAsync(container, looter, wanted, new Clock(when));
    }

    private async Task GiveAsync(int characterId, int itemDefId, int quantity, Credits cost)
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        var inventories = new InventoryService(context);
        Inventory pockets = await inventories.GetOrCreateCarriedAsync(characterId);

        await inventories.AddAsync(pockets.Id, itemDefId, quantity, cost);
        await context.SaveChangesAsync();
    }

    private static Task<int> QuantityAsync(SpaceMmoDbContext context, int characterId, int itemDefId) =>
        context.InventoryItems
            .Where(i => i.Inventory!.CharacterId == characterId
                && i.Inventory.Kind == InventoryKind.CharacterCarried
                && i.ItemDefId == itemDefId)
            .SumAsync(i => i.Quantity);

    private static Task<InventoryItem> CarriedStackAsync(SpaceMmoDbContext context, int characterId, int itemDefId) =>
        context.InventoryItems.SingleAsync(i => i.Inventory!.CharacterId == characterId
            && i.Inventory.Kind == InventoryKind.CharacterCarried
            && i.ItemDefId == itemDefId);

    /// <summary>A clock the test sets.</summary>
    private sealed class Clock(DateTimeOffset now) : TimeProvider
    {
        public override DateTimeOffset GetUtcNow() => now;
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

        Body Planet(string key, double x) => new()
        {
            Key = key, Name = key, StarSystemId = system.Id, Kind = BodyKind.Planet,
            SecurityLevel = SecurityLevel.Secure, RadiusKm = 20, SystemX = x, SystemY = 0, SystemZ = 0,
        };

        Body capital = Planet("body_capital", 0);
        Body homeA = Planet("body_terra", 200);
        Body homeB = Planet("body_grimhold", -200);

        context.Bodies.AddRange(capital, homeA, homeB);
        await context.SaveChangesAsync();

        system.FactionCapitalBodyId = capital.Id;
        system.FactionSafeRadiusKm = 40;
        system.FactionContestedRadiusKm = 120;
        system.FactionADirectionX = 1;
        system.FactionADirectionY = 0;
        system.FactionADirectionZ = 0;
        system.FactionRetaliationSeconds = 300;

        Station Outpost(string key, Body body) => new()
        {
            Key = key, Name = key, StarSystemId = system.Id, BodyId = body.Id, Kind = StationKind.TradingHub,
            DirectionX = 1.0, DockingRangeKilometres = 5.0,
        };

        // Ayla's, to come back at; Orc's, to be docked at.
        Station bStation = Outpost("station_b", homeB);

        context.Stations.AddRange(Outpost("station_a", homeA), bStation);

        var rifleSkill = new Skill
        {
            Key = "laser_rifle", Name = "Laser Rifle", Category = SkillCategory.Combat,
            WeaponFamily = WeaponFamily.Laser, WeaponType = WeaponType.Ranged, WeaponMount = WeaponMount.OnFoot,
            XpPerHundredDamage = 12,
        };

        context.Skills.AddRange(rifleSkill, new Skill
        {
            Key = "constitution", Name = "Constitution", Category = SkillCategory.Combat,
        });

        await context.SaveChangesAsync();

        var rifle = new ItemDef
        {
            Key = "laser_rifle", Name = "Laser Rifle", Category = ItemCategory.Weapon, VolumeM3 = 2,
            EquipsAs = EquipKind.Weapon, WeaponSkillId = rifleSkill.Id, WeaponDamage = 55, WeaponCooldownMs = 500,
            WeaponRangeMetres = 150,
        };

        var laser = new ItemDef
        {
            Key = "crude_mining_laser", Name = "Crude Mining Laser", Category = ItemCategory.Tool, VolumeM3 = 2,
            EquipsAs = EquipKind.MiningTool,
        };

        var ore = new ItemDef
        {
            Key = "ferrite_ore", Name = "Ferrite Ore", Category = ItemCategory.Raw, VolumeM3 = 0.4,
            Style = ItemStyle.Terran,
        };

        var plate = new ItemDef
        {
            Key = "ferrite_plate", Name = "Structural Plate", Category = ItemCategory.Refined, VolumeM3 = 0.2,
        };

        var story = new QuestDef { Key = "intro", Name = "Intro", Kind = QuestKind.MainStory };

        context.ItemDefs.AddRange(rifle, laser, ore, plate);
        context.QuestDefs.Add(story);

        var account = new Account { Email = "fighter@local.test", PasswordHash = "x", CreatedAt = T0 };

        context.Accounts.Add(account);
        await context.SaveChangesAsync();

        Character Person(string name, Race race, Body home) => new()
        {
            AccountId = account.Id, Name = name, Race = race, HomeBodyId = home.Id,
            Balance = Credits.FromWholeCredits(100), CreatedAt = T0,
        };

        Character ayla = Person("Ayla", Race.Humanoid, homeA);
        Character orc = Person("Orc", Race.SpaceOrc, homeB);

        context.Characters.AddRange(ayla, orc);
        await context.SaveChangesAsync();

        foreach (Character done in new[] { ayla, orc })
        {
            context.CharacterQuests.Add(new CharacterQuest
            {
                CharacterId = done.Id, QuestDefId = story.Id, State = QuestState.Completed, StepOrdinal = 1, StartedAt = T0,
                CompletedAt = T0,
            });
        }

        await context.SaveChangesAsync();

        _ayla = ayla.Id;
        _orc = orc.Id;
        _oreId = ore.Id;
        _plateId = plate.Id;
        _bStationId = bStation.Id;

        _rifle = await WearAsync(_orc, rifle.Id, 100, EquipmentSlot.Weapon1);
        _rifle = await WearAsync(_ayla, rifle.Id, 100, EquipmentSlot.Weapon1);
        _wornLaser = await WearAsync(_ayla, laser.Id, 70, EquipmentSlot.MiningTool);
        _spareLaser = await CarryAsync(_ayla, laser.Id, 80);

        await GiveAsync(_ayla, _oreId, 6, Credits.FromWholeCredits(30));

        await using SpaceMmoDbContext styled = _fixture.CreateContext();

        var inventories = new InventoryService(styled);
        Inventory pockets = await inventories.GetOrCreateCarriedAsync(_ayla);

        await inventories.AddAsync(pockets.Id, _plateId, 5, Credits.Zero, ItemStyle.Martian);
        await styled.SaveChangesAsync();
    }

    private async Task<long> CarryAsync(int characterId, int itemDefId, int condition)
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        Inventory pockets = await new InventoryService(context).GetOrCreateCarriedAsync(characterId);

        var instance = new ItemInstance
        {
            ItemDefId = itemDefId, InventoryId = pockets.Id, Condition = condition,
            AcquisitionValue = Credits.FromWholeCredits(100), CreatedAt = T0,
        };

        context.ItemInstances.Add(instance);
        await context.SaveChangesAsync();

        return instance.Id;
    }

    private async Task<long> WearAsync(int characterId, int itemDefId, int condition, EquipmentSlot slot)
    {
        long id = await CarryAsync(characterId, itemDefId, condition);

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        await new EquipmentService(context).EquipAsync(characterId, id, slot);

        return id;
    }
}
