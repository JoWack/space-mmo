using SpaceMMO.Domain.Items;
using SpaceMMO.Domain.Loss;
using Xunit;

namespace SpaceMMO.Domain.Tests.Loss;

/// <summary>
/// Tests for the destruction rule table (ADR-0006).
/// </summary>
/// <remarks>
/// This table is the primary calibration surface for the material economy, so the
/// validation it performs at construction is a design safety net rather than mere
/// defensiveness — it caught the first draft assigning stackable components a damage
/// chance that the storage model cannot represent.
/// </remarks>
public sealed class DeathRuleTableTests
{
    private static readonly DeathCause[] AllCauses = Enum.GetValues<DeathCause>();
    private static readonly ItemCategory[] AllCategories = Enum.GetValues<ItemCategory>();

    /// <summary>Builds a complete, valid weight set so tests can perturb one entry.</summary>
    private static Dictionary<(DeathCause, ItemCategory), OutcomeWeights> ValidWeights()
    {
        var weights = new Dictionary<(DeathCause, ItemCategory), OutcomeWeights>();

        foreach (DeathCause cause in AllCauses)
        {
            foreach (ItemCategory category in AllCategories)
            {
                weights[(cause, category)] = OutcomeWeights.AlwaysSurvives;
            }
        }

        return weights;
    }

    // ── OutcomeWeights ───────────────────────────────────────────────────────

    [Theory]
    [InlineData(10, 0, 90)]
    [InlineData(0, 70, 30)]
    [InlineData(100, 0, 0)]
    [InlineData(0, 0, 100)]
    public void OutcomeWeights_SummingToOneHundred_IsAccepted(int survived, int damaged, int destroyed)
    {
        var weights = new OutcomeWeights(survived, damaged, destroyed);

        Assert.Equal(survived, weights.SurvivedPercent);
        Assert.Equal(damaged, weights.DamagedPercent);
        Assert.Equal(destroyed, weights.DestroyedPercent);
    }

    [Theory]
    [InlineData(10, 0, 80)]   // sums to 90
    [InlineData(50, 50, 50)]  // sums to 150
    [InlineData(0, 0, 0)]
    public void OutcomeWeights_NotSummingToOneHundred_Throws(int survived, int damaged, int destroyed)
    {
        // A triple that does not sum to 100 would leave part of a stack unaccounted for,
        // which is a material leak or a dupe depending on which way it errs.
        Assert.Throws<ArgumentException>(() => new OutcomeWeights(survived, damaged, destroyed));
    }

    [Fact]
    public void OutcomeWeights_WithNegativeComponent_Throws()
    {
        Assert.Throws<ArgumentException>(() => new OutcomeWeights(110, -10, 0));
    }

    [Theory]
    [InlineData(0, LootOutcome.Survived)]
    [InlineData(9, LootOutcome.Survived)]
    [InlineData(10, LootOutcome.Damaged)]
    [InlineData(39, LootOutcome.Damaged)]
    [InlineData(40, LootOutcome.Destroyed)]
    [InlineData(99, LootOutcome.Destroyed)]
    public void OutcomeWeights_Resolve_PartitionsTheRollRange(int roll, LootOutcome expected)
    {
        // 10 / 30 / 60 — boundaries are inclusive-low, exclusive-high.
        var weights = new OutcomeWeights(10, 30, 60);

        Assert.Equal(expected, weights.Resolve(roll));
    }

    [Theory]
    [InlineData(-1)]
    [InlineData(100)]
    public void OutcomeWeights_Resolve_OutsideRollRange_Throws(int roll)
    {
        Assert.Throws<ArgumentOutOfRangeException>(() => OutcomeWeights.AlwaysSurvives.Resolve(roll));
    }

    [Fact]
    public void OutcomeWeights_Resolve_CoversEveryRollExactlyOnce()
    {
        // Every roll in [0,100) must map somewhere, and the counts must match the weights.
        var weights = new OutcomeWeights(10, 30, 60);
        var counts = new Dictionary<LootOutcome, int>();

        for (int roll = 0; roll < 100; roll++)
        {
            LootOutcome outcome = weights.Resolve(roll);
            counts[outcome] = counts.GetValueOrDefault(outcome) + 1;
        }

        Assert.Equal(10, counts[LootOutcome.Survived]);
        Assert.Equal(30, counts[LootOutcome.Damaged]);
        Assert.Equal(60, counts[LootOutcome.Destroyed]);
    }

