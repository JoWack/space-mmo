using SpaceMMO.Domain.Economy;
using SpaceMMO.Domain.Items;

namespace SpaceMMO.Data.Entities;

/// <summary>
/// An item definition. The fixed catalog loaded from <c>data/items/</c>.
/// </summary>
/// <remarks>
/// Per the locked scope decision, designs are fixed and players own the entire supply. So
/// rows here are content, never player-created.
/// </remarks>
public class ItemDef
{
    public int Id { get; set; }

    /// <summary>Stable key, e.g. <c>ferrite_ore</c>.</summary>
    public required string Key { get; set; }

    public required string Name { get; set; }

    public ItemCategory Category { get; set; }

    /// <summary>
    /// Volume per unit. Cargo capacity is volumetric, not slot-based — which is what makes
    /// bulk hauling a real profession.
    /// </summary>
    public double VolumeM3 { get; set; }

    /// <summary>
    /// How much this thing can carry, for a <see cref="ItemCategory.Hull"/>. Null for everything
    /// else, which is most things.
    /// </summary>
    /// <remarks>
    /// Authored rather than derived from <see cref="VolumeM3"/> (ADR-0014). A rule tying a hold to
    /// the size of the hull reads tidily and means a bigger ship can never be a <em>worse</em>
    /// hauler, which removes an axis of ship design before any ship exists.
    /// </remarks>
    public double? HoldCapacityM3 { get; set; }

    /// <summary>
    /// Not stored: derived from <see cref="Category"/> via
    /// <see cref="ItemCategoryExtensions.IsStackable"/>. A stored flag could contradict the
    /// category, and a <c>Hull</c> marked stackable would be a duplication exploit.
    /// </summary>
    public bool IsStackable => Category.IsStackable();

    /// <summary>
    /// What a faction standing order pays per unit, or null if no faction buys this.
    /// </summary>
    /// <remarks>
    /// A price floor so a player at zero credits is never stuck — they can neither start a job nor
    /// place a sell order, both of which charge up front, so ore would otherwise be unsellable
    /// precisely when it is the only thing they own. Deliberately low: if selling to the faction is
    /// ever better than selling to a player, the player market stops forming.
    ///
    /// Content restricts this to Raw items; see the validator.
    /// </remarks>
    public Credits? FactionBuyPrice { get; set; }
}

/// <summary>A crafting recipe, loaded from <c>data/recipes/</c>.</summary>
public class Recipe
{
    public int Id { get; set; }

    public required string Key { get; set; }

    public int OutputItemDefId { get; set; }

    public ItemDef? OutputItemDef { get; set; }

    public int OutputQuantity { get; set; }

    public int SkillId { get; set; }

    public Skill? Skill { get; set; }

    /// <summary>Minimum skill level required, 1–99.</summary>
    public int RequiredLevel { get; set; }

    /// <summary>Job duration in seconds. Enforced against the server clock, never the client's.</summary>
    public int JobSeconds { get; set; }

    /// <summary>
    /// Skill XP granted per run, awarded at claim and never at start.
    /// </summary>
    /// <remarks>
    /// Awarding at start would make start-and-cancel an XP farm costing only the job fee.
    /// </remarks>
    public long XpPerRun { get; set; }

    /// <summary>
    /// A tool that must be held to run this recipe, or null if none is needed. This is how
    /// the onboarding chain gates ore mining behind crafting a mining laser first.
    /// </summary>
    public int? RequiredToolItemDefId { get; set; }

    public ItemDef? RequiredToolItemDef { get; set; }

    public ICollection<RecipeInput> Inputs { get; } = [];
}

/// <summary>One material requirement of a recipe.</summary>
public class RecipeInput
{
    public int RecipeId { get; set; }

    public Recipe? Recipe { get; set; }

    public int ItemDefId { get; set; }

    public ItemDef? ItemDef { get; set; }

    public int Quantity { get; set; }
}

/// <summary>
/// A container of items: carried, in a ship hold, or rented at a station.
/// </summary>
public class Inventory
{
    public long Id { get; set; }

