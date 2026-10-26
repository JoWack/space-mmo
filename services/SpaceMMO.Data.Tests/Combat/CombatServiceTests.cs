using System.Data.Common;
using Microsoft.EntityFrameworkCore;
using Microsoft.EntityFrameworkCore.Diagnostics;
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

        return await Service(context).HitAsync(new HitRequest(attacker, target, zone, facing, where), new Clock(when));
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

    // ── Stamina (task 101) ───────────────────────────────────────────────────

    private async Task<StaminaSpentResult?> SpendAsync(int characterId, int spent)
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        return await Service(context).SpendStaminaAsync(characterId, spent);
    }

    private async Task<long> StaminaXpAsync(int characterId)
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        return await context.CharacterSkills
            .Where(s => s.CharacterId == characterId && s.Skill!.Key == "stamina")
            .Select(s => s.Xp)
            .SingleOrDefaultAsync();
    }

    [Fact]
    public async Task A_new_character_has_level_1s_pool()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        Assert.Equal(100, (await Service(context).StatusAsync(_ayla, T0))!.MaxStamina);
    }

    [Fact]
    public async Task Stamina_spent_earns_its_rate_and_a_level_grows_the_pool()
    {
        // Just short of level 50, then the 100 points that reach it: the pool is 125 there (ADR-0017).
        await using (SpaceMmoDbContext context = _fixture.CreateContext())
        {
            int stamina = await context.Skills.Where(s => s.Key == "stamina").Select(s => s.Id).SingleAsync();

            context.CharacterSkills.Add(new CharacterSkill
            {
                CharacterId = _ayla, SkillId = stamina, Xp = SkillCurve.XpForLevel(50) - 100,
            });

            await context.SaveChangesAsync();
        }

        StaminaSpentResult spent = (await SpendAsync(_ayla, 100))!;

        Assert.Equal(100, spent.XpAwarded);
        Assert.Equal(50, spent.Level);
        Assert.Equal(125, spent.MaxStamina);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.Equal(125, (await Service(verify).StatusAsync(_ayla, T0))!.MaxStamina);
    }

    [Fact]
    public async Task The_first_spend_makes_the_skill_and_the_next_adds_to_it()
    {
        Assert.Equal(250, (await SpendAsync(_mars, 250))!.XpAwarded);
        Assert.Equal(40, (await SpendAsync(_mars, 40))!.XpAwarded);

        Assert.Equal(290, await StaminaXpAsync(_mars));
    }

    [Fact]
    public async Task Reports_that_arrive_together_all_count()
    {
        // Each award writes the skill's new total: without the character's lock, two at once both read the old one
        // and the second write loses the first's.
        await SpendAsync(_orc, 1);

        await Task.WhenAll(Enumerable.Range(0, 10).Select(_ => SpendAsync(_orc, 10)));

        Assert.Equal(101, await StaminaXpAsync(_orc));
    }

    [Fact]
    public async Task Nothing_spent_earns_nothing_and_still_says_the_pool()
    {
        StaminaSpentResult spent = (await SpendAsync(_newbie, 0))!;

        Assert.Equal(0, spent.XpAwarded);
        Assert.Equal(1, spent.Level);
        Assert.Equal(100, spent.MaxStamina);
    }

    [Fact]
    public async Task Nobody_spends_for_a_character_that_does_not_exist() =>
        Assert.Null(await SpendAsync(999_999, 50));

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

    // ── Where a hit's time went (task 198) ───────────────────────────────────

    [Fact]
    public async Task A_hit_counts_every_round_trip_it_sent_and_which_took_the_lock()
    {
        var sent = new StatementsSent();

        await using SpaceMmoDbContext context = CountingContext(sent);

        HitResult hit = await Service(context).HitAsync(
            new HitRequest(_orc, _ayla, HitZone.Body, HitFacing.Front, InBsHalf), new Clock(T0));

        Assert.True(hit.Landed, hit.Refusal);

        HitTiming timing = hit.Timing!;

        // Counted through a different hook from the tally's, so neither a missed kind of statement nor one counted
        // twice gets past.
        Assert.Equal(sent.Count, timing.RoundTrips.Count);
        Assert.Equal(1, timing.LockRoundTrips);
        Assert.All(timing.RoundTrips, trip => Assert.True(trip > TimeSpan.Zero));
        Assert.True(timing.Between >= TimeSpan.Zero, $"The parts add up to more than the whole: {timing}");
    }

    [Fact]
    public async Task A_refusal_that_needs_no_database_makes_no_round_trips()
    {
        HitResult self = await HitAsync(_orc, _orc, InBsHalf, T0);

        Assert.Equal("self", self.Refusal);
        Assert.Empty(self.Timing!.RoundTrips);
    }

    [Fact]
    public async Task Waiting_for_another_hit_on_the_pair_is_counted_as_taking_the_lock()
    {
        TimeSpan held = TimeSpan.FromMilliseconds(500);

        HitResult hit;

        // Another transaction holds the target, as a hit on them from somebody else would.
        await using (SpaceMmoDbContext other = _fixture.CreateContext())
        {
            await using var holding = await other.Database.BeginTransactionAsync();

            await other.Characters
                .FromSqlInterpolated($"SELECT * FROM characters WHERE id = {_ayla} FOR UPDATE")
                .ToListAsync();

            Task<HitResult> waiting = HitAsync(_orc, _ayla, InBsHalf, T0);

            // Released only once the hit is queued behind it, however long its connection took to open (2 s from
            // localhost, task 198), and then held a known while longer.
            await BlockedOnALockAsync();
            await Task.Delay(held);
            await holding.CommitAsync();

            hit = await waiting;
        }

        HitTiming timing = hit.Timing!;

        Assert.True(hit.Landed, hit.Refusal);
        Assert.True(timing.Locking >= held, $"The wait went somewhere else: {timing}");
        Assert.True(timing.Reading + timing.Holding < timing.Locking, $"The wait went somewhere else: {timing}");
    }

    // ── Holding the lock briefly (task 198) ──────────────────────────────────

    [Fact]
    public async Task A_hit_holds_the_lock_for_one_read_its_save_and_its_commit()
    {
        // The first hit, before Orc has a row for any skill it pays; another once he has; and a kill.
        HitTiming first = await CountedHitAsync(T0);
        HitTiming again = await CountedHitAsync(T0.AddSeconds(1));

        await SetHealthAsync(_ayla, 30, T0.AddSeconds(1));

        HitTiming kill = await CountedHitAsync(T0.AddSeconds(2));

        // Three, where it was fifteen: every round trip holding the lock is one every hit queued behind it waits out.
        // The read cannot ride in the lock's own statement (A_hit_queued_behind_another_keeps_what_that_hit_wrote).
        Assert.All(
            new[] { first, again, kill },
            timing => Assert.Equal(3, timing.HoldingRoundTrips));
    }

    [Fact]
    public async Task A_hit_queued_behind_another_keeps_what_that_hit_wrote()
    {
        HitResult second;

        await using (SpaceMmoDbContext other = _fixture.CreateContext())
        {
            // Orc's first shot on Ayla, held open, writing what a first hit writes -- the attack, and Orc's first rifle,
            // pistol and constitution XP -- while his second waits behind it.
            await using var holding = await other.Database.BeginTransactionAsync();

            await other.Characters
                .FromSqlInterpolated(
                    $"SELECT * FROM characters WHERE id IN ({_ayla}, {_orc}) ORDER BY id FOR UPDATE")
                .ToListAsync();

            Dictionary<string, int> skills = await other.Skills.ToDictionaryAsync(s => s.Key, s => s.Id);

            other.AttackRecords.Add(new AttackRecord { AttackerCharacterId = _orc, TargetCharacterId = _ayla, AttackedAt = T0 });
            other.CharacterSkills.AddRange(
                new CharacterSkill { CharacterId = _orc, SkillId = skills["laser_rifle"], Xp = 6 },
                new CharacterSkill { CharacterId = _orc, SkillId = skills["laser_pistol"], Xp = 1 },
                new CharacterSkill { CharacterId = _orc, SkillId = skills["constitution"], Xp = 2 });

            await other.SaveChangesAsync();

            Task<HitResult> waiting = HitAsync(_orc, _ayla, InBsHalf, T0.AddSeconds(1));

            await BlockedOnALockAsync();
            await holding.CommitAsync();

            second = await waiting;
        }

        Assert.True(second.Landed, second.Refusal);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        // Both shots' XP, where a read from before the first committed would overwrite it or insert its rows again.
        Assert.Equal(12, await XpAsync(verify, _orc, "laser_rifle"));
        Assert.Equal(2, await XpAsync(verify, _orc, "laser_pistol"));
        Assert.Equal(4, await XpAsync(verify, _orc, "constitution"));
        Assert.Equal(T0.AddSeconds(1), (await verify.AttackRecords.SingleAsync()).AttackedAt);
    }

    [Fact]
    public async Task A_hit_that_queued_is_judged_when_it_takes_the_lock()
    {
        // Orc, the local, fires first; Ayla, the intruder, may answer for the retaliation window after his last shot.
        Assert.True((await HitAsync(_orc, _ayla, InBsHalf, T0)).Landed);

        var clock = new Clock(T0.AddSeconds(1));
        HitResult answer;

        await using (SpaceMmoDbContext other = _fixture.CreateContext())
        {
            // Orc's next shot, at two seconds, holds the pair while Ayla's answer, arrived at one, waits behind it.
            await using var holding = await other.Database.BeginTransactionAsync();

            await other.Characters
                .FromSqlInterpolated(
                    $"SELECT * FROM characters WHERE id IN ({_ayla}, {_orc}) ORDER BY id FOR UPDATE")
                .ToListAsync();

            await other.Database.ExecuteSqlInterpolatedAsync(
                $"UPDATE attack_records SET attacked_at = {T0.AddSeconds(2)} WHERE attacker_character_id = {_orc}");

            await using SpaceMmoDbContext context = _fixture.CreateContext();

            Task<HitResult> waiting = Service(context).HitAsync(
                new HitRequest(_ayla, _orc, HitZone.Body, HitFacing.Front, InBsHalf), clock);

            await BlockedOnALockAsync();

            clock.Now = T0.AddSeconds(3);
            await holding.CommitAsync();

            answer = await waiting;
        }

        // Judged at one second, the provocation would be in its future, and refused.
        Assert.True(answer.Landed, answer.Refusal);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.Equal(T0.AddSeconds(3), (await verify.Characters.SingleAsync(c => c.Id == _ayla)).LastCombatAt);
        Assert.Equal(T0.AddSeconds(3), (await verify.Characters.SingleAsync(c => c.Id == _orc)).LastCombatAt);
    }

    /// <summary>One hit by Orc on Ayla, counted from outside, and that EF made no savepoint for it.</summary>
    private async Task<HitTiming> CountedHitAsync(DateTimeOffset when)
    {
        var sent = new StatementsSent();

        await using SpaceMmoDbContext context = CountingContext(sent);

        HitResult hit = await Service(context).HitAsync(
            new HitRequest(_orc, _ayla, HitZone.Body, HitFacing.Front, InBsHalf), new Clock(when));

        Assert.True(hit.Landed, hit.Refusal);
        Assert.Equal(sent.Count, hit.Timing!.RoundTrips.Count);

        // A savepoint is two round trips the tally cannot see: EF sends it through the transaction, not as a command.
        Assert.Equal(0, sent.Savepoints);

        return hit.Timing;
    }

    private async Task BlockedOnALockAsync()
    {
        await using SpaceMmoDbContext watcher = _fixture.CreateContext();

        for (int tries = 0; tries < 200; tries++)
        {
            int blocked = await watcher.Database
                .SqlQuery<int>($"""
                    SELECT count(*)::int AS "Value" FROM pg_stat_activity
                    WHERE datname = current_database() AND wait_event_type = 'Lock'
                    """)
                .SingleAsync();

            if (blocked > 0)
            {
                return;
            }

            await Task.Delay(50);
        }

        Assert.Fail("The hit never queued behind the held lock.");
    }

    // ── Helpers ──────────────────────────────────────────────────────────────

    private SpaceMmoDbContext CountingContext(StatementsSent sent) =>
        new(new DbContextOptionsBuilder<SpaceMmoDbContext>()
            .UseNpgsql(_fixture.ConnectionString)
            .UseSnakeCaseNamingConvention()
            .AddInterceptors(sent, sent.Commits)
            .Options);

    /// <summary>Statements and commits as they leave, the other end from the tally's.</summary>
    private sealed class StatementsSent : DbCommandInterceptor
    {
        private int _count;
        private int _savepoints;

        public StatementsSent() => Commits = new CommitsSent(this);

        public int Count => _count;

        public int Savepoints => _savepoints;

        public IInterceptor Commits { get; }

        public override ValueTask<InterceptionResult<DbDataReader>> ReaderExecutingAsync(
            DbCommand command, CommandEventData eventData, InterceptionResult<DbDataReader> result,
            CancellationToken cancellationToken = default)
        {
            Interlocked.Increment(ref _count);

            return ValueTask.FromResult(result);
        }

        public override ValueTask<InterceptionResult<int>> NonQueryExecutingAsync(
            DbCommand command, CommandEventData eventData, InterceptionResult<int> result,
            CancellationToken cancellationToken = default)
        {
            Interlocked.Increment(ref _count);

            return ValueTask.FromResult(result);
        }

        public override ValueTask<InterceptionResult<object>> ScalarExecutingAsync(
            DbCommand command, CommandEventData eventData, InterceptionResult<object> result,
            CancellationToken cancellationToken = default)
        {
            Interlocked.Increment(ref _count);

            return ValueTask.FromResult(result);
        }

        private sealed class CommitsSent(StatementsSent owner) : DbTransactionInterceptor
        {
            public override ValueTask<InterceptionResult> TransactionCommittingAsync(
                DbTransaction transaction, TransactionEventData eventData, InterceptionResult result,
                CancellationToken cancellationToken = default)
            {
                Interlocked.Increment(ref owner._count);

                return ValueTask.FromResult(result);
            }

            public override ValueTask<InterceptionResult> CreatingSavepointAsync(
                DbTransaction transaction, TransactionEventData eventData, InterceptionResult result,
                CancellationToken cancellationToken = default)
            {
                Interlocked.Increment(ref owner._savepoints);

                return ValueTask.FromResult(result);
            }
        }
    }

    /// <summary>A clock the test sets: a hit reads it once it holds its lock.</summary>
    private sealed class Clock(DateTimeOffset now) : TimeProvider
    {
        public DateTimeOffset Now { get; set; } = now;

        public override DateTimeOffset GetUtcNow() => Now;
    }

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
        }, new Skill
        {
            Key = "stamina", Name = "Stamina", Category = SkillCategory.Combat, XpPerHundredStaminaSpent = 100,
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
