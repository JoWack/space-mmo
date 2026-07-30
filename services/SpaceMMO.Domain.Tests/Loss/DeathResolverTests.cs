using SpaceMMO.Domain.Items;
using SpaceMMO.Domain.Loss;
using Xunit;

namespace SpaceMMO.Domain.Tests.Loss;

/// <summary>
/// Tests for death and loot resolution (ADR-0006).
/// </summary>
public sealed class DeathResolverTests
{
    private static readonly DeathCause[] AllCauses = Enum.GetValues<DeathCause>();
    private static readonly ItemCategory[] AllCategories = Enum.GetValues<ItemCategory>();

    private static DeathItem Ore(int quantity) => new("ferrite_ore", ItemCategory.Raw, quantity);

    private static DeathItem Armor(long instanceId) =>
        new("combat_armor", ItemCategory.Armor, 1, instanceId);

    // ── Determinism ──────────────────────────────────────────────────────────

    [Fact]
    public void Resolve_WithTheSameSeed_IsByteIdentical()
    {
        // The property that makes a player dispute over a lost item replayable from the
        // log rather than a matter of opinion.
        var loadout = new[] { Ore(500), Armor(1), Armor(2), new DeathItem("mining_laser", ItemCategory.Tool, 1, 3) };

        IReadOnlyList<ResolvedItem> first = DeathResolver.Resolve(loadout, DeathCause.ShipExplosion, 12_345UL);
        IReadOnlyList<ResolvedItem> second = DeathResolver.Resolve(loadout, DeathCause.ShipExplosion, 12_345UL);

        Assert.Equal(first, second);
    }

    [Fact]
    public void Resolve_IsIndependentOfLoadoutOrder()
    {
        // Each entry seeds from (seed, key, instance), so the inventory ordering the
        // server happens to produce cannot change any outcome.
        var loadout = new[]
        {
            Ore(500),
            Armor(1),
            Armor(2),
            new DeathItem("plasma_rifle", ItemCategory.Weapon, 1, 3),
        };

        var reversed = loadout.Reverse().ToArray();

        IReadOnlyList<ResolvedItem> forward =
            DeathResolver.Resolve(loadout, DeathCause.ShipDisabled, 777UL);
        IReadOnlyList<ResolvedItem> backward =
            DeathResolver.Resolve(reversed, DeathCause.ShipDisabled, 777UL);

        Assert.Equal(
            forward.OrderBy(r => r.ItemKey).ThenBy(r => r.InstanceId),
            backward.OrderBy(r => r.ItemKey).ThenBy(r => r.InstanceId));
    }

    [Fact]
    public void Resolve_DistinctInstances_AreDecorrelated()
    {
        // Two identical items must not share a roll — losing both or neither, never one.
        // Over many seeds they should agree only about as often as chance allows.
        int agreements = 0;
        const int trials = 2_000;

        for (ulong seed = 0; seed < trials; seed++)
        {
            IReadOnlyList<ResolvedItem> resolved =
                DeathResolver.Resolve([Armor(1), Armor(2)], DeathCause.ShipDisabled, seed);

            if (resolved[0].SurvivedQuantity == resolved[1].SurvivedQuantity
                && resolved[0].DamagedQuantity == resolved[1].DamagedQuantity)
            {
                agreements++;
            }
        }

        // Armor on ShipDisabled is 40/35/25, so chance agreement is ~0.345. A shared
        // random stream would give 100%.
        Assert.InRange(agreements / (double)trials, 0.25, 0.45);
    }

    // ── Conservation ─────────────────────────────────────────────────────────

    [Fact]
    public void Resolve_AlwaysConservesQuantity()
    {
        // Material conservation is EconSim invariant 1. Drift here is a dupe or a leak.
        foreach (DeathCause cause in AllCauses)
        {
            foreach (ItemCategory category in AllCategories)
            {
                int quantity = category.IsStackable() ? 137 : 1;
                var item = new DeathItem($"test_{category}", category, quantity, InstanceId: 1);

                ResolvedItem resolved = DeathResolver.Resolve([item], cause, 42UL)[0];

                Assert.Equal(quantity, resolved.TotalQuantity);
            }
        }
    }

