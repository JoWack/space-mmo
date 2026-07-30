namespace SpaceMMO.Domain.Items;

/// <summary>
/// Where an inventory lives, which determines its capacity and who can reach it.
/// </summary>
public enum InventoryKind
{
    /// <summary>
    /// Carried on the character's person. Small, and travels with them everywhere — including
    /// into on-foot combat, where death resolution applies.
    /// </summary>
    CharacterCarried = 0,

    /// <summary>
    /// A ship's cargo hold. Volumetric capacity set by the hull, and the thing that is inside
    /// the explosion when a ship is destroyed.
    /// </summary>
    ShipHold = 1,

    /// <summary>
    /// Storage rented at a station. Safe from death resolution, but accrues station rent —
    /// the sink that stops it becoming a free infinite warehouse.
    /// </summary>
    StationHangar = 2,
}
