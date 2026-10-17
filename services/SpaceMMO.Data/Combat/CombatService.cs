using Microsoft.EntityFrameworkCore;
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

    public static HitResult Refused(string reason) => new(reason, 0, 0, 0, false, null, 0);
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
    public async Task<HitResult> HitAsync(
        HitRequest request, DateTimeOffset now, CancellationToken cancellationToken = default)
    {
        if (request.AttackerCharacterId == request.TargetCharacterId)
        {
            return HitResult.Refused("self");
        }

        await using var transaction = await _database.Database.BeginTransactionAsync(cancellationToken);

        // Both locked, lower id first, so two players shooting each other at the same moment cannot deadlock.
        (Character attacker, Character target) = await LockPairAsync(
            request.AttackerCharacterId, request.TargetCharacterId, cancellationToken);

        int attackerMax = await MaxHealthAsync(attacker.Id, cancellationToken);
        int targetMax = await MaxHealthAsync(target.Id, cancellationToken);

        int attackerHealth = CombatHealth.Current(attacker.Health, attackerMax, attacker.LastCombatAt, now);
        int targetHealth = CombatHealth.Current(target.Health, targetMax, target.LastCombatAt, now);

        if (attackerHealth <= 0)
        {
            return HitResult.Refused("attacker_dead");
        }

        if (targetHealth <= 0)
        {
            return HitResult.Refused("target_dead");
        }

        WeaponLoadout? weapon = await LoadoutAsync(attacker.Id, cancellationToken);

        if (weapon is null)
        {
            return HitResult.Refused("no_weapon");
        }

        FactionSpace? space = await FactionSpaceAsync(cancellationToken);

        if (space is null)
        {
            return HitResult.Refused("no_rules");
        }

        DateTimeOffset? targetAttackedAttacker = await _database.AttackRecords
            .Where(r => r.AttackerCharacterId == target.Id && r.TargetCharacterId == attacker.Id)
            .Select(r => (DateTimeOffset?)r.AttackedAt)
            .SingleOrDefaultAsync(cancellationToken);

        var attempt = new AttackAttempt
        {
            AttackerFaction = Races.FactionFor(attacker.Race),
            TargetFaction = Races.FactionFor(target.Race),
            Position = request.Position,
            TargetIsProtected = await IsProtectedAsync(target.Id, cancellationToken),
            TargetAttackedAttackerAt = targetAttackedAttacker,
            Now = now,
        };

        if (!Pvp.CanAttack(space, attempt))
        {
            return HitResult.Refused("not_allowed");
        }

        await RecordAttackAsync(attacker.Id, target.Id, now, cancellationToken);

        int damage = Hits.Damage(weapon.Damage, request.Zone, request.Facing);

        // XP for the damage that landed, not for overkill: a 90-point shot on somebody with 10 left is worth 10.
        int dealt = Math.Min(damage, targetHealth);
        int targetAfter = targetHealth - dealt;

        // Health as it stands now, then the clock: see the class remarks.
        attacker.Health = attackerHealth;
        attacker.LastCombatAt = now;
        target.Health = targetAfter;
        target.LastCombatAt = now;

        long weaponXp = await AwardCombatXpAsync(attacker.Id, weapon.SkillKey, dealt, cancellationToken);

        bool killed = targetAfter == 0;

        if (killed)
        {
            _database.DeathRecords.Add(new DeathRecord
            {
                CharacterId = target.Id,
                Cause = DeathCause.PersonalCombatPlanet,

                // ADR-0016 has no roll in it: what a death costs follows from what was carried.
                ResolutionSeed = 0,
                StarSystemId = await _database.StarSystems.Select(s => s.Id).FirstAsync(cancellationToken),
                KillerCharacterId = attacker.Id,
                OccurredAt = now,
            });
        }

        await _database.SaveChangesAsync(cancellationToken);
        await transaction.CommitAsync(cancellationToken);

        return new HitResult(null, dealt, targetAfter, targetMax, killed, weapon.SkillKey, weaponXp);
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
    public async Task<bool> IsProtectedAsync(int characterId, CancellationToken cancellationToken = default)
    {
        int mainStory = await _database.QuestDefs
            .CountAsync(q => q.Kind == QuestKind.MainStory && q.RetiredAt == null, cancellationToken);

        int finished = await _database.CharacterQuests
            .Where(q => q.CharacterId == characterId
                && q.State == QuestState.Completed
                && q.QuestDef!.Kind == QuestKind.MainStory
                && q.QuestDef.RetiredAt == null)
            .Select(q => q.QuestDefId)
            .Distinct()
            .CountAsync(cancellationToken);

        return finished < mainStory;
    }

    /// <summary>The system's faction space, as seeded from content (task 104), or null if none is.</summary>
    public async Task<FactionSpace?> FactionSpaceAsync(CancellationToken cancellationToken = default)
    {
        StarSystem? system = await _database.StarSystems
            .Where(s => s.FactionCapitalBodyId != null)
            .OrderBy(s => s.Id)
            .FirstOrDefaultAsync(cancellationToken);

        Body? capital = system is null
            ? null
            : await _database.Bodies.SingleOrDefaultAsync(b => b.Id == system.FactionCapitalBodyId, cancellationToken);

        if (system is null || capital?.SystemX is not double x || capital.SystemY is not double y
            || capital.SystemZ is not double z)
        {
            return null;
        }

        var content = new Domain.Content.FactionSpaceContent(
            capital.Key,
            system.FactionSafeRadiusKm!.Value,
            system.FactionContestedRadiusKm!.Value,
            [system.FactionADirectionX!.Value, system.FactionADirectionY!.Value, system.FactionADirectionZ!.Value],
            system.FactionRetaliationSeconds!.Value);

        return FactionSpace.FromContent(content, new SystemPosition(x, y, z));
    }

    /// <summary>
    /// The weapon's XP, shared with its siblings (ADR-0015), and constitution's third on top (ADR-0017).
    /// </summary>
    /// <returns>What the weapon used kept, for the report.</returns>
    private async Task<long> AwardCombatXpAsync(
        int characterId, string weaponSkillKey, int damage, CancellationToken cancellationToken)
    {
        List<Skill> skills = await _database.Skills.ToListAsync(cancellationToken);

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
            await SkillAwards.AwardAsync(_database, characterId, skills.Single(s => s.Key == key).Id, share, cancellationToken);
        }

        if (skills.SingleOrDefault(s => s.Key == CombatXp.ConstitutionSkillKey) is { } constitution)
        {
            await SkillAwards.AwardAsync(
                _database, characterId, constitution.Id, CombatXp.ConstitutionShare(xp), cancellationToken);
        }

        return shares.GetValueOrDefault(weaponSkillKey);
    }

    private async Task RecordAttackAsync(
        int attackerId, int targetId, DateTimeOffset now, CancellationToken cancellationToken)
    {
        AttackRecord? record = await _database.AttackRecords
            .SingleOrDefaultAsync(r => r.AttackerCharacterId == attackerId && r.TargetCharacterId == targetId,
                cancellationToken);

        if (record is null)
        {
            _database.AttackRecords.Add(new AttackRecord
            {
                AttackerCharacterId = attackerId,
                TargetCharacterId = targetId,
                AttackedAt = now,
            });

            return;
        }

        record.AttackedAt = now;
    }

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
}
