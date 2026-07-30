using Microsoft.EntityFrameworkCore;
using SpaceMMO.Data.Entities;
using SpaceMMO.Domain.Economy;
using SpaceMMO.Domain.Quests;

namespace SpaceMMO.Data.Quests;

/// <summary>Thrown when a quest's prerequisite has not been completed.</summary>
public sealed class QuestLockedException(string questKey, string prerequisiteKey)
    : InvalidOperationException($"'{questKey}' requires '{prerequisiteKey}' to be completed first.")
{
    public string QuestKey { get; } = questKey;

    public string PrerequisiteKey { get; } = prerequisiteKey;
}

/// <summary>Thrown when a repeatable quest is retaken before its cooldown has elapsed.</summary>
public sealed class QuestOnCooldownException(string questKey, DateTimeOffset availableAt)
    : InvalidOperationException($"'{questKey}' can be taken again at {availableAt:O}.")
{
    public string QuestKey { get; } = questKey;

    public DateTimeOffset AvailableAt { get; } = availableAt;
}

/// <summary>What recording an objective event did.</summary>
/// <param name="QuestsAdvanced">Quests whose current step took progress.</param>
/// <param name="QuestsCompleted">Quests finished by this event.</param>
/// <param name="CreditsGranted">Credits actually paid, after any daily cap.</param>
/// <param name="CreditsWithheld">Credits refused because the daily cap was reached.</param>
public readonly record struct RecordProgressResult(
    IReadOnlyList<long> QuestsAdvanced,
    IReadOnlyList<long> QuestsCompleted,
    Credits CreditsGranted,
    Credits CreditsWithheld);

/// <summary>
/// Tracks quest progress and grants rewards, per design-bible §4 and §9.
/// </summary>
/// <remarks>
/// <para>
/// <strong>Nothing calls into this from inside another domain service.</strong> Gathering and
/// industry return what happened; the caller forwards it here. Mining has no reason to know
/// quests exist, and coupling them would mean every new activity had to remember to notify the
/// quest system.
/// </para>
/// <para>
/// <strong>Completion is decided server-side.</strong> The client renders a journal and never
/// asserts that a step is done.
/// </para>
/// </remarks>
public sealed class QuestService(SpaceMmoDbContext database)
{
    private readonly SpaceMmoDbContext _database =
        database ?? throw new ArgumentNullException(nameof(database));

    /// <summary>
    /// Accepts a quest, checking its prerequisite and any repeat cooldown.
    /// </summary>
    /// <exception cref="QuestLockedException">If the prerequisite is not complete.</exception>
    /// <exception cref="QuestOnCooldownException">If a repeatable quest was taken too recently.</exception>
    /// <exception cref="InvalidOperationException">
    /// If the quest is already in progress, or is one-shot and already completed.
    /// </exception>
    public async Task<long> AcceptAsync(
        int characterId, string questKey, CancellationToken cancellationToken = default)
    {
        await using var transaction =
            await _database.Database.BeginTransactionAsync(cancellationToken);

        QuestDef quest = await _database.QuestDefs
            .Include(q => q.PrerequisiteQuestDef)
            .SingleAsync(q => q.Key == questKey, cancellationToken);

        await GuardPrerequisiteAsync(characterId, quest, cancellationToken);

        bool alreadyActive = await _database.CharacterQuests.AnyAsync(
            cq => cq.CharacterId == characterId
                && cq.QuestDefId == quest.Id
                && cq.State == QuestState.InProgress,
            cancellationToken);

        if (alreadyActive)
        {
            throw new InvalidOperationException($"'{questKey}' is already in progress.");
        }

        await GuardRepeatabilityAsync(characterId, quest, cancellationToken);

        var accepted = new CharacterQuest
        {
            CharacterId = characterId,
            QuestDefId = quest.Id,
            State = QuestState.InProgress,
            StepOrdinal = 1,
            StepProgress = 0,
            StartedAt = DateTimeOffset.UtcNow,
        };

        _database.CharacterQuests.Add(accepted);

        await _database.SaveChangesAsync(cancellationToken);
        await transaction.CommitAsync(cancellationToken);

        return accepted.Id;
    }

