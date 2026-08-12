using Microsoft.EntityFrameworkCore;
using SpaceMMO.Data.Entities;
using SpaceMMO.Data.Progression;
using SpaceMMO.Data.Industry;
using SpaceMMO.Data.Inventories;
using SpaceMMO.Domain.Economy;
using SpaceMMO.Domain.Gathering;
using SpaceMMO.Data.Quests;
using SpaceMMO.Domain.Progression;
using SpaceMMO.Domain.Quests;

namespace SpaceMMO.Data.Gathering;

/// <summary>The outcome of a gathering attempt.</summary>
/// <param name="ItemDefId">What was extracted.</param>
/// <param name="Quantity">Units extracted. Zero if too little time has passed or the node is spent.</param>
/// <param name="XpAwarded">Skill XP granted, proportional to units.</param>
/// <param name="NodeRemaining">What the node still holds afterwards.</param>
/// <param name="RespawnAt">When the node refills, if this attempt exhausted it.</param>
public readonly record struct GatherResult(
    int ItemDefId,
    int Quantity,
    long XpAwarded,
    int NodeRemaining,
    DateTimeOffset? RespawnAt)
{
    /// <summary>True if nothing was extracted.</summary>
    public bool IsEmpty => Quantity == 0;

    /// <summary>True if this attempt exhausted the node.</summary>
    public bool Depleted => NodeRemaining == 0;
}

/// <summary>
/// A deposit id that names nothing.
/// </summary>
/// <remarks>
/// Its own exception rather than the raw <see cref="InvalidOperationException"/> that
/// <c>SingleAsync</c> throws, because node ids arrive from clients and a client is free to send any
/// number at all. Left unhandled it was a 500 — an unhandled-fault log entry and an alert-worthy
/// status code for what is really just a caller naming something that is not there.
/// </remarks>
public sealed class UnknownResourceNodeException(long resourceNodeId)
    : Exception($"No resource node with id {resourceNodeId}.")
{
    public long ResourceNodeId { get; } = resourceNodeId;
}

/// <summary>
/// Extracts material from resource deposits — the only place material enters the economy.
/// </summary>
/// <remarks>
/// <para>
/// <strong>Server-authoritative.</strong> The client asks to gather; the server decides how much
/// wall-clock time has actually elapsed since the character last did, and grants only what that
/// entitles them to. A client calling in a tight loop extracts exactly as much as one calling at
/// the tick interval.
/// </para>
/// <para>
/// <strong>Sharing model is per node.</strong> A shared deposit has one depletion row that
/// everyone draws down; a per-character deposit has one row each. Both go through the same code
/// path here — the only difference is which state row gets resolved and locked — so switching a
/// node between the two is an <c>UPDATE</c> on one column.
/// </para>
/// <para>
/// <strong>Respawn is lazy.</strong> A depleted node refills when someone next tries to work it,
/// rather than on a timer. Nothing observes a node except a player gathering from it, so a
/// background sweeper would be pure cost.
/// </para>
/// </remarks>
public sealed class GatheringService(SpaceMmoDbContext database)
{
    private readonly SpaceMmoDbContext _database =
        database ?? throw new ArgumentNullException(nameof(database));

    private readonly InventoryService _inventories = new(database);

