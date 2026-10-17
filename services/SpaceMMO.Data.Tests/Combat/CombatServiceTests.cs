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
/// Hits, health and respawning (task 194), against a database.
/// </summary>
/// <remarks>
/// <para>
/// <strong>Geometry of its own, deliberately simple</strong>: the capital at the origin, safe to 40 km, contested to
/// 120 km, Faction A's half towards +X. The shipped geometry is checked against the planets elsewhere
/// (<c>TheShippedFactionSpaceFitsTheSystemTests</c>); what this checks is that the service asks the rule with the
/// right facts, which is where a game server's hit could be judged wrongly with every rule test still green.
/// </para>
/// <para>
/// Ayla is Humanoid and Mars Martian, both Faction A; Orc is a Space Orc, Faction B. All three have finished the
/// main story; Newbie, a Space Elf, has not, and is protected. Every attacker holds the tier-1 rifle in Weapon 1.
/// </para>
/// </remarks>
[Collection(SharedDatabase.Name)]
public sealed class CombatServiceTests(DatabaseFixture fixture) : IAsyncLifetime
{
    private static readonly DateTimeOffset T0 = new(2026, 10, 16, 20, 0, 0, TimeSpan.Zero);

    private static readonly SystemPosition InAsHalf = new(200, 0, 0);
    private static readonly SystemPosition InBsHalf = new(-200, 0, 0);
    private static readonly SystemPosition AtTheCapital = new(10, 0, 0);

    private readonly DatabaseFixture _fixture = fixture;

    private int _ayla;
    private int _mars;
    private int _orc;
    private int _newbie;
    private int _rifleDefId;
    private int _aStationId;
    private int _bStationId;

    public async Task InitializeAsync()
    {
        await _fixture.ResetAsync();
        await SeedAsync();
    }

    public Task DisposeAsync() => Task.CompletedTask;

    private static CombatService Service(SpaceMmoDbContext context) => new(context);

