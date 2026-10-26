using SpaceMMO.Domain.Economy;
using SpaceMMO.Domain.Industry;
using SpaceMMO.Domain.Loss;
using Xunit;

namespace SpaceMMO.Domain.Tests.Industry;

/// <summary>What a repair does and costs (ADR-0016 as amended, task 196).</summary>
public sealed class RepairsTests
{
    // ── The maximum ──────────────────────────────────────────────────────────

    [Fact]
    public void A_full_repair_from_broken_takes_18_off_the_maximum()
    {
        Assert.Equal(new RepairPlan(100, 82, 8200, false), Repairs.Plan(0, Repairs.NewMaxHundredths));
    }

    [Fact]
    public void One_deaths_top_up_takes_under_2()
    {
        Assert.Equal(new RepairPlan(10, 98, 9820, false), Repairs.Plan(90, Repairs.NewMaxHundredths));
    }

    [Fact]
    public void Only_whole_points_restored_are_charged_against_the_maximum()
    {
        // 98.20 allows 98: the fifth of a point it never gave back costs nothing.
        Assert.Equal(new RepairPlan(10, 96, 9640, false), Repairs.Plan(88, 9820));
    }

    [Theory]
    [InlineData(100, 10_000)]
    [InlineData(98, 9820)]
    [InlineData(0, 99)]
    public void Nothing_to_repair_is_refused(int condition, int maxHundredths)
    {
        Assert.False(Repairs.IsDamaged(condition, maxHundredths));
        Assert.Throws<ArgumentOutOfRangeException>(() => Repairs.Plan(condition, maxHundredths));
    }

    [Fact]
    public void Below_one_deaths_wear_is_worn_out()
    {
        // 11.00 from broken loses 1.98, leaving 9.02: the next death would break it whatever its owner did.
        Assert.True(Repairs.Plan(0, 1100).WornOut);
        Assert.Equal(902, Repairs.Plan(0, 1100).MaxAfterHundredths);
    }

    [Fact]
    public void Exactly_one_deaths_wear_is_not_worn_out()
    {
        // 11.80 at 1 restores 10 and loses 1.80, leaving 10.00: strictly below wears out, and this is not below.
        RepairPlan plan = Repairs.Plan(1, 1180);

        Assert.Equal(1000, plan.MaxAfterHundredths);
        Assert.False(plan.WornOut);
        Assert.Equal(DeathCosts.WearPerDeath, plan.ConditionAfter);
    }

    [Fact]
    public void Condition_never_ends_above_the_maximum()
    {
        for (int max = 0; max <= Repairs.NewMaxHundredths; max += 7)
        {
            for (int condition = 0; condition < Repairs.WholeMax(max); condition++)
            {
                RepairPlan plan = Repairs.Plan(condition, max);

                Assert.True(plan.ConditionAfter * Repairs.HundredthsPerPoint <= plan.MaxAfterHundredths,
                    $"{condition} at {max}: {plan}");
                Assert.True(plan.ConditionAfter >= condition, $"{condition} at {max} went down: {plan}");
            }
        }
    }

    // ── The lifetime ─────────────────────────────────────────────────────────

    /// <summary>Deaths an item survives, worn every death and repaired whenever <paramref name="repairNow"/> says.</summary>
    private static int DeathsSurvived(Func<int, bool> repairNow)
    {
        int condition = Repairs.FullCondition;
        int max = Repairs.NewMaxHundredths;

        for (int deaths = 0; ; deaths++)
        {
            if (Repairs.IsDamaged(condition, max) && repairNow(condition))
            {
                RepairPlan plan = Repairs.Plan(condition, max);

                if (plan.WornOut)
                {
                    // The death that brought it here is the one it did not come back from.
                    return deaths - 1;
                }

                (condition, max) = (plan.ConditionAfter, plan.MaxAfterHundredths);
            }

            condition = DeathCosts.Worn(condition);
        }
    }