    // ── Table construction ───────────────────────────────────────────────────

    [Fact]
    public void Table_WithAMissingPair_Throws()
    {
        // A missing entry would silently default to some wrong economic outcome, so the
        // table demands every pair explicitly.
        var weights = ValidWeights();
        weights.Remove((DeathCause.ShipExplosion, ItemCategory.Hull));

        ArgumentException error = Assert.Throws<ArgumentException>(() => new DeathRuleTable(weights));
        Assert.Contains("missing an entry", error.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void Table_AssigningDamageToAStackableCategory_Throws()
    {
        // The invariant that caught the real design error: stackable items are stored as
        // (item_def, qty) pairs with nowhere to record condition.
        var weights = ValidWeights();
        weights[(DeathCause.ShipExplosion, ItemCategory.Component)] = new OutcomeWeights(5, 10, 85);

        ArgumentException error = Assert.Throws<ArgumentException>(() => new DeathRuleTable(weights));
        Assert.Contains("does not track condition", error.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void Table_AssigningDamageToACategoryWithCondition_IsAccepted()
    {
        var weights = ValidWeights();
        weights[(DeathCause.ShipExplosion, ItemCategory.Armor)] = new OutcomeWeights(5, 15, 80);

        var table = new DeathRuleTable(weights);

        Assert.Equal(15, table.For(DeathCause.ShipExplosion, ItemCategory.Armor).DamagedPercent);
    }

    // ── The default table ────────────────────────────────────────────────────

    [Fact]
    public void DefaultTable_CoversEveryPair()
    {
        foreach (DeathCause cause in AllCauses)
        {
            foreach (ItemCategory category in AllCategories)
            {
                OutcomeWeights weights = DeathRuleTable.Default.For(cause, category);

                Assert.Equal(
                    100,
                    weights.SurvivedPercent + weights.DamagedPercent + weights.DestroyedPercent);
            }
        }
    }

    [Fact]
    public void DefaultTable_ExplosionIsTheHeaviestSink()
    {
        // Explosion must destroy more than any other cause for every category, or the
        // "disable to loot" fork collapses.
        foreach (ItemCategory category in AllCategories)
        {
            int exploded = DeathRuleTable.Default
                .For(DeathCause.ShipExplosion, category).DestroyedPercent;

            int disabled = DeathRuleTable.Default
                .For(DeathCause.ShipDisabled, category).DestroyedPercent;

            Assert.True(
                exploded >= disabled,
                $"{category}: explosion destroys {exploded}% but disabling destroys {disabled}%.");
        }
    }

    [Fact]
    public void DefaultTable_StationDeathsLoseNothing()
    {
        foreach (ItemCategory category in AllCategories)
        {
            Assert.Equal(
                OutcomeWeights.AlwaysSurvives,
                DeathRuleTable.Default.For(DeathCause.PersonalCombatStation, category));
        }
    }

    [Fact]
    public void DefaultTable_SelfDestructDestroysEverything()
    {
        foreach (ItemCategory category in AllCategories)
        {
            Assert.Equal(
                OutcomeWeights.AlwaysDestroyed,
                DeathRuleTable.Default.For(DeathCause.SelfDestruct, category));
        }
    }

    [Fact]
    public void DefaultTable_ArmorNeverSurvivesOnFootPlanetaryCombat()
    {
        OutcomeWeights weights = DeathRuleTable.Default
            .For(DeathCause.PersonalCombatPlanet, ItemCategory.Armor);

        Assert.Equal(0, weights.SurvivedPercent);
        Assert.Equal(70, weights.DamagedPercent);
        Assert.Equal(30, weights.DestroyedPercent);
    }

    [Fact]
    public void DefaultTable_ExplodingHullIsAlwaysDestroyed()
    {
        Assert.Equal(
            OutcomeWeights.AlwaysDestroyed,
            DeathRuleTable.Default.For(DeathCause.ShipExplosion, ItemCategory.Hull));
    }
}
