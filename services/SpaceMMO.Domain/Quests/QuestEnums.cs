namespace SpaceMMO.Domain.Quests;

/// <summary>
/// Quest kinds, per design-bible §7.
/// </summary>
public enum QuestKind
{
    /// <summary>
    /// The onboarding chain and later story arcs. One-shot, and the bootstrap credit faucet.
    /// </summary>
    MainStory = 0,

    /// <summary>Career chains unlocked at the capital world. One-shot. Content not yet designed.</summary>
    Career = 1,

    /// <summary>
    /// Repeatable with a cooldown. The steady-state credit faucet, subject to the daily cap.
    /// </summary>
    Sidequest = 2,

    /// <summary>
    /// Player-generated, posted against players who kill in low-security space. Funded by the
    /// poster, so it is a transfer rather than a faucet.
    /// </summary>
    Bounty = 3,
}

/// <summary>
/// A character's progress through one quest.
/// </summary>
/// <remarks>
/// There is no <c>NotStarted</c> member: a quest a character has never taken simply has no
/// row. Representing absence as a state invites rows that exist only to say nothing happened.
/// </remarks>
public enum QuestState
{
    /// <summary>Accepted and being worked on.</summary>
    InProgress = 0,

    /// <summary>Finished and rewards granted.</summary>
    Completed = 1,

    /// <summary>Abandoned by the player. Repeatable quests may be retaken after cooldown.</summary>
    Abandoned = 2,
}

/// <summary>
/// What a quest step asks the player to do, per design-bible §4.
/// </summary>
/// <remarks>
/// This set covers the entire onboarding chain, which is why building that chain first was a
/// good forcing function for the quest engine's design. Every objective is validated
/// server-side; the client never decides that a step is complete.
/// </remarks>
public enum ObjectiveType
{
    /// <summary>Collect a quantity of an item by hand from resource nodes.</summary>
    Gather = 0,

    /// <summary>Produce a quantity of an item via a recipe.</summary>
    Craft = 1,

    /// <summary>Process raw material into a refined form. A craft with a different verb for the player.</summary>
    Refine = 2,

    /// <summary>Reach a named body or system.</summary>
    Travel = 3,

    /// <summary>Dock at a named station.</summary>
    Dock = 4,

    /// <summary>Speak to a quest giver.</summary>
    Talk = 5,
}