    [Fact]
    public void Resolve_EmptyLoadout_ReturnsEmpty()
    {
        Assert.Empty(DeathResolver.Resolve([], DeathCause.ShipExplosion, 1UL));
    }

    [Fact]
    public void Resolve_NeverDamagesStackableItems()
    {
        // Stackable items have nowhere to record condition, so a damaged outcome would be
        // unrepresentable in storage.
        foreach (DeathCause cause in AllCauses)
        {
            foreach (ItemCategory category in AllCategories.Where(c => c.IsStackable()))
            {
                ResolvedItem resolved = DeathResolver
                    .Resolve([new DeathItem($"test_{category}", category, 100)], cause, 9UL)[0];

                Assert.Equal(0, resolved.DamagedQuantity);
            }
        }
    }

    // ── Stack resolution ─────────────────────────────────────────────────────

    [Theory]
    [InlineData(1_000, 100)]  // 10% of 1,000 survives an explosion
    [InlineData(10, 1)]
    [InlineData(7, 0)]        // floors, and the remainder goes to destroyed
    [InlineData(1, 0)]
    public void Resolve_Stack_SplitsProportionallyWithRemainderDestroyed(
        int quantity, int expectedSurvived)
    {
        // Deterministic by design: per-unit rolls on a 10,000-unit hold would be so swingy
        // that EconSim's material-flow measurements would be dominated by variance.
        ResolvedItem resolved =
            DeathResolver.Resolve([Ore(quantity)], DeathCause.ShipExplosion, 1UL)[0];

        Assert.Equal(expectedSurvived, resolved.SurvivedQuantity);
        Assert.Equal(quantity - expectedSurvived, resolved.DestroyedQuantity);
    }

    [Fact]
    public void Resolve_Stack_IgnoresTheSeedEntirely()
    {
        // Confirms the proportional path is genuinely deterministic rather than
        // accidentally stable for the seeds under test.
        var outcomes = new HashSet<int>();

        for (ulong seed = 0; seed < 100; seed++)
        {
            outcomes.Add(DeathResolver.Resolve([Ore(1_000)], DeathCause.ShipExplosion, seed)[0]
                .SurvivedQuantity);
        }

        Assert.Single(outcomes);
    }

    // ── Design intent, expressed as tests ────────────────────────────────────

    [Fact]
    public void Armor_NeverSurvivesOnFootPlanetaryCombatIntact()
    {
        // The rule that prompted this whole system: armor is the thing being shot, so it
        // always drops broken or destroyed — 0/70/30.
        for (ulong seed = 0; seed < 500; seed++)
        {
            ResolvedItem resolved =
                DeathResolver.Resolve([Armor(1)], DeathCause.PersonalCombatPlanet, seed)[0];

            Assert.Equal(0, resolved.SurvivedQuantity);
            Assert.Equal(1, resolved.DamagedQuantity + resolved.DestroyedQuantity);
        }
    }

    [Fact]
    public void Armor_OnFootPlanetaryCombat_IsDamagedAboutSeventyPercentOfTheTime()
    {
        int damaged = 0;
        const int trials = 4_000;

        for (ulong seed = 0; seed < trials; seed++)
        {
            damaged += DeathResolver
                .Resolve([Armor(1)], DeathCause.PersonalCombatPlanet, seed)[0].DamagedQuantity;
        }

        // Generous bounds — this checks the weights are wired up, not the RNG's quality.
        Assert.InRange(damaged / (double)trials, 0.65, 0.75);
    }

    [Fact]
    public void StationDeath_LosesNothing()
    {
        // Stations are safe zones, or the economy's hubs become gank-farms.
        foreach (ItemCategory category in AllCategories)
        {
            int quantity = category.IsStackable() ? 250 : 1;
            var item = new DeathItem($"test_{category}", category, quantity, InstanceId: 1);

            ResolvedItem resolved =
                DeathResolver.Resolve([item], DeathCause.PersonalCombatStation, 3UL)[0];

            Assert.Equal(quantity, resolved.SurvivedQuantity);
            Assert.Equal(0, resolved.DestroyedQuantity);
        }
    }

