using SpaceMMO.Domain.Economy;
using SpaceMMO.Domain.Items;
using SpaceMMO.Domain.Loss;
using Xunit;

namespace SpaceMMO.Domain.Tests.Loss;

/// <summary>What dying costs (ADR-0016, task 103).</summary>
public sealed class DeathCostsTests
{
    private static readonly Credits Forty = Credits.FromWholeCredits(40);

    [Theory]
    [InlineData(10, 5, 5)]
    [InlineData(5, 3, 2)]
    [InlineData(1, 1, 0)]
    [InlineData(2, 1, 1)]
    [InlineData(201, 101, 100)]
    public void Half_drops_half_is_destroyed_and_an_odd_stacks_extra_unit_drops(int carried, int dropped, int destroyed)
    {
        // Joe, 19 October: the extra unit drops, against rounding toward the sink.
        Assert.Equal((dropped, destroyed), DeathCosts.Split(carried));
    }

    [Fact]
    public void A_stack_of_nothing_is_not_split()
    {
        Assert.Throws<ArgumentOutOfRangeException>(() => DeathCosts.Split(0));
    }

    [Theory]
    [InlineData(100, 90)]
    [InlineData(15, 5)]
    [InlineData(10, 0)]
    [InlineData(4, 0)]
    [InlineData(0, 0)]
    public void Worn_gear_loses_ten_and_stops_at_broken(int before, int after)
    {
        Assert.Equal(after, DeathCosts.Worn(before));
    }

    [Fact]
    public void Every_material_carried_is_lost_and_nothing_else()
    {
        DeathCostPlan plan = DeathCosts.Plan(
            DeathCause.PersonalCombatPlanet,
            [
                new CarriedGoods(1, ItemStyle.Common, ItemCategory.Raw, 10, Forty),
                new CarriedGoods(2, ItemStyle.Martian, ItemCategory.Refined, 3, Credits.Zero),
                new CarriedGoods(3, ItemStyle.Common, ItemCategory.Component, 1, Credits.Zero),
                new CarriedGoods(4, ItemStyle.Common, ItemCategory.Consumable, 4, Credits.Zero),

                // Not a material, so kept: a stack of one can never exist, and the rule says so rather than
                // trusting it.
                new CarriedGoods(5, ItemStyle.Common, ItemCategory.Weapon, 1, Credits.Zero),
            ],
            []);

        Assert.Equal(
            [
                new LostGoods(1, ItemStyle.Common, 5, 5, Credits.FromWholeCredits(20)),
                new LostGoods(2, ItemStyle.Martian, 2, 1, Credits.Zero),
                new LostGoods(3, ItemStyle.Common, 1, 0, Credits.Zero),
                new LostGoods(4, ItemStyle.Common, 2, 2, Credits.Zero),
            ],
            plan.Lost);

        Assert.True(plan.DropsAnything);
    }

    [Fact]
    public void Everything_worn_wears()
    {
        DeathCostPlan plan = DeathCosts.Plan(
            DeathCause.PersonalCombatPlanet, [], [new WornItem(7, 100), new WornItem(8, 70), new WornItem(9, 5)]);

        Assert.Equal([new WornDown(7, 100, 90), new WornDown(8, 70, 60), new WornDown(9, 5, 0)], plan.Worn);
        Assert.Empty(plan.Lost);
        Assert.False(plan.DropsAnything);
    }

    [Fact]
    public void Dying_at_a_station_costs_nothing()
    {
        Assert.Equal(DeathCause.PersonalCombatStation, DeathCosts.OnFoot(docked: true));
        Assert.Equal(DeathCause.PersonalCombatPlanet, DeathCosts.OnFoot(docked: false));

        DeathCostPlan plan = DeathCosts.Plan(
            DeathCause.PersonalCombatStation,
            [new CarriedGoods(1, ItemStyle.Common, ItemCategory.Raw, 10, Forty)],
            [new WornItem(7, 100)]);

        Assert.Empty(plan.Lost);
        Assert.Empty(plan.Worn);
    }

    [Theory]
    [InlineData(4_001, 3, 7)]
    [InlineData(100, 1, 3)]
    [InlineData(0, 5, 10)]
    public void The_dropped_half_takes_its_share_of_the_cost_and_the_two_shares_sum_to_the_whole(
        long basis, int part, int whole)
    {
        Credits all = Credits.FromMinorUnits(basis);
        Credits share = DeathCosts.ShareOf(all, part, whole);
        Credits rest = DeathCosts.ShareOf(all, whole - part, whole);

        // Floored, so the share never exceeds its fraction, and the rest is at least its own.
        Assert.True(share.MinorUnits * whole <= basis * part);
        Assert.True((all - share).MinorUnits >= rest.MinorUnits);
    }

    [Fact]
    public void The_container_lasts_ten_minutes()
    {
        Assert.Equal(TimeSpan.FromMinutes(10), DeathCosts.LootableFor);
    }
}
