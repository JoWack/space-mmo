using SpaceMMO.Domain.Characters;

namespace SpaceMMO.Domain.Items;

/// <summary>
/// Which world's look a good has (ADR-0008, task 187).
/// </summary>
/// <remarks>
/// <para>
/// <strong>A crafted good is one item with a style, not four items.</strong> Four races' plate shares
/// one order book, so the market stays liquid at small population while each world's goods still
/// look like that world's (ADR-0008).
/// </para>
/// <para>
/// <strong>Style follows the materials, not the maker</strong> (Joe, 10 October 2026): "Goods made
/// from common materials should all have the same style - only good manufactured from
/// racial-specific materials have that racial style." So a crude mining laser, made of scrap and
/// nothing else, is <see cref="Common"/> whoever made it, and plate refined from ferrite is
/// <see cref="Terran"/> whoever refined it.
/// </para>
/// <para>
/// Named for the worlds rather than the races, as the Visual Bible and task 178 name them. Crosses
/// the wire as its number, which <c>EBackendItemStyle</c> in the client mirrors.
/// </para>
/// </remarks>
public enum ItemStyle
{
    /// <summary>Made from shared materials only: scrap, or a mix no one world made.</summary>
    Common = 0,

    /// <summary>Terra's, the Humanoid homeworld.</summary>
    Terran = 1,

    /// <summary>Ares', the Martian homeworld.</summary>
    Martian = 2,

    /// <summary>Verdance's, the Space Elf homeworld.</summary>
    Verdant = 3,

    /// <summary>Grimhold's, the Space Orc homeworld.</summary>
    Grimhold = 4,
}

/// <summary>
/// The rules that decide a good's style.
/// </summary>
public static class ItemStyles
{
    /// <summary>The style of a race's homeworld.</summary>
    /// <exception cref="ArgumentOutOfRangeException">If the race is not a defined value.</exception>
    public static ItemStyle ForRace(Race race) => race switch
    {
        Race.Humanoid => ItemStyle.Terran,
        Race.Martian => ItemStyle.Martian,
        Race.SpaceElf => ItemStyle.Verdant,
        Race.SpaceOrc => ItemStyle.Grimhold,
        _ => throw new ArgumentOutOfRangeException(nameof(race), race, "Unknown race."),
    };

    /// <summary>
    /// The style a homeworld's materials carry, or <see cref="ItemStyle.Common"/> for a body that is
    /// nobody's home, such as the Capital.
    /// </summary>
    public static ItemStyle ForBody(string bodyKey)
    {
        foreach (Race race in Enum.GetValues<Race>())
        {
            if (string.Equals(Races.HomeBodyKeyFor(race), bodyKey, StringComparison.Ordinal))
            {
                return ForRace(race);
            }
        }

        return ItemStyle.Common;
    }

    /// <summary>
    /// The style a job's output takes: its main input's, or Common if the recipe names none.
    /// </summary>
    /// <param name="mainInputStyle">
    /// The style the main input was drawn in, or null if the recipe has no main input.
    /// </param>
    /// <remarks>
    /// A recipe without a main input either consumes only common materials, as the crude laser does,
    /// or consumes several worlds' at once with none of them in charge, as the alloy frame does. Both
    /// are made by no one world, which is what Common means. A fusion style for the second kind is
    /// task 178's to decide, and would replace this answer rather than sit beside it.
    /// </remarks>
    public static ItemStyle ForOutput(ItemStyle? mainInputStyle) => mainInputStyle ?? ItemStyle.Common;

    /// <summary>
    /// Which style of a material to draw from first, when an inventory holds several.
    /// </summary>
    /// <param name="held">What is held, by style. Styles holding none may be omitted.</param>
    /// <param name="preferred">
    /// Drawn first if held: for an ordinary input, the style the main input was drawn in, so a
    /// Terran shuttle takes Terran thrusters while there are any.
    /// </param>
    /// <remarks>
    /// After the preferred style, the largest stack first, so a draw breaks up as few stacks as it
    /// can; ties in style order, so the answer never depends on the order rows came back in.
    /// </remarks>
    public static IReadOnlyList<ItemStyle> DrawOrder(
        IReadOnlyDictionary<ItemStyle, int> held, ItemStyle? preferred)
    {
        ArgumentNullException.ThrowIfNull(held);

        return [.. held
            .Where(h => h.Value > 0)
            .OrderByDescending(h => h.Key == preferred)
            .ThenByDescending(h => h.Value)
            .ThenBy(h => h.Key)
            .Select(h => h.Key)];
    }

    /// <summary>
    /// The style of a main input to use when the player did not pick one: the one held most of.
    /// </summary>
    /// <returns>Null if none is held at all.</returns>
    /// <remarks>
    /// The client always sends the player's pick (task 187's industry panel). This answers a caller
    /// that does not, such as a test or an older client, with the choice a player holding one style
    /// would have made anyway.
    /// </remarks>
    public static ItemStyle? MostHeld(IReadOnlyDictionary<ItemStyle, int> held)
    {
        IReadOnlyList<ItemStyle> order = DrawOrder(held, preferred: null);

        return order.Count == 0 ? null : order[0];
    }
}