    [Fact]
    public void SelfDestruct_DestroysEverything()
    {
        foreach (ItemCategory category in AllCategories)
        {
            int quantity = category.IsStackable() ? 250 : 1;
            var item = new DeathItem($"test_{category}", category, quantity, InstanceId: 1);

            ResolvedItem resolved = DeathResolver.Resolve([item], DeathCause.SelfDestruct, 4UL)[0];

            Assert.Equal(quantity, resolved.DestroyedQuantity);
            Assert.Equal(0, resolved.LootableQuantity);
        }
    }

    [Fact]
    public void DisablingAShip_PreservesFarMoreCargoThanDestroyingIt()
    {
        // The deliberate tactical fork: a pirate who wants loot must disable rather than
        // obliterate. If this ever inverted, piracy would have no reason to exist.
        int explodedSurvivors = DeathResolver
            .Resolve([Ore(1_000)], DeathCause.ShipExplosion, 5UL)[0].SurvivedQuantity;

        int disabledSurvivors = DeathResolver
            .Resolve([Ore(1_000)], DeathCause.ShipDisabled, 5UL)[0].SurvivedQuantity;

        Assert.True(
            disabledSurvivors > explodedSurvivors * 3,
            $"Disabling preserved {disabledSurvivors} vs {explodedSurvivors} exploded — "
            + "the gap must be large enough to make disabling worth the extra difficulty.");
    }

    [Fact]
    public void ParkedHull_SurvivesItsOwnerDyingOnFoot()
    {
        foreach (DeathCause cause in new[]
        {
            DeathCause.PersonalCombatPlanet,
            DeathCause.PersonalCombatStation,
            DeathCause.EnvironmentalPlanet,
        })
        {
            ResolvedItem resolved = DeathResolver
                .Resolve([new DeathItem("hull_shuttle", ItemCategory.Hull, 1, 1)], cause, 6UL)[0];

            Assert.Equal(1, resolved.SurvivedQuantity);
        }
    }

    [Fact]
    public void ExplodingHull_IsAlwaysDestroyed()
    {
        for (ulong seed = 0; seed < 200; seed++)
        {
            ResolvedItem resolved = DeathResolver.Resolve(
                [new DeathItem("hull_shuttle", ItemCategory.Hull, 1, 1)],
                DeathCause.ShipExplosion,
                seed)[0];

            Assert.Equal(1, resolved.DestroyedQuantity);
        }
    }

    // ── Input validation ─────────────────────────────────────────────────────

    [Fact]
    public void Resolve_NonStackableWithQuantityAboveOne_Throws()
    {
        Assert.Throws<ArgumentException>(
            () => DeathResolver.Resolve(
                [new DeathItem("combat_armor", ItemCategory.Armor, 3, 1)],
                DeathCause.ShipExplosion,
                1UL));
    }

    [Theory]
    [InlineData(0)]
    [InlineData(-5)]
    public void Resolve_NonPositiveQuantity_Throws(int quantity)
    {
        Assert.Throws<ArgumentException>(
            () => DeathResolver.Resolve([Ore(quantity)], DeathCause.ShipExplosion, 1UL));
    }

    [Fact]
    public void Resolve_DuplicateInstanceIds_Throws()
    {
        // Two instances sharing a discriminator would share a random stream and always
        // resolve identically — a statistical bug nearly impossible to spot in play, so it
        // fails loudly instead. The usual cause is a caller leaving instance ids defaulted.
        Assert.Throws<ArgumentException>(
            () => DeathResolver.Resolve([Armor(1), Armor(1)], DeathCause.ShipExplosion, 1UL));
    }

    [Fact]
    public void Resolve_DuplicateStackableEntries_IsAllowed()
    {
        // Stackable items resolve without randomness, so they need no discriminator and
        // two ore entries are harmless.
        IReadOnlyList<ResolvedItem> resolved =
            DeathResolver.Resolve([Ore(100), Ore(100)], DeathCause.ShipExplosion, 1UL);

        Assert.Equal(2, resolved.Count);
    }
}