    /// <summary>
    /// Applies an objective event to every quest the character has in progress.
    /// </summary>
    /// <remarks>
    /// Applied across all active quests rather than just one, because two quests can legitimately
    /// ask for the same action and a player should not have to guess which one their mining counts
    /// toward.
    /// </remarks>
    public async Task<RecordProgressResult> RecordProgressAsync(
        int characterId,
        ObjectiveEvent objectiveEvent,
        CancellationToken cancellationToken = default)
    {
        if (objectiveEvent.Quantity <= 0)
        {
            throw new ArgumentOutOfRangeException(
                nameof(objectiveEvent), objectiveEvent.Quantity, "Event quantity must be positive.");
        }

        await using var transaction =
            await _database.Database.BeginTransactionAsync(cancellationToken);

        // Locked so two concurrent events cannot both advance the same step from the same
        // starting progress and double-count it.
        List<CharacterQuest> active = await _database.CharacterQuests
            .FromSqlInterpolated($"""
                SELECT * FROM character_quests
                WHERE character_id = {characterId} AND state = 'InProgress'
                FOR UPDATE
                """)
            .ToListAsync(cancellationToken);

        var advanced = new List<long>();
        var completed = new List<long>();
        Credits granted = Credits.Zero;
        Credits withheld = Credits.Zero;

        foreach (CharacterQuest characterQuest in active)
        {
            QuestStep? step = await _database.QuestSteps.FirstOrDefaultAsync(
                s => s.QuestDefId == characterQuest.QuestDefId
                    && s.Ordinal == characterQuest.StepOrdinal,
                cancellationToken);

            if (step is null)
            {
                continue;
            }

            var definition = new StepDefinition(
                step.Ordinal, step.ObjectiveType, step.TargetKey, step.Quantity);

            ProgressOutcome outcome = QuestProgress.Apply(
                definition, characterQuest.StepProgress, objectiveEvent);

            if (outcome.NoChange)
            {
                continue;
            }

            characterQuest.StepProgress = outcome.NewProgress;
            advanced.Add(characterQuest.Id);

            if (!outcome.StepCompleted)
            {
                continue;
            }

            bool hasNextStep = await _database.QuestSteps.AnyAsync(
                s => s.QuestDefId == characterQuest.QuestDefId
                    && s.Ordinal == characterQuest.StepOrdinal + 1,
                cancellationToken);

            if (hasNextStep)
            {
                characterQuest.StepOrdinal++;
                characterQuest.StepProgress = 0;
                continue;
            }

            FaucetGrant reward = await CompleteAsync(characterQuest, cancellationToken);

            granted += reward.Granted;
            withheld += reward.Withheld;
            completed.Add(characterQuest.Id);
        }

        await _database.SaveChangesAsync(cancellationToken);
        await transaction.CommitAsync(cancellationToken);

        return new RecordProgressResult(advanced, completed, granted, withheld);
    }

    /// <summary>
    /// Finishes a quest and pays out.
    /// </summary>
    /// <remarks>
    /// XP is granted in full always. Only credits pass through the faucet cap, and only for
    /// repeatable kinds — a capped reward still leaves the quest worth doing for progression,
    /// which is the whole point of withholding credits rather than blocking the content
    /// (economy-design §2b).
    /// </remarks>
    private async Task<FaucetGrant> CompleteAsync(
        CharacterQuest characterQuest, CancellationToken cancellationToken)
    {
        QuestDef quest = await _database.QuestDefs
            .SingleAsync(q => q.Id == characterQuest.QuestDefId, cancellationToken);

        DateTimeOffset now = DateTimeOffset.UtcNow;

        characterQuest.State = QuestState.Completed;
        characterQuest.CompletedAt = now;

        if (quest.RewardSkillId is int skillId && quest.RewardXp > 0)
        {
            await AwardXpAsync(characterQuest.CharacterId, skillId, quest.RewardXp, cancellationToken);
        }

        if (!quest.RewardCredits.IsPositive)
        {
            return new FaucetGrant(Credits.Zero, Credits.Zero);
        }

        LedgerReason reason = quest.Kind == QuestKind.Sidequest
            ? LedgerReason.QuestReward
            : LedgerReason.StoryReward;

        FaucetGrant grant = LedgerReasons.IsCappedFaucet(reason)
            ? await ApplyDailyCapAsync(characterQuest.CharacterId, quest.RewardCredits, now, cancellationToken)
            : new FaucetGrant(quest.RewardCredits, Credits.Zero);

        if (grant.Granted.IsPositive)
        {
            await AdjustBalanceAsync(
                characterQuest.CharacterId, grant.Granted, reason, quest.Id, now, cancellationToken);
        }

        return grant;
    }

