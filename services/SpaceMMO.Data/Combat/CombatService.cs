using System.Diagnostics;
using Microsoft.EntityFrameworkCore;
using SpaceMMO.Data.Diagnostics;
using SpaceMMO.Data.Entities;
using SpaceMMO.Data.Progression;
using SpaceMMO.Domain.Characters;
using SpaceMMO.Domain.Combat;
using SpaceMMO.Domain.Items;
using SpaceMMO.Domain.Loss;
using SpaceMMO.Domain.Progression;
using SpaceMMO.Domain.Quests;
using SpaceMMO.Domain.Universe;

namespace SpaceMMO.Data.Combat;

/// <summary>One hit, as the game server saw it (task 194).</summary>
/// <param name="AttackerCharacterId">Who fired.</param>
/// <param name="TargetCharacterId">Who was hit.</param>
/// <param name="Zone">Where on the body.</param>
/// <param name="Facing">From which side of them.</param>
/// <param name="Position">Where the shot was fired from, in system kilometres: what decides whether it was allowed.</param>
public readonly record struct HitRequest(
    int AttackerCharacterId, int TargetCharacterId, HitZone Zone, HitFacing Facing, SystemPosition Position);

/// <summary>What a hit did, or why it did nothing.</summary>
/// <param name="Refusal">
/// Null for a hit that landed; otherwise <c>self</c>, <c>attacker_dead</c>, <c>target_dead</c>, <c>no_weapon</c>,
/// <c>no_rules</c> or <c>not_allowed</c>.
/// </param>
public sealed record HitResult(
    string? Refusal,
    int Damage,
    int TargetHealth,
    int TargetMaxHealth,
    bool Killed,
    string? WeaponSkillKey,
    long WeaponXp)
{
    public bool Landed => Refusal is null;

    /// <summary>Where the time went in deciding this, for the log (task 198).</summary>
    public HitTiming? Timing { get; init; }

    public static HitResult Refused(string reason) => new(reason, 0, 0, 0, false, null, 0);
}

/// <summary>
/// Where one hit's time went (task 198): a hit took 17–24 ms with the game closed and seconds while it ran.
/// </summary>
/// <param name="Total">From being asked to answering.</param>
/// <param name="Connecting">Opening the connection, from the pool or new.</param>
/// <param name="ReadsBeforeLock">
/// How many of the first <paramref name="RoundTrips"/> were reads made before the lock: what a hit on either
/// character cannot change, which delays this hit and not the ones queued behind it.
/// </param>
/// <param name="LockRoundTrips">
/// How many round trips after those it took to hold both characters' rows. That statement's time is its round trip
/// plus however long another hit on either character kept them.
/// </param>
/// <param name="RoundTrips">Every statement and commit, in order, as long as each took to answer.</param>
public sealed record HitTiming(
    TimeSpan Total, TimeSpan Connecting, int ReadsBeforeLock, int LockRoundTrips, IReadOnlyList<TimeSpan> RoundTrips)
{
    /// <summary>The reads before the lock.</summary>
    public TimeSpan Reading => Sum(RoundTrips.Take(ReadsBeforeLock));

    /// <summary>Taking the pair's lock, waiting included.</summary>
    public TimeSpan Locking => Sum(RoundTrips.Skip(ReadsBeforeLock).Take(LockRoundTrips));

    /// <summary>How many round trips were made holding the lock, which every hit queued behind it waits out.</summary>
    public int HoldingRoundTrips => Math.Max(0, RoundTrips.Count - ReadsBeforeLock - LockRoundTrips);

    /// <summary>The round trips made holding the lock.</summary>
    public TimeSpan Holding => Sum(RoundTrips.Skip(ReadsBeforeLock + LockRoundTrips));

    /// <summary>What is left: the API itself, between one round trip and the next.</summary>
    public TimeSpan Between => Total - Connecting - Reading - Locking - Holding;

    private static TimeSpan Sum(IEnumerable<TimeSpan> spans) =>
        spans.Aggregate(TimeSpan.Zero, (sum, span) => sum + span);
}