    /// <summary>
    /// Extracts whatever the elapsed time, the character's skill, and the node currently allow.
    /// </summary>
    /// <remarks>
    /// Returns an empty result rather than throwing when too little time has passed or the node is
    /// spent. Those are ordinary states a client polls through, not errors.
    /// </remarks>
    /// <exception cref="SkillTooLowException">If the character's skill is below the node's requirement.</exception>
    /// <exception cref="MissingToolException">If the node needs a tool the character does not hold.</exception>
    public async Task<GatherResult> GatherAsync(
        int characterId,
        long resourceNodeId,
        int stationIdForStorage,
        CancellationToken cancellationToken = default)
    {
        await using var transaction =
            await _database.Database.BeginTransactionAsync(cancellationToken);

        // Character first, matching the lock order used by the market and industry services, so
        // none of the three can deadlock against each other.
        Character character = await LockCharacterAsync(characterId, cancellationToken);

        ResourceNode node = await _database.ResourceNodes
            .Include(n => n.Skill)
            .SingleOrDefaultAsync(n => n.Id == resourceNodeId, cancellationToken)
            ?? throw new UnknownResourceNodeException(resourceNodeId);

        int level = await SkillLevelAsync(characterId, node.SkillId, cancellationToken);

        if (level < node.RequiredLevel)
        {
            throw new SkillTooLowException(node.Skill!.Key, node.RequiredLevel, level);
        }

        await GuardToolAsync(characterId, node, cancellationToken);

        DateTimeOffset now = DateTimeOffset.UtcNow;

        ResourceNodeState state = await ResolveAndLockStateAsync(node, characterId, cancellationToken);

        ApplyRespawn(state, node, now);

        // A character who has never gathered is treated as fully rested rather than as having
        // waited zero seconds, so their first action is not an unexplained no-op.
        long elapsedSeconds = character.LastGatheredAt is DateTimeOffset last
            ? (long)Math.Max(0d, (now - last).TotalSeconds)
            : GatheringYield.TickSeconds * GatheringYield.MaxBankedTicks;

        int quantity = GatheringYield.UnitsAvailable(elapsedSeconds, level, state.QuantityRemaining);

        if (quantity == 0)
        {
            // Still commit: a lazy respawn applied above is real progress worth persisting.
            await _database.SaveChangesAsync(cancellationToken);
            await transaction.CommitAsync(cancellationToken);

            return new GatherResult(
                node.ItemDefId, 0, 0, state.QuantityRemaining, state.RespawnAt);
        }

        state.QuantityRemaining -= quantity;

        if (state.QuantityRemaining == 0)
        {
            state.RespawnAt = now.AddSeconds(node.RespawnSeconds);
        }

        Inventory hangar = await _inventories.GetOrCreateStationHangarAsync(
            characterId, stationIdForStorage, cancellationToken);

        // Gathered material enters at zero cost basis: it took labour, not credits. That zero is
        // what makes a hull built from self-gathered ore cost only its manufacturing fees
        // (ADR-0006).
        await _inventories.AddAsync(
            hangar.Id, node.ItemDefId, quantity, Credits.Zero, cancellationToken);

        long xp = quantity * GatheringYield.XpPerUnit;
        await AwardXpAsync(characterId, node.SkillId, xp, cancellationToken);

        // Advanced to now rather than by the ticks consumed, so banked time cannot be spent twice
        // by calling repeatedly.
        character.LastGatheredAt = now;

        // Reported from inside this transaction, not after it. A gather that committed while its
        // quest update failed would leave a player holding ore no quest ever counted, and the only
        // symptom would be a step that refuses to advance for reasons nothing logs.
        //
        // The item's key rather than its id, because that is what content authored the objective
        // against and ids differ between any two seeded databases.
        string itemKey = await _database.ItemDefs
            .Where(d => d.Id == node.ItemDefId)
            .Select(d => d.Key)
            .SingleAsync(cancellationToken);

        await new QuestService(_database).RecordProgressAsync(
            characterId,
            new ObjectiveEvent(ObjectiveType.Gather, itemKey, quantity),
            cancellationToken);

        await _database.SaveChangesAsync(cancellationToken);
        await transaction.CommitAsync(cancellationToken);

        return new GatherResult(
            node.ItemDefId, quantity, xp, state.QuantityRemaining, state.RespawnAt);
    }

    /// <summary>
    /// Finds the depletion row this character draws from, creating it on first use and locking it
    /// for the transaction.
    /// </summary>
    /// <remarks>
    /// <para>
    /// The single place the two sharing models differ. A shared node resolves to the row with a
    /// null owner, so every gatherer locks the <em>same</em> row and contends — which is exactly
    /// the intent. A per-character node resolves to that character's own row, so nobody contends
    /// with anybody.
    /// </para>
    /// <para>
    /// <c>FOR UPDATE</c> is what stops two gatherers on a shared node both reading the last ten
    /// units and both extracting them.
    /// </para>
    /// <para>
    /// On a node nobody has touched yet there is no row <em>to</em> lock, so two gatherers
    /// arriving together would both find nothing and both insert. The first read is therefore
    /// treated as optimistic: on a miss, an idempotent insert creates the row, and the read is
    /// repeated to take the lock properly. The unique indexes make the losing insert a no-op
    /// rather than a duplicate.
    /// </para>
    /// </remarks>
    private async Task<ResourceNodeState> ResolveAndLockStateAsync(
        ResourceNode node, int characterId, CancellationToken cancellationToken)
    {
        int? owner = node.SharingModel == NodeSharingModel.Shared ? null : characterId;

        ResourceNodeState? state = await SelectAndLockStateAsync(node.Id, owner, cancellationToken);

        if (state is not null)
        {
            return state;
        }

        // First touch of this deposit. Untouched deposits cost nothing to store, which is what
        // lets a generated universe hold far more of them than anyone will ever visit (ADR-0002).
        await InsertStateIfAbsentAsync(node, owner, cancellationToken);

        return await SelectAndLockStateAsync(node.Id, owner, cancellationToken)
            ?? throw new InvalidOperationException(
                $"State for node {node.Id} vanished immediately after being created.");
    }

