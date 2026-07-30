using SpaceMMO.Domain.Economy;
using SpaceMMO.Domain.Quests;

namespace SpaceMMO.Data.Entities;

/// <summary>
/// A quest definition, loaded from <c>data/quests/</c>.
/// </summary>
public class QuestDef
{
    public int Id { get; set; }

    /// <summary>Stable key, e.g. <c>intro_gather_scrap</c>.</summary>
    public required string Key { get; set; }

    public required string Name { get; set; }

    public QuestKind Kind { get; set; }

    /// <summary>Quest that must be completed first, or null if this one is available immediately.</summary>
    public int? PrerequisiteQuestDefId { get; set; }

    public QuestDef? PrerequisiteQuestDef { get; set; }

    /// <summary>Career this quest belongs to, for <see cref="QuestKind.Career"/> chains.</summary>
    public string? CareerKey { get; set; }

    /// <summary>
    /// Credit reward. Routed through the daily faucet cap for
    /// <see cref="QuestKind.Sidequest"/>; one-shot and uncapped for the main story chain.
    /// </summary>
    public Credits RewardCredits { get; set; }

    /// <summary>Skill the XP reward applies to, or null if the quest awards no XP.</summary>
    public int? RewardSkillId { get; set; }

    public Skill? RewardSkill { get; set; }

    public long RewardXp { get; set; }

    /// <summary>
    /// Cooldown before a character may retake this quest. Null means one-shot.
    /// </summary>
    /// <remarks>
    /// The per-quest limiter, which stops one optimal quest being farmed in a tight loop. The
    /// daily credit cap is the separate, aggregate limiter (economy-design §2b).
    /// </remarks>
    public int? CooldownSeconds { get; set; }

    public ICollection<QuestStep> Steps { get; } = [];
}

/// <summary>One objective within a quest, completed in order.</summary>
public class QuestStep
{
    public int Id { get; set; }

    public int QuestDefId { get; set; }

    public QuestDef? QuestDef { get; set; }

    /// <summary>Position in the chain, starting at 1.</summary>
    public int Ordinal { get; set; }

    public required string Description { get; set; }

    public ObjectiveType ObjectiveType { get; set; }

    /// <summary>
    /// What the objective refers to — an item key, body key, or station key depending on
    /// <see cref="ObjectiveType"/>. Resolved by the quest engine, not a foreign key, because
    /// the target table varies.
    /// </summary>
    public required string TargetKey { get; set; }

    /// <summary>How many are required. One for travel, dock, and talk objectives.</summary>
    public int Quantity { get; set; }
}

/// <summary>A character's progress through one quest.</summary>
public class CharacterQuest
{
    public long Id { get; set; }

    public int CharacterId { get; set; }

    public Character? Character { get; set; }

    public int QuestDefId { get; set; }

    public QuestDef? QuestDef { get; set; }

    public QuestState State { get; set; }

    /// <summary>Which step is active. Advanced only by server-side validation.</summary>
    public int StepOrdinal { get; set; }

    /// <summary>Progress toward the current step's required quantity.</summary>
    public int StepProgress { get; set; }

    public DateTimeOffset StartedAt { get; set; }

    public DateTimeOffset? CompletedAt { get; set; }
}

/// <summary>
/// A player-funded bounty on another player.
/// </summary>
/// <remarks>
/// The amount is escrowed from the poster when posted, so a bounty is a credit
/// <em>transfer</em> and never a faucet. The criminal-flagging system that makes bounties
/// meaningful is M4 work; the table exists now so the schema does not need changing then.
/// </remarks>
public class Bounty
{
    public long Id { get; set; }

    public int TargetCharacterId { get; set; }

    public Character? TargetCharacter { get; set; }

    public int PosterCharacterId { get; set; }

    public Credits Amount { get; set; }

    public DateTimeOffset PostedAt { get; set; }

    /// <summary>Who collected it. Null while outstanding.</summary>
    public int? ClaimedByCharacterId { get; set; }

    public DateTimeOffset? ClaimedAt { get; set; }

    /// <summary>The death that satisfied this bounty.</summary>
    public long? ClaimedByDeathRecordId { get; set; }
}