/// <summary>The weapon a character would fire: what is in Weapon 1 (task 194).</summary>
public sealed record WeaponLoadout(
    long ItemInstanceId, string ItemKey, string Name, string SkillKey, int Damage, int CooldownMs, int RangeMetres);

/// <summary>A character's health now, and what they would fire.</summary>
public sealed record CombatStatus(int Health, int MaxHealth, DateTimeOffset? LastCombatAt, WeaponLoadout? Weapon);

/// <summary>Where a character came back after dying.</summary>
public sealed record RespawnResult(int StationId, string StationKey, int Health, int MaxHealth);

/// <summary>Thrown when a respawn is asked for and cannot happen.</summary>
public sealed class RespawnException(string message, string reason) : InvalidOperationException(message)
{
    /// <summary><c>not_dead</c>, <c>no_character</c> or <c>no_station</c>.</summary>
    public string Reason { get; } = reason;
}

/// <summary>
/// Hits, health and the attack record (task 194): what the game server reports and what persists.
/// </summary>
/// <remarks>
/// <para>
/// <strong>The game server decides what was hit; this decides what it did</strong> (ADR-0003: persistence lives
/// here, never in UE). The server traces the shot and says who was hit, where and from which side; this judges
/// whether the attack was allowed (<see cref="Pvp.CanAttack"/>), what the weapon in Weapon 1 does, the
/// target's health, the shooter's XP and the attack record that lets the target answer back.
/// </para>
/// <para>
/// <strong>Health is stored as of the last fight</strong> (<see cref="CombatHealth.Current"/>), so both
/// characters' health is written as it stands now before either's last-combat time moves: otherwise the
/// regeneration owed up to this moment would be lost, or granted twice.
/// </para>
/// </remarks>
public sealed class CombatService(SpaceMmoDbContext database)
{
    private readonly SpaceMmoDbContext _database =
        database ?? throw new ArgumentNullException(nameof(database));

    /// <summary>A character's health now and the weapon they would fire.</summary>
    public async Task<CombatStatus?> StatusAsync(
        int characterId, DateTimeOffset now, CancellationToken cancellationToken = default)
    {
        Character? character = await _database.Characters
            .SingleOrDefaultAsync(c => c.Id == characterId, cancellationToken);

        if (character is null)
        {
            return null;
        }

        int max = await MaxHealthAsync(characterId, cancellationToken);
        int health = CombatHealth.Current(character.Health, max, character.LastCombatAt, now);

        return new CombatStatus(health, max, character.LastCombatAt, await LoadoutAsync(characterId, cancellationToken));
    }

