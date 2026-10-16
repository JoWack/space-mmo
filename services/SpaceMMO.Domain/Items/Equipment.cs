namespace SpaceMMO.Domain.Items;

/// <summary>
/// A place on a character that an item can be worn or held in (task 193).
/// </summary>
/// <remarks>
/// <para>
/// <strong>Decided by Joe, 16 October 2026.</strong> Two weapons, switched between (ADR-0015); armour for the
/// three hit zones, head, body and legs, so each piece protects hits to its own zone; and one slot for each
/// tool type, because a tool must be equipped to work.
/// </para>
/// <para>
/// Crosses the wire and is stored by name, like every enum here: append, never reorder. A second tool type
/// is a new member after <see cref="MiningTool"/>.
/// </para>
/// </remarks>
public enum EquipmentSlot
{
    Weapon1 = 0,
    Weapon2 = 1,
    Head = 2,
    Body = 3,
    Legs = 4,

    /// <summary>Where a mining tool must be for mining to work: the crude mining laser, so far.</summary>
    MiningTool = 5,
}

/// <summary>What sort of slot an item goes in: what content says about it (task 193).</summary>
/// <remarks>
/// Separate from <see cref="EquipmentSlot"/> because a weapon goes in either of two slots: an item says it is
/// a weapon, and the character's state says which of Weapon 1 and Weapon 2 it is in.
/// </remarks>
public enum EquipKind
{
    Weapon = 0,
    Head = 1,
    Body = 2,
    Legs = 3,
    MiningTool = 4,
}

/// <summary>The rules of what goes where (task 193).</summary>
public static class Equipment
{
    /// <summary>The slots an item of this kind may go in, in the order an empty one is chosen.</summary>
    public static IReadOnlyList<EquipmentSlot> SlotsFor(EquipKind kind) => kind switch
    {
        EquipKind.Weapon => [EquipmentSlot.Weapon1, EquipmentSlot.Weapon2],
        EquipKind.Head => [EquipmentSlot.Head],
        EquipKind.Body => [EquipmentSlot.Body],
        EquipKind.Legs => [EquipmentSlot.Legs],
        EquipKind.MiningTool => [EquipmentSlot.MiningTool],
        _ => throw new ArgumentOutOfRangeException(nameof(kind), kind, "Unknown equip kind."),
    };

    /// <summary>Whether an item of this kind may go in this slot.</summary>
    public static bool Fits(EquipKind kind, EquipmentSlot slot) => SlotsFor(kind).Contains(slot);

    /// <summary>
    /// The first empty slot an item of this kind may go in, or null when every one is taken.
    /// </summary>
    /// <remarks>
    /// What auto-equipping a newly claimed tool or weapon asks (Joe, 16 October): it goes in only where nothing
    /// is, so claiming a second laser never takes the first off.
    /// </remarks>
    public static EquipmentSlot? FirstEmpty(EquipKind kind, IReadOnlyCollection<EquipmentSlot> occupied)
    {
        ArgumentNullException.ThrowIfNull(occupied);

        foreach (EquipmentSlot slot in SlotsFor(kind))
        {
            if (!occupied.Contains(slot))
            {
                return slot;
            }
        }

        return null;
    }

    /// <summary>
    /// Where an item equipped without a slot named goes: the first empty one, or the first of its slots,
    /// swapping out what is there.
    /// </summary>
    /// <remarks>Double-clicking a weapon fills Weapon 2 when Weapon 1 is taken, and replaces Weapon 1 when both are.</remarks>
    public static EquipmentSlot Choose(EquipKind kind, IReadOnlyCollection<EquipmentSlot> occupied) =>
        FirstEmpty(kind, occupied) ?? SlotsFor(kind)[0];

    /// <summary>
    /// Which kinds an item category may author: weapons are weapons, armour is a piece for a zone, and a tool
    /// is for a tool slot. Nothing else is worn.
    /// </summary>
    public static bool CategoryMayBe(ItemCategory category, EquipKind kind) => category switch
    {
        ItemCategory.Weapon => kind == EquipKind.Weapon,
        ItemCategory.Armor => kind is EquipKind.Head or EquipKind.Body or EquipKind.Legs,
        ItemCategory.Tool => kind == EquipKind.MiningTool,
        _ => false,
    };

    /// <summary>Whether items of this category are worn at all, and so must say how.</summary>
    public static bool IsEquippable(ItemCategory category) =>
        category is ItemCategory.Weapon or ItemCategory.Armor or ItemCategory.Tool;
}
