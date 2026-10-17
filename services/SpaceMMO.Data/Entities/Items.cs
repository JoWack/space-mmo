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
public class ItemDef : IRetirableContent
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

    /// <summary>
    /// A group a quest step may ask for as one — <c>starter_ore</c> for the four homeworld ores — or
    /// null (task 186).
    /// </summary>
    public string? Family { get; set; }

    /// <summary>
    /// The world a raw material belongs to, and so the style of what is made from it (task 187).
    /// <see cref="ItemStyle.Common"/> for everything else.
    /// </summary>
    /// <remarks>
    /// A raw material's style is the item's, because each world's ore is already a different item
    /// (ADR-0008); its stacks never carry one of their own. A crafted good's style is the stack's or
    /// the instance's, decided when it was made.
    /// </remarks>
    public ItemStyle Style { get; set; }

    /// <summary>What slot this goes in, for a weapon, armour piece or tool; null otherwise (task 193).</summary>
    public EquipKind? EquipsAs { get; set; }

    /// <summary>For a weapon (task 194), the skill it levels; null for anything else.</summary>
    public int? WeaponSkillId { get; set; }

    public Skill? WeaponSkill { get; set; }

    /// <summary>A basic attack's damage on the body from the front, before armour: the weapon's tier (ADR-0017).</summary>
    public int? WeaponDamage { get; set; }

    /// <summary>The least time between two basic attacks, in milliseconds.</summary>
    public int? WeaponCooldownMs { get; set; }

    /// <summary>How far a basic attack reaches, in metres.</summary>
    public int? WeaponRangeMetres { get; set; }

    /// <summary>
    /// When content stopped naming this item, or null while it still does (task 188).
    /// </summary>
    /// <remarks>
    /// Retired rather than deleted: stacks, instances, orders and ledger lines still refer to it
    /// and stay valid. Goods already held keep working -- moved, sold, used as inputs -- and only
    /// new supply stops, since no recipe or deposit in the pack can make one (Joe, 11 October).
    /// Authoring the key again clears it.
    /// </remarks>
    public DateTimeOffset? RetiredAt { get; set; }
}

/// <summary>A crafting recipe, loaded from <c>data/recipes/</c>.</summary>
public class Recipe : IRetirableContent
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

    /// <summary>
    /// The input whose style the output takes, or null for a recipe that makes Common goods (task 187).
    /// </summary>
    /// <remarks>
    /// The whole of it is drawn from one style, which the player picks when starting the job.
    /// </remarks>
    public int? MainInputItemDefId { get; set; }

    public ItemDef? MainInputItemDef { get; set; }

    /// <summary>
    /// When content stopped naming this recipe, or null while it still does (task 188).
    /// </summary>
    /// <remarks>
    /// Retired rather than deleted: jobs, running and finished, still refer to it and stay valid.
    /// It is not listed and no job can start from it; a running one can still be claimed or
    /// cancelled. Authoring the key again clears it.
    /// </remarks>
    public DateTimeOffset? RetiredAt { get; set; }

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
    /// Which world's look these goods have (task 187). Part of the key: one inventory holds Terran
    /// plate and Martian plate as two stacks.
    /// </summary>
    /// <remarks>
    /// Always <see cref="ItemStyle.Common"/> for a raw material, whose world is the item's own
    /// (<see cref="ItemDef.Style"/>). So a raw material still has one stack per inventory, and
    /// re-authoring an ore's style never strands a stack under the old one.
    /// </remarks>
    public ItemStyle Style { get; set; }

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
    /// Which world's look this item has, decided when it was made (task 187).
    /// </summary>
    public ItemStyle Style { get; set; }

    /// <summary>
    /// Which slot it is in, while it sits in its owner's Equipped inventory; null everywhere else (task 193).
    /// </summary>
    /// <remarks>
    /// One item to a slot, enforced by a unique index on the inventory and the slot. Set and cleared only by
    /// <c>EquipmentService</c>: the transfer routes refuse the Equipped inventory, so nothing arrives there
    /// without a slot or leaves still holding one.
    /// </remarks>
    public EquipmentSlot? EquippedSlot { get; set; }

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

    /// <summary>
    /// Where this hull is standing in the world, in system kilometres, or null if it is put away.
    /// </summary>
    /// <remarks>
    /// <para>
    /// <strong>Null is the load-bearing value: it means "inside a hangar".</strong> ADR-0012 said
    /// where a ship is needs no column, and that was true exactly while a parked ship and a ship
    /// standing on the apron were the same thing. Task 153 made docking remove the pawn, so one row
    /// — a hull sitting in a station's hangar inventory — came to mean two different worlds: one
    /// where a ship is visible outside and one where it is not. Nothing could tell them apart, so
    /// signing in spawned a ship that had been put away, and the Ships tab refused to summon one
    /// because it was "already here".
    /// </para>
    /// <para>
    /// <strong>Three nullable columns rather than a type</strong>, exactly as
    /// <c>Character.LastSystemX</c> is and for the same reason: a hull that is nowhere is not a hull
    /// at the origin, and a struct of three doubles would have to invent a sentinel to say so.
    /// </para>
    /// <para>
    /// <strong>On every item instance, not on a hulls-only table.</strong> ADR-0006 settled that
    /// trade already — "adding a column to a table full of live player items is far worse than
    /// carrying an unused one" — and this is three nullable doubles that are null for every ore and
    /// every laser.
    /// </para>
    /// <para>
    /// Written by the game server and nobody else, because it is a position (ADR-0003). It is set
    /// when a pawn is put in the world and while it is flown, and cleared when the ship is stowed.
    /// </para>
    /// </remarks>
    public double? DeployedSystemX { get; set; }

    public double? DeployedSystemY { get; set; }

    public double? DeployedSystemZ { get; set; }

    /// <summary>
    /// Whether this hull is standing in the world rather than inside a hangar.
    /// </summary>
    /// <remarks>
    /// The X column alone decides it. All three are written together and cleared together, and a
    /// reader that tested each separately would invent a fourth state that cannot occur.
    /// </remarks>
    public bool IsDeployed => DeployedSystemX is not null;

    /// <summary>Set when death resolution or salvage destroys the instance.</summary>
    public DateTimeOffset? DestroyedAt { get; set; }
}
