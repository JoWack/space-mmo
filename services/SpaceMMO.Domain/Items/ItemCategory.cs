namespace SpaceMMO.Domain.Items;

/// <summary>
/// Item categories, per design-bible §3. Drives UI grouping, storage rules, and death
/// resolution.
/// </summary>
/// <remarks>
/// Persisted as a string, not an integer, so the database is readable during the many hours
/// of hand-querying a project this size involves. That makes reordering members safe and
/// <em>renaming</em> one a data migration — the opposite of the tradeoff integer storage
/// would give. Adding a member is always safe.
/// </remarks>
public enum ItemCategory
{
    /// <summary>Unprocessed gathered material.</summary>
    Raw = 0,

    /// <summary>Processed intermediate.</summary>
    Refined = 1,

    /// <summary>Manufactured part.</summary>
    Component = 2,

    /// <summary>Single-use item.</summary>
    Consumable = 3,

    /// <summary>Equipment that gates a gathering skill.</summary>
    Tool = 4,

    /// <summary>Ship-fittable equipment.</summary>
    Module = 5,

    /// <summary>Personal protective equipment.</summary>
    Armor = 6,

    /// <summary>Personal weapon.</summary>
    Weapon = 7,

    /// <summary>Ship frame.</summary>
    Hull = 8,
}

/// <summary>
/// Category properties derived from design-bible §3.
/// </summary>
/// <remarks>
/// Stackability and condition are computed from the category rather than stored as
/// separate columns. A stored flag could disagree with the category, and a
/// <c>Hull</c> row marked stackable would be a duplication exploit.
/// </remarks>
public static class ItemCategoryExtensions
{
    /// <summary>
    /// True if items in this category are stored as <c>(item_def, qty)</c> pairs rather
    /// than as individually tracked instances.
    /// </summary>
    public static bool IsStackable(this ItemCategory category) => category switch
    {
        ItemCategory.Raw or
        ItemCategory.Refined or
        ItemCategory.Component or
        ItemCategory.Consumable => true,

        ItemCategory.Tool or
        ItemCategory.Module or
        ItemCategory.Armor or
        ItemCategory.Weapon or
        ItemCategory.Hull => false,

        _ => throw new ArgumentOutOfRangeException(
            nameof(category), category, "Unhandled item category."),
    };

    /// <summary>
    /// True if items in this category track a 0–100 condition value and can therefore
    /// be left <em>damaged</em> by death resolution rather than only destroyed.
    /// </summary>
    /// <remarks>
    /// Exactly the inverse of <see cref="IsStackable"/> today, but they are distinct
    /// concepts and will diverge — a stackable item with wear, or an unstackable one
    /// without, is entirely plausible later.
    /// </remarks>
    public static bool HasCondition(this ItemCategory category) => !category.IsStackable();
}