    /// <summary>Applies one hit, if the rules allow it.</summary>
    /// <remarks>
    /// <para>
    /// <strong>Three round trips holding the lock, not fifteen</strong> (task 198). Hits on a pair queue behind each
    /// other's lock, so whatever a hit does while holding it, every hit behind it waits out too. On a machine whose
    /// cores the game fills, a round trip through Docker into its virtual machine costs about 40 ms, and fifteen of
    /// them held the lock 0.6 s a hit: two players firing outran it, and the queue grew to ten seconds. So what a
    /// hit on either character cannot change is read before the lock, and after it come one read of what another
    /// hit could have changed, the save and the commit.
    /// </para>
    /// <para>
    /// <strong>The read cannot share the lock's statement.</strong> A statement sees the database as it was when the
    /// statement began. The rows it locks are fetched again once the lock is granted, but anything it joins to them
    /// is not, so a hit that queued would read the skill rows and attack records from before the hit ahead of it:
    /// it would overwrite that hit's XP, insert a row that hit had already inserted, and judge an answer by an old
    /// attack. Tried, and caught by
    /// <c>A_hit_queued_behind_another_keeps_what_that_hit_wrote</c>, which fails on a duplicate key.
    /// </para>
    /// <para>
    /// <strong>A clock, not a time</strong>, read once the lock is held. A hit that queued is decided after the hits
    /// ahead of it, which recorded their attacks and fights at their own later times; judged by when it arrived, an
    /// answer would find its provocation in the future and be refused, and a fight's clock could be written
    /// backwards.
    /// </para>
    /// </remarks>
    public async Task<HitResult> HitAsync(
        HitRequest request, TimeProvider clock, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(clock);

        long started = Stopwatch.GetTimestamp();
        using RoundTripTally tally = RoundTripTally.Begin();
        int readsBeforeLock = 0;
        int lockRoundTrips = 0;

        // Every answer says where its time went (task 198). A refusal is stamped before its rollback, which is one
        // more round trip than it counts.
        HitResult Timed(HitResult result) => result with
        {
            Timing = new HitTiming(
                Stopwatch.GetElapsedTime(started), tally.Connecting, readsBeforeLock, lockRoundTrips,
                [.. tally.RoundTrips]),
        };

        if (request.AttackerCharacterId == request.TargetCharacterId)
        {
            return Timed(HitResult.Refused("self"));
        }

        // One connection for the whole hit, rather than one taken from the pool for each read.
        await _database.Database.OpenConnectionAsync(cancellationToken);

        // What a hit on either character cannot change: the weapon, the rules, protection and the skills.
        WeaponLoadout? weapon = await LoadoutAsync(request.AttackerCharacterId, cancellationToken);
        Rules? rules = await RulesAsync(cancellationToken);
        bool targetIsProtected = await IsProtectedAsync(request.TargetCharacterId, cancellationToken);
        List<Skill> skills = await _database.Skills.ToListAsync(cancellationToken);

        readsBeforeLock = tally.RoundTrips.Count;

        await using var transaction = await _database.Database.BeginTransactionAsync(cancellationToken);

        // The save is one statement and a failure abandons the whole hit, so a savepoint around it protects nothing
        // and costs two more round trips under the lock.
        _database.Database.AutoSavepointsEnabled = false;

        (Character attacker, Character target) = await LockPairAsync(
            request.AttackerCharacterId, request.TargetCharacterId, cancellationToken);

        lockRoundTrips = tally.RoundTrips.Count - readsBeforeLock;

        DateTimeOffset now = clock.GetUtcNow();

        PairState pair = await ReadPairAsync(attacker.Id, target.Id, cancellationToken);

        int? constitutionId = skills.SingleOrDefault(s => s.Key == CombatXp.ConstitutionSkillKey)?.Id;
        int attackerMax = CombatHealth.Max(SkillCurve.LevelForXp(pair.Xp(attacker.Id, constitutionId)));
        int targetMax = CombatHealth.Max(SkillCurve.LevelForXp(pair.Xp(target.Id, constitutionId)));

        int attackerHealth = CombatHealth.Current(attacker.Health, attackerMax, attacker.LastCombatAt, now);
        int targetHealth = CombatHealth.Current(target.Health, targetMax, target.LastCombatAt, now);

        if (attackerHealth <= 0)
        {
            return Timed(HitResult.Refused("attacker_dead"));
        }

        if (targetHealth <= 0)
        {
            return Timed(HitResult.Refused("target_dead"));
        }

        if (weapon is null)
        {
            return Timed(HitResult.Refused("no_weapon"));
        }

        if (rules is null)
        {
            return Timed(HitResult.Refused("no_rules"));
        }

        var attempt = new AttackAttempt
        {
            AttackerFaction = Races.FactionFor(attacker.Race),
            TargetFaction = Races.FactionFor(target.Race),
            Position = request.Position,
            TargetIsProtected = targetIsProtected,
            TargetAttackedAttackerAt = pair.Attack(target.Id, attacker.Id)?.AttackedAt,
            Now = now,
        };

        if (!Pvp.CanAttack(rules.Space, attempt))
        {
            return Timed(HitResult.Refused("not_allowed"));
        }

        RecordAttack(pair, attacker.Id, target.Id, now);

        int damage = Hits.Damage(weapon.Damage, request.Zone, request.Facing);

        // XP for the damage that landed, not for overkill: a 90-point shot on somebody with 10 left is worth 10.
        int dealt = Math.Min(damage, targetHealth);
        int targetAfter = targetHealth - dealt;

        // Health as it stands now, then the clock: see the class remarks.
        attacker.Health = attackerHealth;
        attacker.LastCombatAt = now;
        target.Health = targetAfter;
        target.LastCombatAt = now;

        long weaponXp = AwardCombatXp(skills, attacker.Id, weapon.SkillKey, dealt);

        bool killed = targetAfter == 0;

        if (killed)
        {
            _database.DeathRecords.Add(new DeathRecord
            {
                CharacterId = target.Id,
                Cause = DeathCause.PersonalCombatPlanet,

                // ADR-0016 has no roll in it: what a death costs follows from what was carried.
                ResolutionSeed = 0,

                // The system whose rules allowed it.
                StarSystemId = rules.StarSystemId,
                KillerCharacterId = attacker.Id,
                OccurredAt = now,
            });
        }

        await _database.SaveChangesAsync(cancellationToken);
        await transaction.CommitAsync(cancellationToken);

        return Timed(new HitResult(null, dealt, targetAfter, targetMax, killed, weapon.SkillKey, weaponXp));
    }