    private async Task<HitResult> HitAsync(
        int attacker, int target, SystemPosition where, DateTimeOffset when,
        HitZone zone = HitZone.Body, HitFacing facing = HitFacing.Front)
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        return await Service(context).HitAsync(new HitRequest(attacker, target, zone, facing, where), when);
    }

    // ── A hit that lands ─────────────────────────────────────────────────────

    [Fact]
    public async Task A_local_firing_on_an_intruder_lands_and_pays_the_rifle_its_sibling_and_constitution()
    {
        HitResult hit = await HitAsync(_orc, _ayla, InBsHalf, T0);

        Assert.True(hit.Landed, hit.Refusal);
        Assert.Equal(55, hit.Damage);
        Assert.Equal(945, hit.TargetHealth);
        Assert.Equal(1_000, hit.TargetMaxHealth);
        Assert.False(hit.Killed);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        // 55 damage at 12 a hundred is 7 XP: 80% to the rifle, a fifth to its one sibling (floored to 1, the rest
        // to the rifle), and a third of the 7 on top to constitution.
        Assert.Equal(6, await XpAsync(verify, _orc, "laser_rifle"));
        Assert.Equal(1, await XpAsync(verify, _orc, "laser_pistol"));
        Assert.Equal(2, await XpAsync(verify, _orc, "constitution"));
        Assert.Equal(6, hit.WeaponXp);

        // And the attack is on record, the way round that lets Ayla answer.
        Assert.True(await verify.AttackRecords.AnyAsync(r => r.AttackerCharacterId == _orc && r.TargetCharacterId == _ayla));
    }

    [Fact]
    public async Task Where_it_lands_and_from_which_side_scale_it()
    {
        HitResult hit = await HitAsync(_orc, _ayla, InBsHalf, T0, HitZone.Head, HitFacing.Back);

        Assert.Equal(99, hit.Damage);
    }

    // ── Who may shoot whom ───────────────────────────────────────────────────

    [Fact]
    public async Task An_intruder_may_not_fire_first_but_may_answer()
    {
        HitResult first = await HitAsync(_ayla, _orc, InBsHalf, T0);

        Assert.Equal("not_allowed", first.Refusal);

        await HitAsync(_orc, _ayla, InBsHalf, T0.AddSeconds(1));

        HitResult answer = await HitAsync(_ayla, _orc, InBsHalf, T0.AddSeconds(2));

        Assert.True(answer.Landed, answer.Refusal);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        // The refused shot did nothing: no health lost by Orc beyond the answer, no XP for it.
        Assert.Equal(945, (await verify.Characters.SingleAsync(c => c.Id == _orc)).Health);
    }

    [Fact]
    public async Task A_refused_shot_leaves_no_trace()
    {
        await HitAsync(_ayla, _orc, InBsHalf, T0);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.False(await verify.AttackRecords.AnyAsync());
        Assert.Equal(0, await XpAsync(verify, _ayla, "laser_rifle"));
        Assert.Null((await verify.Characters.SingleAsync(c => c.Id == _orc)).Health);
    }

    [Fact]
    public async Task Friends_never_and_nobody_at_the_capital()
    {
        Assert.Equal("not_allowed", (await HitAsync(_ayla, _mars, InAsHalf, T0)).Refusal);
        Assert.Equal("not_allowed", (await HitAsync(_orc, _ayla, AtTheCapital, T0)).Refusal);
    }

    [Fact]
    public async Task A_new_player_is_safe_until_they_shoot_back()
    {
        // In A's half, Ayla is the local, and Newbie is still protected.
        Assert.Equal("not_allowed", (await HitAsync(_ayla, _newbie, InAsHalf, T0)).Refusal);

        // Newbie, protected but provoking, fires on Ayla. Ayla may now answer: protection is a shield, not a
        // firing position (ADR-0009). Newbie is the intruder here, and may fire only once provoked, so the
        // first shot has to come from somewhere they may fire: their own half.
        Assert.True((await HitAsync(_newbie, _ayla, InBsHalf, T0.AddSeconds(1))).Landed);
        Assert.True((await HitAsync(_ayla, _newbie, InAsHalf, T0.AddSeconds(2))).Landed);
    }

    // ── Weapons ──────────────────────────────────────────────────────────────

    [Fact]
    public async Task Nothing_in_Weapon_1_fires_nothing()
    {
        await using (SpaceMmoDbContext context = _fixture.CreateContext())
        {
            await new EquipmentService(context).UnequipAsync(_orc, EquipmentSlot.Weapon1);
        }

        Assert.Equal("no_weapon", (await HitAsync(_orc, _ayla, InBsHalf, T0)).Refusal);
    }

    [Fact]
    public async Task A_broken_rifle_fires_nothing()
    {
        await using (SpaceMmoDbContext context = _fixture.CreateContext())
        {
            ItemInstance rifle = await context.ItemInstances.SingleAsync(
                i => i.Inventory!.CharacterId == _orc && i.EquippedSlot == EquipmentSlot.Weapon1);
            rifle.Condition = 0;
            await context.SaveChangesAsync();
        }

        Assert.Equal("no_weapon", (await HitAsync(_orc, _ayla, InBsHalf, T0)).Refusal);
    }

    // ── Killing ──────────────────────────────────────────────────────────────

    [Fact]
    public async Task A_kill_takes_only_what_was_left_is_recorded_and_ends_it()
    {
        await SetHealthAsync(_ayla, 30, T0.AddSeconds(-1));

        HitResult kill = await HitAsync(_orc, _ayla, InBsHalf, T0);

        Assert.True(kill.Killed);
        Assert.Equal(30, kill.Damage);
        Assert.Equal(0, kill.TargetHealth);

        await using (SpaceMmoDbContext verify = _fixture.CreateContext())
        {
            DeathRecord death = await verify.DeathRecords.SingleAsync();

            Assert.Equal(_ayla, death.CharacterId);
            Assert.Equal(_orc, death.KillerCharacterId);
            Assert.Equal(DeathCause.PersonalCombatPlanet, death.Cause);

            // XP for the 30 that landed, not the 55 the bolt carried: 4, all of it under the sibling's floor.
            Assert.Equal(4, await XpAsync(verify, _orc, "laser_rifle") + await XpAsync(verify, _orc, "laser_pistol"));
        }

        // Nothing more is done to the dead, however long they lie there.
        Assert.Equal("target_dead", (await HitAsync(_orc, _ayla, InBsHalf, T0.AddMinutes(5))).Refusal);

        // And they fire nothing either.
        Assert.Equal("attacker_dead", (await HitAsync(_ayla, _orc, InBsHalf, T0.AddSeconds(1))).Refusal);
    }

    // ── Health coming back ───────────────────────────────────────────────────

    [Fact]
    public async Task Health_comes_back_out_of_combat()
    {
        await SetHealthAsync(_ayla, 200, T0);

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        Assert.Equal(200, (await Service(context).StatusAsync(_ayla, T0.AddSeconds(9)))!.Health);
        Assert.Equal(400, (await Service(context).StatusAsync(_ayla, T0.AddSeconds(14)))!.Health);
        Assert.Equal(1_000, (await Service(context).StatusAsync(_ayla, T0.AddMinutes(2)))!.Health);
    }

    [Fact]
    public async Task Firing_keeps_the_health_regained_before_it()
    {
        // Orc was hurt long ago and has regained it all; firing now restarts his clock, and must not take back
        // what the old clock gave him.
        await SetHealthAsync(_orc, 500, T0.AddMinutes(-5));

        await HitAsync(_orc, _ayla, InBsHalf, T0);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Character orc = await verify.Characters.SingleAsync(c => c.Id == _orc);

        Assert.Equal(1_000, orc.Health);
        Assert.Equal(T0, orc.LastCombatAt);
    }

    [Fact]
    public async Task Constitution_raises_the_maximum()
    {
        await using (SpaceMmoDbContext context = _fixture.CreateContext())
        {
            int constitution = await context.Skills.Where(s => s.Key == "constitution").Select(s => s.Id).SingleAsync();

            context.CharacterSkills.Add(new CharacterSkill
            {
                CharacterId = _ayla, SkillId = constitution, Xp = SkillCurve.XpForLevel(50),
            });

            await context.SaveChangesAsync();
        }

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        CombatStatus status = (await Service(verify).StatusAsync(_ayla, T0))!;

        Assert.Equal(1_500, status.MaxHealth);
        Assert.Equal(1_500, status.Health);
    }

    // ── Coming back ──────────────────────────────────────────────────────────

    [Fact]
    public async Task The_living_do_not_respawn()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        RespawnException refused = await Assert.ThrowsAsync<RespawnException>(() => Service(context).RespawnAsync(_ayla, T0));

        Assert.Equal("not_dead", refused.Reason);
    }

    [Fact]
    public async Task The_dead_come_back_where_they_last_docked_with_full_health()
    {
        await using (SpaceMmoDbContext dock = _fixture.CreateContext())
        {
            var docking = new DockingService(dock, new InventoryService(dock));
            await docking.DockAsync(_ayla, _bStationId);
            await docking.UndockAsync(_ayla);
        }

        await SetHealthAsync(_ayla, 0, T0);

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        RespawnResult back = await Service(context).RespawnAsync(_ayla, T0.AddSeconds(5));

        Assert.Equal(_bStationId, back.StationId);
        Assert.Equal(1_000, back.Health);
        Assert.Equal(1_000, (await Service(context).StatusAsync(_ayla, T0.AddSeconds(5)))!.Health);
    }

    [Fact]
    public async Task Somebody_who_never_docked_comes_back_on_their_home_world()
    {
        await SetHealthAsync(_ayla, 0, T0);

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        Assert.Equal(_aStationId, (await Service(context).RespawnAsync(_ayla, T0)).StationId);
    }

    // ── Helpers ──────────────────────────────────────────────────────────────

    private async Task SetHealthAsync(int characterId, int health, DateTimeOffset lastCombat)
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        Character character = await context.Characters.SingleAsync(c => c.Id == characterId);
        character.Health = health;
        character.LastCombatAt = lastCombat;

        await context.SaveChangesAsync();
    }

    private static Task<long> XpAsync(SpaceMmoDbContext context, int characterId, string skillKey) =>
        context.CharacterSkills
            .Where(s => s.CharacterId == characterId && s.Skill!.Key == skillKey)
            .Select(s => s.Xp)
            .SingleOrDefaultAsync();

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

        Station aStation = Outpost("station_a", homeA);
        Station bStation = Outpost("station_b", homeB);

        context.Stations.AddRange(aStation, bStation);

        var rifleSkill = new Skill
        {
            Key = "laser_rifle", Name = "Laser Rifle", Category = SkillCategory.Combat,
            WeaponFamily = WeaponFamily.Laser, WeaponType = WeaponType.Ranged, WeaponMount = WeaponMount.OnFoot,
            XpPerHundredDamage = 12,
        };

        var pistolSkill = new Skill
        {
            Key = "laser_pistol", Name = "Laser Pistol", Category = SkillCategory.Combat,
            WeaponFamily = WeaponFamily.Laser, WeaponType = WeaponType.Ranged, WeaponMount = WeaponMount.OnFoot,
            XpPerHundredDamage = 12,
        };

        context.Skills.AddRange(rifleSkill, pistolSkill, new Skill
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

        var story = new QuestDef { Key = "intro", Name = "Intro", Kind = QuestKind.MainStory };

        context.ItemDefs.Add(rifle);
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
        Character mars = Person("Mars", Race.Martian, homeA);
        Character orc = Person("Orc", Race.SpaceOrc, homeB);
        Character newbie = Person("Newbie", Race.SpaceElf, homeB);

        context.Characters.AddRange(ayla, mars, orc, newbie);
        await context.SaveChangesAsync();

        foreach (Character done in new[] { ayla, mars, orc })
        {
            context.CharacterQuests.Add(new CharacterQuest
            {
                CharacterId = done.Id, QuestDefId = story.Id, State = QuestState.Completed, StepOrdinal = 1, StartedAt = T0,
                CompletedAt = T0,
            });
        }

        await context.SaveChangesAsync();

        _ayla = ayla.Id;
        _mars = mars.Id;
        _orc = orc.Id;
        _newbie = newbie.Id;
        _rifleDefId = rifle.Id;
        _aStationId = aStation.Id;
        _bStationId = bStation.Id;

        foreach (int fighter in new[] { _ayla, _mars, _orc, _newbie })
        {
            await GiveRifleAsync(fighter);
        }
    }

    private async Task GiveRifleAsync(int characterId)
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        Inventory pockets = await new InventoryService(context).GetOrCreateCarriedAsync(characterId);

        var instance = new ItemInstance
        {
            ItemDefId = _rifleDefId, InventoryId = pockets.Id, Condition = 100,
            AcquisitionValue = Credits.FromWholeCredits(100), CreatedAt = T0,
        };

        context.ItemInstances.Add(instance);
        await context.SaveChangesAsync();

        await new EquipmentService(context).EquipAsync(characterId, instance.Id, EquipmentSlot.Weapon1);
    }
}
