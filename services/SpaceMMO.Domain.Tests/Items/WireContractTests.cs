using SpaceMMO.Domain.Economy;
using SpaceMMO.Domain.Items;
using Xunit;

namespace SpaceMMO.Domain.Tests.Items;

/// <summary>
/// Enum values the UE client mirrors by number rather than by name.
/// </summary>
/// <remarks>
/// These cross the wire as integers, so the client holds its own copy of the numbering and has no
/// way to check it. A mismatch does not throw and does not fail a request — the client simply
/// matches the wrong rows and shows the player nothing, which is how a market UI once shipped with
/// a hangar filter that only ever matched ship holds.
///
/// Renumbering any of these means editing EBackendInventoryKind, EBackendOrderSide,
/// EBackendItemStyle, EBackendEquipmentSlot or EBackendEquipKind in
/// client/Source/SpaceMMOBackend/Public/SpaceMMOBackendTypes.h in the same commit.
/// </remarks>
public sealed class WireContractTests
{
    [Theory]
    [InlineData(InventoryKind.CharacterCarried, 0)]
    [InlineData(InventoryKind.ShipHold, 1)]
    [InlineData(InventoryKind.StationHangar, 2)]
    [InlineData(InventoryKind.Equipped, 3)]
    public void InventoryKindKeepsItsWireValue(InventoryKind kind, int expected) =>
        Assert.Equal(expected, (int)kind);

    [Theory]
    [InlineData(OrderSide.Buy, 0)]
    [InlineData(OrderSide.Sell, 1)]
    public void OrderSideKeepsItsWireValue(OrderSide side, int expected) =>
        Assert.Equal(expected, (int)side);

    [Theory]
    [InlineData(ItemStyle.Common, 0)]
    [InlineData(ItemStyle.Terran, 1)]
    [InlineData(ItemStyle.Martian, 2)]
    [InlineData(ItemStyle.Verdant, 3)]
    [InlineData(ItemStyle.Grimhold, 4)]
    public void ItemStyleKeepsItsWireValue(ItemStyle style, int expected) =>
        Assert.Equal(expected, (int)style);

    [Theory]
    [InlineData(EquipmentSlot.Weapon1, 0)]
    [InlineData(EquipmentSlot.Weapon2, 1)]
    [InlineData(EquipmentSlot.Head, 2)]
    [InlineData(EquipmentSlot.Body, 3)]
    [InlineData(EquipmentSlot.Legs, 4)]
    [InlineData(EquipmentSlot.MiningTool, 5)]
    public void EquipmentSlotKeepsItsWireValue(EquipmentSlot slot, int expected) =>
        Assert.Equal(expected, (int)slot);

    [Theory]
    [InlineData(EquipKind.Weapon, 0)]
    [InlineData(EquipKind.Head, 1)]
    [InlineData(EquipKind.Body, 2)]
    [InlineData(EquipKind.Legs, 3)]
    [InlineData(EquipKind.MiningTool, 4)]
    public void EquipKindKeepsItsWireValue(EquipKind kind, int expected) =>
        Assert.Equal(expected, (int)kind);

    // Sent by the game server rather than the client (task 194), and pinned for the same reason: the server's
    // C++ holds its own copy of the numbering.
    [Theory]
    [InlineData(Domain.Combat.HitZone.Head, 0)]
    [InlineData(Domain.Combat.HitZone.Body, 1)]
    [InlineData(Domain.Combat.HitZone.Legs, 2)]
    public void HitZoneKeepsItsWireValue(Domain.Combat.HitZone zone, int expected) =>
        Assert.Equal(expected, (int)zone);

    [Theory]
    [InlineData(Domain.Combat.HitFacing.Front, 0)]
    [InlineData(Domain.Combat.HitFacing.Side, 1)]
    [InlineData(Domain.Combat.HitFacing.Back, 2)]
    public void HitFacingKeepsItsWireValue(Domain.Combat.HitFacing facing, int expected) =>
        Assert.Equal(expected, (int)facing);
}