    private async Task<ResourceNodeState?> SelectAndLockStateAsync(
        long nodeId, int? owner, CancellationToken cancellationToken)
    {
        FormattableString sql;

        if (owner is null)
        {
            sql = $"""
                SELECT * FROM resource_node_states
                WHERE resource_node_id = {nodeId} AND character_id IS NULL
                FOR UPDATE
                """;
        }
        else
        {
            sql = $"""
                SELECT * FROM resource_node_states
                WHERE resource_node_id = {nodeId} AND character_id = {owner.Value}
                FOR UPDATE
                """;
        }

        List<ResourceNodeState> rows = await _database.ResourceNodeStates
            .FromSqlInterpolated(sql)
            .ToListAsync(cancellationToken);

        return rows.Count == 1 ? rows[0] : null;
    }

    /// <summary>
    /// Creates the depletion row, doing nothing if a concurrent transaction got there first.
    /// </summary>
    /// <remarks>
    /// <c>ON CONFLICT DO NOTHING</c> rather than catching a unique violation, because a raised
    /// exception would abort the surrounding transaction in Postgres and lose the work already
    /// done in it.
    /// </remarks>
    private async Task InsertStateIfAbsentAsync(
        ResourceNode node, int? owner, CancellationToken cancellationToken)
    {
        int quantity = node.QuantityMax;

        FormattableString sql;

        if (owner is null)
        {
            sql = $"""
                INSERT INTO resource_node_states
                    (resource_node_id, character_id, quantity_remaining, respawn_at)
                VALUES ({node.Id}, NULL, {quantity}, NULL)
                ON CONFLICT DO NOTHING
                """;
        }
        else
        {
            sql = $"""
                INSERT INTO resource_node_states
                    (resource_node_id, character_id, quantity_remaining, respawn_at)
                VALUES ({node.Id}, {owner.Value}, {quantity}, NULL)
                ON CONFLICT DO NOTHING
                """;
        }

        await _database.Database.ExecuteSqlInterpolatedAsync(sql, cancellationToken);
    }

    /// <summary>Refills a depleted node whose respawn time has passed.</summary>
    private static void ApplyRespawn(ResourceNodeState state, ResourceNode node, DateTimeOffset now)
    {
        if (state.RespawnAt is not DateTimeOffset respawnAt || now < respawnAt)
        {
            return;
        }

        state.QuantityRemaining = node.QuantityMax;
        state.RespawnAt = null;
    }

    private async Task GuardToolAsync(
        int characterId, ResourceNode node, CancellationToken cancellationToken)
    {
        if (node.RequiredToolItemDefId is not int toolDefId)
        {
            return;
        }

        bool owned = await _database.ItemInstances.AnyAsync(
            instance => instance.ItemDefId == toolDefId
                && instance.DestroyedAt == null
                && instance.Condition > 0
                && instance.Inventory!.CharacterId == characterId,
            cancellationToken);

        if (!owned)
        {
            // Both, because the message is read by a person and the key is read by code.
            var tool = await _database.ItemDefs
                .Where(d => d.Id == toolDefId)
                .Select(d => new { d.Key, d.Name })
                .SingleAsync(cancellationToken);

            throw new MissingToolException(tool.Key, tool.Name);
        }
    }

    private async Task<int> SkillLevelAsync(
        int characterId, int skillId, CancellationToken cancellationToken)
    {
        long xp = await _database.CharacterSkills
            .Where(s => s.CharacterId == characterId && s.SkillId == skillId)
            .Select(s => s.Xp)
            .FirstOrDefaultAsync(cancellationToken);

        return SkillCurve.LevelForXp(xp);
    }

    private Task AwardXpAsync(
        int characterId, int skillId, long xp, CancellationToken cancellationToken) =>
        SkillAwards.AwardAsync(_database, characterId, skillId, xp, cancellationToken);

    private async Task<Character> LockCharacterAsync(
        int characterId, CancellationToken cancellationToken)
    {
        List<Character> locked = await _database.Characters
            .FromSqlInterpolated($"SELECT * FROM characters WHERE id = {characterId} FOR UPDATE")
            .ToListAsync(cancellationToken);

        return locked.Count == 1
            ? locked[0]
            : throw new InvalidOperationException($"Character {characterId} does not exist.");
    }
}