    [Fact]
    public void Repaired_after_every_death_an_item_lasts_50()
    {
        // Joe's choice of 18 per 100 was the lifetime: "18 per 100: ~50 deaths".
        Assert.Equal(50, DeathsSurvived(_ => true));
    }

    [Fact]
    public void Waiting_until_broken_does_not_buy_a_much_longer_life()
    {
        // Why the loss is proportional: a flat 2 a repair gave about 50 deaths repaired every time and 255 repaired
        // only when broken, so the thrifty play was to carry near-broken gear into fights.
        int everyDeath = DeathsSurvived(_ => true);
        int whenBroken = DeathsSurvived(condition => condition == 0);

        Assert.InRange(whenBroken, everyDeath, everyDeath * 6 / 5);
    }

    // ── Materials ────────────────────────────────────────────────────────────

    [Theory]
    [InlineData(8, 1, 200)]
    [InlineData(6, 1, 150)]
    [InlineData(10, 1, 250)]
    [InlineData(1, 1, 25)]
    [InlineData(1, 2, 13)]
    public void A_point_owes_a_quarter_of_the_input_per_hundred(int input, int output, long owed)
    {
        Assert.Equal(owed, Repairs.OwedPerPoint(input, output));
    }

    [Fact]
    public void A_full_repair_takes_a_quarter_of_the_recipe()
    {
        // The mining laser: 8 scrap alloy, so 2.
        Assert.Equal((2, 0), Repairs.Charge(0, Repairs.OwedPerPoint(8, 1), 100));

        // The rifle's 6 plate: one and a half, so one now and half on the tab.
        Assert.Equal((1, 5000), Repairs.Charge(0, Repairs.OwedPerPoint(6, 1), 100));
    }

    [Theory]
    [InlineData(8)]
    [InlineData(6)]
    [InlineData(1)]
    public void Ten_top_ups_cost_exactly_one_full_repair(int input)
    {
        long perPoint = Repairs.OwedPerPoint(input, 1);
        (int fullUnits, int fullTab) = Repairs.Charge(0, perPoint, 100);

        int units = 0;
        int tab = 0;

        for (int i = 0; i < 10; i++)
        {
            (int charged, tab) = Repairs.Charge(tab, perPoint, 10);
            units += charged;
        }

        Assert.Equal((fullUnits, fullTab), (units, tab));
    }

    [Fact]
    public void A_small_repair_only_adds_to_the_tab()
    {
        Assert.Equal((0, 2000), Repairs.Charge(0, Repairs.OwedPerPoint(8, 1), 10));
        Assert.Equal((1, 0), Repairs.Charge(8000, Repairs.OwedPerPoint(8, 1), 10));
    }

    [Fact]
    public void A_tab_of_a_whole_unit_or_more_is_refused()
    {
        // A whole unit is charged the moment it is owed, so a tab can never hold one.
        Assert.Throws<ArgumentOutOfRangeException>(() => Repairs.Charge(Repairs.TabPerUnit, 200, 10));
        Assert.Throws<ArgumentOutOfRangeException>(() => Repairs.Charge(-1, 200, 10));
    }

    // ── Credits ──────────────────────────────────────────────────────────────

    [Theory]
    [InlineData(100, 15)]
    [InlineData(10, 2)]
    [InlineData(50, 8)]
    [InlineData(4, 1)]
    [InlineData(3, 0)]
    [InlineData(0, 0)]
    public void Credits_are_the_crafting_fee_shared_and_rounded_to_the_credit(int restored, long credits)
    {
        Assert.Equal(Credits.FromWholeCredits(credits), Repairs.CreditsFor(restored));
    }

    [Fact]
    public void A_full_repair_costs_one_runs_crafting_fee()
    {
        Assert.Equal(IndustryFees.ForJob(1), Repairs.CreditsFor(Repairs.FullCondition));
    }
}
