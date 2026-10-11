using SpaceMMO.Domain.Characters;
using SpaceMMO.Domain.Items;
using Xunit;

namespace SpaceMMO.Domain.Tests.Items;

/// <summary>
/// The pure rules behind a good's style (task 187). The services that apply them are tested against
/// the shipped content in <c>SpaceMMO.Data.Tests.Industry.ItemStyleTests</c>.
/// </summary>
public sealed class ItemStyleTests
{
    [Theory]
    [InlineData(Race.Humanoid, ItemStyle.Terran)]
    [InlineData(Race.Martian, ItemStyle.Martian)]
    [InlineData(Race.SpaceElf, ItemStyle.Verdant)]
    [InlineData(Race.SpaceOrc, ItemStyle.Grimhold)]
    public void EachRacesHomeworldHasItsOwnStyle(Race race, ItemStyle expected)
    {
        Assert.Equal(expected, ItemStyles.ForRace(race));
        Assert.Equal(expected, ItemStyles.ForBody(Races.HomeBodyKeyFor(race)));
    }

    [Fact]
    public void NoTwoRacesShareAStyle_AndNoneIsCommon()
    {
        ItemStyle[] styles = [.. Enum.GetValues<Race>().Select(ItemStyles.ForRace)];

        Assert.Equal(styles.Length, styles.Distinct().Count());
        Assert.DoesNotContain(ItemStyle.Common, styles);
    }

    [Fact]
    public void ABodyThatIsNobodysHomeIsCommon() =>
        Assert.Equal(ItemStyle.Common, ItemStyles.ForBody("body_capital"));

    [Fact]
    public void AnOutputTakesItsMainInputsStyle_OrCommonWithoutOne()
    {
        Assert.Equal(ItemStyle.Martian, ItemStyles.ForOutput(ItemStyle.Martian));
        Assert.Equal(ItemStyle.Common, ItemStyles.ForOutput(ItemStyle.Common));
        Assert.Equal(ItemStyle.Common, ItemStyles.ForOutput(null));
    }

    [Fact]
    public void ThePreferredStyleIsDrawnFirst_EvenWhenItIsTheSmallest()
    {
        var held = new Dictionary<ItemStyle, int>
        {
            [ItemStyle.Terran] = 50,
            [ItemStyle.Martian] = 1,
            [ItemStyle.Grimhold] = 7,
        };

        Assert.Equal(
            [ItemStyle.Martian, ItemStyle.Terran, ItemStyle.Grimhold],
            ItemStyles.DrawOrder(held, ItemStyle.Martian));
    }

    [Fact]
    public void WithoutAPreference_TheLargestStackIsDrawnFirst_AndTiesGoInStyleOrder()
    {
        // Ties are broken by the style, never by the order the rows came back in, so the same
        // hangar always draws the same way.
        var held = new Dictionary<ItemStyle, int>
        {
            [ItemStyle.Grimhold] = 4,
            [ItemStyle.Verdant] = 9,
            [ItemStyle.Terran] = 4,
        };

        Assert.Equal(
            [ItemStyle.Verdant, ItemStyle.Terran, ItemStyle.Grimhold],
            ItemStyles.DrawOrder(held, preferred: null));
    }

    [Fact]
    public void APreferenceThatIsNotHeldChangesNothing_AndEmptyStacksAreSkipped()
    {
        var held = new Dictionary<ItemStyle, int>
        {
            [ItemStyle.Terran] = 3,
            [ItemStyle.Martian] = 0,
        };

        Assert.Equal([ItemStyle.Terran], ItemStyles.DrawOrder(held, ItemStyle.Martian));
    }

    [Fact]
    public void TheStyleHeldMostOfIsTheDefaultPick_AndNothingHeldIsNoPick()
    {
        Assert.Equal(
            ItemStyle.Verdant,
            ItemStyles.MostHeld(new Dictionary<ItemStyle, int>
            {
                [ItemStyle.Terran] = 2,
                [ItemStyle.Verdant] = 6,
            }));

        Assert.Null(ItemStyles.MostHeld(new Dictionary<ItemStyle, int>()));
    }
}
