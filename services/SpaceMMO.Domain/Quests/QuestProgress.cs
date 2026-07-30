namespace SpaceMMO.Domain.Quests;

/// <summary>
/// Something a character did that a quest step might care about.
/// </summary>
/// <param name="Type">What kind of action it was.</param>
/// <param name="TargetKey">What it was done to — an item, body, or station key.</param>
/// <param name="Quantity">How many. One for travel, dock, and talk.</param>
/// <remarks>
/// Emitted by whoever performed the action, not by the quest system reaching into other services.
/// The gathering and industry services return what happened; the caller forwards it here. That
/// keeps the domain services independent of quests entirely — nothing about mining should need to
/// know quests exist.
/// </remarks>
public readonly record struct ObjectiveEvent(ObjectiveType Type, string TargetKey, int Quantity);

/// <summary>One step's requirements, as authored in content.</summary>
/// <param name="Ordinal">Position in the chain, starting at 1.</param>
/// <param name="Type">The action that satisfies it.</param>
/// <param name="TargetKey">What the action must target.</param>
/// <param name="RequiredQuantity">How many are needed.</param>
public readonly record struct StepDefinition(
    int Ordinal,
    ObjectiveType Type,
    string TargetKey,
    int RequiredQuantity);

/// <summary>What an event did to a step.</summary>
/// <param name="NewProgress">Progress after applying the event.</param>
/// <param name="ProgressApplied">How much of the event actually counted.</param>
/// <param name="StepCompleted">True if the step is now satisfied.</param>
public readonly record struct ProgressOutcome(
    int NewProgress,
    int ProgressApplied,
    bool StepCompleted)
{
    /// <summary>True if the event changed nothing.</summary>
    public bool NoChange => ProgressApplied == 0;
}

/// <summary>
/// Advances quest steps, per design-bible §4.
/// </summary>
/// <remarks>
/// Pure, so quest chains can be tested without a database or a client — which matters because the
/// onboarding chain is content that will be rewritten many times, and the engine underneath it
/// should not need re-verifying each time.
/// </remarks>
public static class QuestProgress
{
    /// <summary>
    /// True if an event satisfies the kind of action a step is asking for.
    /// </summary>
    /// <remarks>
    /// Target keys compare ordinally and case-sensitively. They are content identifiers rather
    /// than display text, and a step silently failing to match because of casing would be a
    /// miserable content bug to track down — better that it never matches at all and is noticed
    /// immediately.
    /// </remarks>
    public static bool Matches(StepDefinition step, ObjectiveEvent objectiveEvent) =>
        step.Type == objectiveEvent.Type
        && string.Equals(step.TargetKey, objectiveEvent.TargetKey, StringComparison.Ordinal);

    /// <summary>
    /// Applies an event to a step's current progress.
    /// </summary>
    /// <remarks>
    /// <para>
    /// Progress is capped at what the step requires, and the surplus is discarded rather than
    /// carried into the next step. Steps ask for different things — mining twenty ore should never
    /// pre-complete a step about docking — so carrying over would be meaningless at best and would
    /// skip authored content at worst.
    /// </para>
    /// <para>
    /// Already-complete steps absorb nothing, so a duplicate event cannot push progress past the
    /// requirement.
    /// </para>
    /// </remarks>
    /// <exception cref="ArgumentOutOfRangeException">
    /// If current progress is negative, or the event quantity is not positive.
    /// </exception>
    public static ProgressOutcome Apply(
        StepDefinition step, int currentProgress, ObjectiveEvent objectiveEvent)
    {
        if (currentProgress < 0)
        {
            throw new ArgumentOutOfRangeException(
                nameof(currentProgress), currentProgress, "Progress cannot be negative.");
        }

        if (objectiveEvent.Quantity <= 0)
        {
            throw new ArgumentOutOfRangeException(
                nameof(objectiveEvent), objectiveEvent.Quantity, "Event quantity must be positive.");
        }

        if (!Matches(step, objectiveEvent))
        {
            return new ProgressOutcome(currentProgress, 0, currentProgress >= step.RequiredQuantity);
        }

        int newProgress = Math.Min(currentProgress + objectiveEvent.Quantity, step.RequiredQuantity);

        return new ProgressOutcome(
            NewProgress: newProgress,
            ProgressApplied: newProgress - currentProgress,
            StepCompleted: newProgress >= step.RequiredQuantity);
    }

    /// <summary>
    /// True if a step is already satisfied at the given progress.
    /// </summary>
    public static bool IsComplete(StepDefinition step, int currentProgress) =>
        currentProgress >= step.RequiredQuantity;
}
