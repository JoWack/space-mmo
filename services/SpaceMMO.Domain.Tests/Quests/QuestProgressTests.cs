using SpaceMMO.Domain.Quests;
using Xunit;

namespace SpaceMMO.Domain.Tests.Quests;

/// <summary>
/// Tests for quest step advancement (design-bible §4).
/// </summary>
public sealed class QuestProgressTests
{
    private static StepDefinition GatherScrap(int required = 10) =>
        new(1, ObjectiveType.Gather, "scrap_alloy", required);

    private static ObjectiveEvent Gathered(string key = "scrap_alloy", int quantity = 1) =>
        new(ObjectiveType.Gather, key, quantity);

    // ── Matching ─────────────────────────────────────────────────────────────

    [Fact]
    public void Matches_WhenTypeAndTargetAgree()
    {
        Assert.True(QuestProgress.Matches(GatherScrap(), Gathered()));
    }

    [Fact]
    public void DoesNotMatch_ADifferentTarget()
    {
        Assert.False(QuestProgress.Matches(GatherScrap(), Gathered("ferrite_ore")));
    }

    [Fact]
    public void DoesNotMatch_ADifferentObjectiveType()
    {
        // Crafting scrap must not advance a step about gathering it.
        Assert.False(QuestProgress.Matches(
            GatherScrap(), new ObjectiveEvent(ObjectiveType.Craft, "scrap_alloy", 1)));
    }

    [Fact]
    public void Matching_IsCaseSensitive()
    {
        // Target keys are content identifiers, not display text. A step silently failing to match
        // on casing would be a miserable content bug; never matching is noticed immediately.
        Assert.False(QuestProgress.Matches(GatherScrap(), Gathered("Scrap_Alloy")));
    }

    // ── Applying progress ────────────────────────────────────────────────────

    [Fact]
    public void Apply_AccumulatesTowardTheRequirement()
    {
        ProgressOutcome outcome = QuestProgress.Apply(GatherScrap(), 3, Gathered(quantity: 4));

        Assert.Equal(7, outcome.NewProgress);
        Assert.Equal(4, outcome.ProgressApplied);
        Assert.False(outcome.StepCompleted);
    }

    [Fact]
    public void Apply_CompletesTheStepOnReachingTheRequirement()
    {
        ProgressOutcome outcome = QuestProgress.Apply(GatherScrap(), 9, Gathered());

        Assert.Equal(10, outcome.NewProgress);
        Assert.True(outcome.StepCompleted);
    }

    [Fact]
    public void Apply_DiscardsSurplusRatherThanCarryingIt()
    {
        // Steps ask for different things — mining twenty ore should never pre-complete a step
        // about docking — so surplus would be meaningless at best and skip authored content at
        // worst.
        ProgressOutcome outcome = QuestProgress.Apply(GatherScrap(), 0, Gathered(quantity: 100));

        Assert.Equal(10, outcome.NewProgress);
        Assert.Equal(10, outcome.ProgressApplied);
        Assert.True(outcome.StepCompleted);
    }

    [Fact]
    public void Apply_ToAnAlreadyCompleteStep_AbsorbsNothing()
    {
        // A duplicate event must not push progress past the requirement.
        ProgressOutcome outcome = QuestProgress.Apply(GatherScrap(), 10, Gathered(quantity: 5));

        Assert.Equal(10, outcome.NewProgress);
        Assert.Equal(0, outcome.ProgressApplied);
        Assert.True(outcome.StepCompleted);
    }

    [Fact]
    public void Apply_ANonMatchingEvent_ChangesNothing()
    {
        ProgressOutcome outcome = QuestProgress.Apply(GatherScrap(), 4, Gathered("ferrite_ore", 5));

        Assert.Equal(4, outcome.NewProgress);
        Assert.True(outcome.NoChange);
        Assert.False(outcome.StepCompleted);
    }

    [Fact]
    public void Apply_NeverExceedsTheRequirement()
    {
        // Swept: progress running past the requirement would let a step complete twice and pay
        // its reward twice.
        foreach (int required in new[] { 1, 5, 20, 100 })
        {
            foreach (int current in new[] { 0, 1, required - 1, required })
            {
                foreach (int quantity in new[] { 1, 3, 1_000 })
                {
                    ProgressOutcome outcome = QuestProgress.Apply(
                        GatherScrap(required), Math.Max(0, current), Gathered(quantity: quantity));

                    Assert.InRange(outcome.NewProgress, 0, required);
                }
            }
        }
    }

    [Fact]
    public void Apply_IsMonotonic()
    {
        // Progress must never go backwards, or a step could un-complete itself.
        int progress = 0;

        for (int i = 0; i < 20; i++)
        {
            ProgressOutcome outcome = QuestProgress.Apply(GatherScrap(), progress, Gathered());

            Assert.True(outcome.NewProgress >= progress);
            progress = outcome.NewProgress;
        }

        Assert.Equal(10, progress);
    }

    [Fact]
    public void Apply_ToASingleUnitStep_CompletesImmediately()
    {
        // Travel, dock, and talk objectives all require exactly one.
        var dock = new StepDefinition(7, ObjectiveType.Dock, "station_capital", 1);

        ProgressOutcome outcome = QuestProgress.Apply(
            dock, 0, new ObjectiveEvent(ObjectiveType.Dock, "station_capital", 1));

        Assert.True(outcome.StepCompleted);
    }

    // ── Validation ───────────────────────────────────────────────────────────

    [Fact]
    public void Apply_WithNegativeProgress_Throws()
    {
        Assert.Throws<ArgumentOutOfRangeException>(
            () => QuestProgress.Apply(GatherScrap(), -1, Gathered()));
    }

    [Theory]
    [InlineData(0)]
    [InlineData(-5)]
    public void Apply_WithNonPositiveEventQuantity_Throws(int quantity)
    {
        Assert.Throws<ArgumentOutOfRangeException>(
            () => QuestProgress.Apply(GatherScrap(), 0, Gathered(quantity: quantity)));
    }

    [Fact]
    public void IsComplete_MatchesTheRequirement()
    {
        Assert.False(QuestProgress.IsComplete(GatherScrap(), 9));
        Assert.True(QuestProgress.IsComplete(GatherScrap(), 10));
        Assert.True(QuestProgress.IsComplete(GatherScrap(), 11));
    }
}