    /// <summary>
    /// Brings a dead character back at the station they last docked at, with full health (ADR-0016).
    /// </summary>
    /// <remarks>
    /// A character who never docked anywhere comes back at the first station on their home world, which is
    /// where they started. What dying costs (ADR-0016's materials and wear) is task 103, not this.
    /// </remarks>
    /// <exception cref="RespawnException">If they are not dead, or there is nowhere to put them.</exception>
    public async Task<RespawnResult> RespawnAsync(
        int characterId, DateTimeOffset now, CancellationToken cancellationToken = default)
    {
        Character character = await _database.Characters
            .SingleOrDefaultAsync(c => c.Id == characterId, cancellationToken)
            ?? throw new RespawnException($"No character {characterId}.", "no_character");

        int max = await MaxHealthAsync(characterId, cancellationToken);

        if (CombatHealth.Current(character.Health, max, character.LastCombatAt, now) > 0)
        {
            throw new RespawnException("Only the dead respawn.", "not_dead");
        }

        Station station = await _database.Stations
            .Where(s => s.Id == character.LastDockedStationId)
            .SingleOrDefaultAsync(cancellationToken)
            ?? await _database.Stations
                .Where(s => s.BodyId == character.HomeBodyId && s.RetiredAt == null)
                .OrderBy(s => s.Id)
                .FirstOrDefaultAsync(cancellationToken)
            ?? throw new RespawnException("There is no station to come back at.", "no_station");

        // Full health, and no fight to wait out.
        character.Health = null;
        character.LastCombatAt = null;

        await _database.SaveChangesAsync(cancellationToken);

        return new RespawnResult(station.Id, station.Key, max, max);
    }

    /// <summary>The most health a character has: their constitution level's (ADR-0017).</summary>
    public async Task<int> MaxHealthAsync(int characterId, CancellationToken cancellationToken = default)
    {
        long xp = await _database.CharacterSkills
            .Where(s => s.CharacterId == characterId && s.Skill!.Key == CombatXp.ConstitutionSkillKey)
            .Select(s => s.Xp)
            .SingleOrDefaultAsync(cancellationToken);

        return CombatHealth.Max(SkillCurve.LevelForXp(xp));
    }