    /// <summary>
    /// Runs a credit reward through the per-character daily budget.
    /// </summary>
    /// <remarks>
    /// The single chokepoint every capped faucet routes through, so adding another credit source
    /// later needs no economic rebalancing (economy-design §2b).
    /// </remarks>
    private async Task<FaucetGrant> ApplyDailyCapAsync(
        int characterId, Credits requested, DateTimeOffset now, CancellationToken cancellationToken)
    {
        DateOnly utcDate = DateOnly.FromDateTime(now.UtcDateTime);

        CharacterFaucetDaily? today = await _database.CharacterFaucetDailies.FirstOrDefaultAsync(
            d => d.CharacterId == characterId && d.UtcDate == utcDate, cancellationToken);

        Credits alreadyGranted = today?.CreditsGranted ?? Credits.Zero;

        FaucetGrant grant = FaucetBudget.Evaluate(requested, alreadyGranted);

        if (!grant.Granted.IsPositive)
        {
            return grant;
        }

        if (today is null)
        {
            _database.CharacterFaucetDailies.Add(new CharacterFaucetDaily
            {
                CharacterId = characterId,
                UtcDate = utcDate,
                CreditsGranted = grant.Granted,
            });
        }
        else
        {
            today.CreditsGranted += grant.Granted;
        }

        return grant;
    }

    private async Task GuardPrerequisiteAsync(
        int characterId, QuestDef quest, CancellationToken cancellationToken)
    {
        if (quest.PrerequisiteQuestDefId is not int prerequisiteId)
        {
            return;
        }

        bool done = await _database.CharacterQuests.AnyAsync(
            cq => cq.CharacterId == characterId
                && cq.QuestDefId == prerequisiteId
                && cq.State == QuestState.Completed,
            cancellationToken);

        if (!done)
        {
            throw new QuestLockedException(quest.Key, quest.PrerequisiteQuestDef!.Key);
        }
    }

    /// <summary>
    /// Rejects retaking a one-shot quest, or a repeatable one still on cooldown.
    /// </summary>
    /// <remarks>
    /// The per-quest cooldown is the limiter that stops one optimal sidequest being farmed in a
    /// tight loop. The daily credit cap is the separate, aggregate limiter; both exist because
    /// they do different jobs.
    /// </remarks>
    private async Task GuardRepeatabilityAsync(
        int characterId, QuestDef quest, CancellationToken cancellationToken)
    {
        DateTimeOffset? lastCompleted = await _database.CharacterQuests
            .Where(cq => cq.CharacterId == characterId
                && cq.QuestDefId == quest.Id
                && cq.State == QuestState.Completed)
            .OrderByDescending(cq => cq.CompletedAt)
            .Select(cq => cq.CompletedAt)
            .FirstOrDefaultAsync(cancellationToken);

        if (lastCompleted is not DateTimeOffset completedAt)
        {
            return;
        }

        if (quest.CooldownSeconds is not int cooldown)
        {
            throw new InvalidOperationException($"'{quest.Key}' can only be completed once.");
        }

        DateTimeOffset availableAt = completedAt.AddSeconds(cooldown);

        if (DateTimeOffset.UtcNow < availableAt)
        {
            throw new QuestOnCooldownException(quest.Key, availableAt);
        }
    }

    private async Task AwardXpAsync(
        int characterId, int skillId, long xp, CancellationToken cancellationToken)
    {
        CharacterSkill? skill = await _database.CharacterSkills.FirstOrDefaultAsync(
            s => s.CharacterId == characterId && s.SkillId == skillId, cancellationToken);

        if (skill is null)
        {
            _database.CharacterSkills.Add(new CharacterSkill
            {
                CharacterId = characterId,
                SkillId = skillId,
                Xp = xp,
            });

            return;
        }

        skill.Xp += xp;
    }

    private async Task AdjustBalanceAsync(
        int characterId,
        Credits delta,
        LedgerReason reason,
        long? referenceId,
        DateTimeOffset at,
        CancellationToken cancellationToken)
    {
        long minorUnits = delta.MinorUnits;

        await _database.Database.ExecuteSqlInterpolatedAsync(
            $"UPDATE characters SET balance = balance + {minorUnits} WHERE id = {characterId}",
            cancellationToken);

        _database.LedgerEntries.Add(new LedgerEntry
        {
            CharacterId = characterId,
            DeltaCredits = delta,
            Reason = reason,
            ReferenceId = referenceId,
            CreatedAt = at,
        });
    }
}
