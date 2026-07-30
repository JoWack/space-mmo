using SpaceMMO.Domain.Items;

namespace SpaceMMO.Domain.Loss;

/// <summary>
/// The probability split for one <c>(cause, category)</c> pair, in whole percent.
/// </summary>
public readonly record struct OutcomeWeights
{
    /// <summary>Chance the item survives intact, in whole percent.</summary>
    public int SurvivedPercent { get; }

    /// <summary>Chance the item survives damaged, in whole percent.</summary>
    public int DamagedPercent { get; }

    /// <summary>Chance the item is destroyed, in whole percent.</summary>
    public int DestroyedPercent { get; }

    /// <summary>
    /// Creates a weight triple. The three values must sum to exactly 100.
    /// </summary>
    /// <exception cref="ArgumentException">If the weights do not sum to 100, or any is negative.</exception>
    public OutcomeWeights(int survivedPercent, int damagedPercent, int destroyedPercent)
    {
        if (survivedPercent < 0 || damagedPercent < 0 || destroyedPercent < 0)
        {
            throw new ArgumentException(
                $"Weights cannot be negative: {survivedPercent}/{damagedPercent}/{destroyedPercent}.");
        }

        int total = survivedPercent + damagedPercent + destroyedPercent;
        if (total != 100)
        {
            throw new ArgumentException(
                $"Weights must sum to 100, got {total} " +
                $"({survivedPercent}/{damagedPercent}/{destroyedPercent}).");
        }

        SurvivedPercent = survivedPercent;
        DamagedPercent = damagedPercent;
        DestroyedPercent = destroyedPercent;
    }

    /// <summary>Everything survives intact.</summary>
    public static OutcomeWeights AlwaysSurvives => new(100, 0, 0);

    /// <summary>Everything is destroyed.</summary>
    public static OutcomeWeights AlwaysDestroyed => new(0, 0, 100);

    /// <summary>
    /// Maps a roll in <c>[0, 100)</c> to an outcome. Ordering is survived, then damaged,
    /// then destroyed.
    /// </summary>
    public LootOutcome Resolve(int roll)
    {
        if (roll is < 0 or >= 100)
        {
            throw new ArgumentOutOfRangeException(nameof(roll), roll, "Roll must be in [0, 100).");
        }

        if (roll < SurvivedPercent)
        {
            return LootOutcome.Survived;
        }

        return roll < SurvivedPercent + DamagedPercent
            ? LootOutcome.Damaged
            : LootOutcome.Destroyed;
    }
}

/// <summary>
/// Maps <c>(death cause, item category)</c> to outcome probabilities, per ADR-0006.
/// </summary>
/// <remarks>
/// <para>
/// This table is the primary calibration surface for the entire material economy — these
/// numbers set how fast materials leave the game. <see cref="Default"/> is the
/// first-draft table from economy-design §3b; loading a tuned table from
/// <c>data/death-rules.json</c> belongs in the data layer, not here, because
/// <c>SpaceMMO.Domain</c> does no I/O.
/// </para>
/// <para>
/// Construction validates two invariants: every triple sums to 100, and categories that
/// do not track condition cannot have a nonzero <c>damaged</c> weight. The second one is
/// load-bearing — it caught a genuine inconsistency in the first-draft table, where
/// stackable components were assigned a 10% damage chance that the storage model cannot
/// represent.
/// </para>
/// </remarks>
public sealed class DeathRuleTable
{
    private static readonly ItemCategory[] AllCategories = Enum.GetValues<ItemCategory>();
    private static readonly DeathCause[] AllCauses = Enum.GetValues<DeathCause>();

    private readonly Dictionary<(DeathCause Cause, ItemCategory Category), OutcomeWeights> _weights;

    /// <summary>
    /// Creates a table from a complete set of weights.
    /// </summary>
    /// <exception cref="ArgumentException">
    /// If any <c>(cause, category)</c> pair is missing, or a non-condition category has a
    /// nonzero damaged weight.
    /// </exception>
    public DeathRuleTable(
        IReadOnlyDictionary<(DeathCause Cause, ItemCategory Category), OutcomeWeights> weights)
    {
        ArgumentNullException.ThrowIfNull(weights);

        _weights = new Dictionary<(DeathCause, ItemCategory), OutcomeWeights>(weights.Count);

        foreach (DeathCause cause in AllCauses)
        {
            foreach (ItemCategory category in AllCategories)
            {
                if (!weights.TryGetValue((cause, category), out OutcomeWeights entry))
                {
                    throw new ArgumentException(
                        $"Death rule table is missing an entry for ({cause}, {category}). " +
                        "Every pair must be specified explicitly; a missing entry would " +
                        "silently default to a wrong economic outcome.");
                }

                if (!category.HasCondition() && entry.DamagedPercent != 0)
                {
                    throw new ArgumentException(
                        $"({cause}, {category}) has a damaged weight of {entry.DamagedPercent}%, " +
                        $"but {category} does not track condition, so a damaged outcome cannot " +
                        "be represented. Fold it into survived or destroyed.");
                }

                _weights[(cause, category)] = entry;
            }
        }
    }

    /// <summary>Weights for one pair.</summary>
    public OutcomeWeights For(DeathCause cause, ItemCategory category) => _weights[(cause, category)];