    /// <summary>The weapon in Weapon 1, if it is a weapon and not broken.</summary>
    /// <remarks>
    /// Weapon 1 only, for now: switching between the two weapons is a client action that does not exist yet,
    /// and until it does the first slot is the one held.
    /// </remarks>
    public async Task<WeaponLoadout?> LoadoutAsync(int characterId, CancellationToken cancellationToken = default) =>
        await _database.ItemInstances
            .Where(i => i.Inventory!.CharacterId == characterId
                && i.Inventory.Kind == InventoryKind.Equipped
                && i.EquippedSlot == EquipmentSlot.Weapon1
                && i.DestroyedAt == null
                && i.Condition > 0
                && i.ItemDef!.WeaponSkillId != null)
            .Select(i => new WeaponLoadout(
                i.Id,
                i.ItemDef!.Key,
                i.ItemDef.Name,
                i.ItemDef.WeaponSkill!.Key,
                i.ItemDef.WeaponDamage!.Value,
                i.ItemDef.WeaponCooldownMs!.Value,
                i.ItemDef.WeaponRangeMetres!.Value))
            .SingleOrDefaultAsync(cancellationToken);

    /// <summary>
    /// Whether a character is still under new-player protection: the main story not yet finished (ADR-0008).
    /// </summary>
    /// <remarks>One round trip: any live main-story quest they have not completed.</remarks>
    public async Task<bool> IsProtectedAsync(int characterId, CancellationToken cancellationToken = default) =>
        await _database.QuestDefs
            .Where(q => q.Kind == QuestKind.MainStory && q.RetiredAt == null)
            .AnyAsync(
                q => !_database.CharacterQuests.Any(c =>
                    c.CharacterId == characterId && c.QuestDefId == q.Id && c.State == QuestState.Completed),
                cancellationToken);

    /// <summary>The system's faction space, as seeded from content (task 104), or null if none is.</summary>
    public async Task<FactionSpace?> FactionSpaceAsync(CancellationToken cancellationToken = default) =>
        (await RulesAsync(cancellationToken))?.Space;

    /// <summary>The faction space and the system it belongs to, in one round trip.</summary>
    private async Task<Rules?> RulesAsync(CancellationToken cancellationToken)
    {
        var found = await _database.StarSystems
            .Where(s => s.FactionCapitalBodyId != null)
            .OrderBy(s => s.Id)
            .Select(s => new
            {
                System = s,
                Capital = _database.Bodies.FirstOrDefault(b => b.Id == s.FactionCapitalBodyId),
            })
            .FirstOrDefaultAsync(cancellationToken);

        if (found is null || found.Capital?.SystemX is not double x || found.Capital.SystemY is not double y
            || found.Capital.SystemZ is not double z)
        {
            return null;
        }

        StarSystem system = found.System;

        var content = new Domain.Content.FactionSpaceContent(
            found.Capital.Key,
            system.FactionSafeRadiusKm!.Value,
            system.FactionContestedRadiusKm!.Value,
            [system.FactionADirectionX!.Value, system.FactionADirectionY!.Value, system.FactionADirectionZ!.Value],
            system.FactionRetaliationSeconds!.Value);

        return new Rules(FactionSpace.FromContent(content, new SystemPosition(x, y, z)), system.Id);
    }

    /// <summary>
    /// The weapon's XP, shared with its siblings (ADR-0015), and constitution's third on top (ADR-0017).
    /// </summary>
    /// <remarks>
    /// Into the attacker's skill rows <see cref="ReadPairAsync"/> already holds, so awarding is never a round trip
    /// under the lock (<see cref="SkillAwards.AwardTracked"/>).
    /// </remarks>
    /// <returns>What the weapon used kept, for the report.</returns>
    private long AwardCombatXp(IReadOnlyList<Skill> skills, int characterId, string weaponSkillKey, int damage)
    {
        Skill used = skills.Single(s => s.Key == weaponSkillKey);

        if (used.AsWeapon() is not { } usedWeapon || used.XpPerHundredDamage is not int rate)
        {
            return 0;
        }

        long xp = CombatXp.ForDamage(damage, rate);

        IReadOnlyDictionary<string, long> shares = WeaponXp.Share(
            usedWeapon, xp, skills.Select(s => s.AsWeapon()).OfType<WeaponSkill>());

        foreach ((string key, long share) in shares)
        {
            SkillAwards.AwardTracked(_database, characterId, skills.Single(s => s.Key == key).Id, share);
        }

        if (skills.SingleOrDefault(s => s.Key == CombatXp.ConstitutionSkillKey) is { } constitution)
        {
            SkillAwards.AwardTracked(_database, characterId, constitution.Id, CombatXp.ConstitutionShare(xp));
        }

        return shares.GetValueOrDefault(weaponSkillKey);
    }