    public int CharacterId { get; set; }

    public Character? Character { get; set; }

    public InventoryKind Kind { get; set; }

    /// <summary>Set for <see cref="InventoryKind.StationHangar"/>; null otherwise.</summary>
    public int? StationId { get; set; }

    public Station? Station { get; set; }

    /// <summary>
    /// The ship this hold belongs to, for <see cref="InventoryKind.ShipHold"/>. Null otherwise.
    /// </summary>
    public long? ShipItemInstanceId { get; set; }

    public ItemInstance? ShipItemInstance { get; set; }

    /// <summary>Volumetric capacity. Zero means unlimited, used for station hangars.</summary>
    public double CapacityM3 { get; set; }

    public ICollection<InventoryItem> StackedItems { get; } = [];

    public ICollection<ItemInstance> Instances { get; } = [];
}

/// <summary>
/// A quantity of a stackable item in an inventory.
/// </summary>
/// <remarks>
/// Only for categories where <see cref="ItemCategoryExtensions.IsStackable"/> holds. Items
/// that track condition live in <see cref="ItemInstance"/> instead, because a stack has
/// nowhere to record wear (ADR-0006).
/// </remarks>
public class InventoryItem
{
    public long InventoryId { get; set; }

    public Inventory? Inventory { get; set; }

    public int ItemDefId { get; set; }

    public ItemDef? ItemDef { get; set; }

    public int Quantity { get; set; }

    /// <summary>
    /// What this whole stack actually cost its owner.
    /// </summary>
    /// <remarks>
    /// <para>
    /// Gathered material costs nothing but labour, so it enters at zero. Bought material enters at
    /// the price actually paid. When a job consumes a stack, its share of the cost basis flows
    /// into the crafted output's <see cref="ItemInstance.AcquisitionValue"/>.
    /// </para>
    /// <para>
    /// This exists so insurance can be pegged to what a hull genuinely cost to build (ADR-0006).
    /// Without it a crafted ship has no honest acquisition value, and the alternatives are a market
    /// reference price — precisely the insurance fraud vector — or a placeholder that permanently
    /// understates every player-built ship.
    /// </para>
    /// <para>
    /// A whole-stack total rather than a per-unit average, because per-unit would need rounding on
    /// every add and those errors would accumulate. Removal takes a proportional share and leaves
    /// the remainder, so the two always sum back to the original exactly.
    /// </para>
    /// </remarks>
    public Credits CostBasis { get; set; }
}

/// <summary>
/// An individually tracked, non-stackable item: a tool, module, weapon, armor piece, or hull.
/// </summary>
public class ItemInstance
{
    public long Id { get; set; }

    public int ItemDefId { get; set; }

    public ItemDef? ItemDef { get; set; }

    /// <summary>Null once the instance has been destroyed.</summary>
    public long? InventoryId { get; set; }

    public Inventory? Inventory { get; set; }

    /// <summary>
    /// Condition from 0 to 100. Below a threshold the item is unusable until repaired.
    /// </summary>
    /// <remarks>
    /// Present from the first migration even though the repair loop is deferred past M3.
    /// Adding a column to a table full of live player items is far worse than carrying an
    /// unused one (ADR-0006).
    /// </remarks>
    public int Condition { get; set; }

    /// <summary>
    /// What this instance actually cost — input material value if crafted, price paid if
    /// bought.
    /// </summary>
    /// <remarks>
    /// <strong>Insurance payouts are pegged to this and never to a market reference price.</strong>
    /// That single choice is what closes the insurance fraud vector (ADR-0006), so every
    /// creation path must set it correctly. It is non-nullable for exactly that reason: a path
    /// that forgets is an exploit.
    /// </remarks>
    public Credits AcquisitionValue { get; set; }

    public DateTimeOffset CreatedAt { get; set; }

    /// <summary>Set when death resolution or salvage destroys the instance.</summary>
    public DateTimeOffset? DestroyedAt { get; set; }
}