    /// <summary>
    /// The first-draft table from economy-design §3b.
    /// </summary>
    /// <remarks>
    /// Reading the design intent out of the numbers: explosion destroys 80–95% of
    /// everything; disabling preserves most cargo; armor never survives on-foot
    /// planetary combat intact; station deaths lose nothing; a parked ship is unaffected
    /// by its owner dying on foot.
    /// </remarks>
    public static DeathRuleTable Default { get; } = BuildDefault();

    private static DeathRuleTable BuildDefault()
    {
        var w = new Dictionary<(DeathCause, ItemCategory), OutcomeWeights>();

        // ── Ship explosion: the heavy sink. Your cargo was inside the detonation. ──
        w[(DeathCause.ShipExplosion, ItemCategory.Raw)] = new(10, 0, 90);
        w[(DeathCause.ShipExplosion, ItemCategory.Refined)] = new(10, 0, 90);
        w[(DeathCause.ShipExplosion, ItemCategory.Component)] = new(5, 0, 95);
        w[(DeathCause.ShipExplosion, ItemCategory.Consumable)] = new(5, 0, 95);
        w[(DeathCause.ShipExplosion, ItemCategory.Tool)] = new(10, 10, 80);
        w[(DeathCause.ShipExplosion, ItemCategory.Module)] = new(5, 15, 80);
        w[(DeathCause.ShipExplosion, ItemCategory.Armor)] = new(5, 15, 80);
        w[(DeathCause.ShipExplosion, ItemCategory.Weapon)] = new(5, 10, 85);
        w[(DeathCause.ShipExplosion, ItemCategory.Hull)] = OutcomeWeights.AlwaysDestroyed;

        // ── Ship disabled: salvageable. The tactical fork — disable to loot. ──
        w[(DeathCause.ShipDisabled, ItemCategory.Raw)] = new(70, 0, 30);
        w[(DeathCause.ShipDisabled, ItemCategory.Refined)] = new(70, 0, 30);
        w[(DeathCause.ShipDisabled, ItemCategory.Component)] = new(60, 0, 40);
        w[(DeathCause.ShipDisabled, ItemCategory.Consumable)] = new(50, 0, 50);
        w[(DeathCause.ShipDisabled, ItemCategory.Tool)] = new(50, 30, 20);
        w[(DeathCause.ShipDisabled, ItemCategory.Module)] = new(40, 40, 20);
        w[(DeathCause.ShipDisabled, ItemCategory.Armor)] = new(40, 35, 25);
        w[(DeathCause.ShipDisabled, ItemCategory.Weapon)] = new(45, 35, 20);
        // The wreck exists but the frame is never flyable again without a rebuild.
        w[(DeathCause.ShipDisabled, ItemCategory.Hull)] = new(0, 100, 0);

        // ── On-foot planetary combat: armor takes the punishment. ──
        w[(DeathCause.PersonalCombatPlanet, ItemCategory.Raw)] = new(90, 0, 10);
        w[(DeathCause.PersonalCombatPlanet, ItemCategory.Refined)] = new(90, 0, 10);
        w[(DeathCause.PersonalCombatPlanet, ItemCategory.Component)] = new(90, 0, 10);
        w[(DeathCause.PersonalCombatPlanet, ItemCategory.Consumable)] = new(80, 0, 20);
        w[(DeathCause.PersonalCombatPlanet, ItemCategory.Tool)] = new(70, 25, 5);
        w[(DeathCause.PersonalCombatPlanet, ItemCategory.Module)] = new(80, 15, 5);
        w[(DeathCause.PersonalCombatPlanet, ItemCategory.Armor)] = new(0, 70, 30);
        w[(DeathCause.PersonalCombatPlanet, ItemCategory.Weapon)] = new(60, 30, 10);
        // A ship parked in orbit is unaffected by its owner being shot on the surface.
        w[(DeathCause.PersonalCombatPlanet, ItemCategory.Hull)] = OutcomeWeights.AlwaysSurvives;

        // ── Stations are safe zones. Nothing is lost, ever. ──
        foreach (ItemCategory category in AllCategories)
        {
            w[(DeathCause.PersonalCombatStation, category)] = OutcomeWeights.AlwaysSurvives;
        }

        // ── Environment: gentle on cargo, hard on your suit. ──
        w[(DeathCause.EnvironmentalPlanet, ItemCategory.Raw)] = new(95, 0, 5);
        w[(DeathCause.EnvironmentalPlanet, ItemCategory.Refined)] = new(95, 0, 5);
        w[(DeathCause.EnvironmentalPlanet, ItemCategory.Component)] = new(95, 0, 5);
        w[(DeathCause.EnvironmentalPlanet, ItemCategory.Consumable)] = new(85, 0, 15);
        w[(DeathCause.EnvironmentalPlanet, ItemCategory.Tool)] = new(60, 35, 5);
        w[(DeathCause.EnvironmentalPlanet, ItemCategory.Module)] = new(85, 15, 0);
        w[(DeathCause.EnvironmentalPlanet, ItemCategory.Armor)] = new(10, 60, 30);
        w[(DeathCause.EnvironmentalPlanet, ItemCategory.Weapon)] = new(70, 25, 5);
        w[(DeathCause.EnvironmentalPlanet, ItemCategory.Hull)] = OutcomeWeights.AlwaysSurvives;

        // ── Self-destruct: nothing survives. Removes the zero-risk insurance path. ──
        foreach (ItemCategory category in AllCategories)
        {
            w[(DeathCause.SelfDestruct, category)] = OutcomeWeights.AlwaysDestroyed;
        }

        return new DeathRuleTable(w);
    }
}