    private void RecordAttack(PairState pair, int attackerId, int targetId, DateTimeOffset now)
    {
        if (pair.Attack(attackerId, targetId) is { } record)
        {
            record.AttackedAt = now;

            return;
        }

        _database.AttackRecords.Add(new AttackRecord
        {
            AttackerCharacterId = attackerId,
            TargetCharacterId = targetId,
            AttackedAt = now,
        });
    }

    /// <summary>Both characters, locked, as they stand once the lock is granted.</summary>
    /// <remarks>Lower id first, so two players shooting each other at the same moment cannot deadlock.</remarks>
    private async Task<(Character Attacker, Character Target)> LockPairAsync(
        int attackerId, int targetId, CancellationToken cancellationToken)
    {
        int first = Math.Min(attackerId, targetId);
        int second = Math.Max(attackerId, targetId);

        List<Character> locked = await _database.Characters
            .FromSqlInterpolated(
                $"SELECT * FROM characters WHERE id IN ({first}, {second}) ORDER BY id FOR UPDATE")
            .ToListAsync(cancellationToken);

        Character attacker = locked.SingleOrDefault(c => c.Id == attackerId)
            ?? throw new InvalidOperationException($"Character {attackerId} does not exist.");
        Character target = locked.SingleOrDefault(c => c.Id == targetId)
            ?? throw new InvalidOperationException($"Character {targetId} does not exist.");

        return (attacker, target);
    }

    /// <summary>
    /// Everything about the pair another hit could have changed: their skill rows and the attacks between them, in
    /// one round trip, read once the lock is held so it sees what the hit ahead of this one wrote.
    /// </summary>
    /// <remarks>
    /// A single query on purpose: split, it would be a round trip for each collection, under the lock.
    /// </remarks>
    private async Task<PairState> ReadPairAsync(int attackerId, int targetId, CancellationToken cancellationToken)
    {
        int[] pair = [attackerId, targetId];

        var read = await _database.Characters
            .Where(c => pair.Contains(c.Id))
            .AsSingleQuery()
            .Select(c => new
            {
                Skills = _database.CharacterSkills.Where(s => s.CharacterId == c.Id).ToList(),
                Attacks = _database.AttackRecords
                    .Where(r => r.AttackerCharacterId == c.Id && pair.Contains(r.TargetCharacterId))
                    .ToList(),
            })
            .ToListAsync(cancellationToken);

        return new PairState([.. read.SelectMany(r => r.Skills)], [.. read.SelectMany(r => r.Attacks)]);
    }

    /// <summary>The faction space a hit is judged by, and the system it belongs to.</summary>
    private sealed record Rules(FactionSpace Space, int StarSystemId);

    /// <summary>The pair's skill rows and the attacks between them, as read under the lock.</summary>
    private sealed record PairState(IReadOnlyList<CharacterSkill> Skills, IReadOnlyList<AttackRecord> Attacks)
    {
        public long Xp(int characterId, int? skillId) =>
            Skills.SingleOrDefault(s => s.CharacterId == characterId && s.SkillId == skillId)?.Xp ?? 0;

        public AttackRecord? Attack(int attackerId, int targetId) =>
            Attacks.SingleOrDefault(r => r.AttackerCharacterId == attackerId && r.TargetCharacterId == targetId);
    }
}
