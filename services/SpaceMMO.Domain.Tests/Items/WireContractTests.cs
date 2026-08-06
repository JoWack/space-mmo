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
/// Renumbering any of these means editing EBackendInventoryKind or EBackendOrderSide in
/// client/Source/SpaceMMOBackend/Public/SpaceMMOBackendTypes.h in the same commit.
/// </remarks>
public sealed class WireContractTests
{
    [Theory]
    [InlineData(InventoryKind.CharacterCarried, 0)]
    [InlineData(InventoryKind.ShipHold, 1)]
    [InlineData(InventoryKind.StationHangar, 2)]
    public void InventoryKindKeepsItsWireValue(InventoryKind kind, int expected) =>
        Assert.Equal(expected, (int)kind);

    [Theory]
    [InlineData(OrderSide.Buy, 0)]
    [InlineData(OrderSide.Sell, 1)]
    public void OrderSideKeepsItsWireValue(OrderSide side, int expected) =>
        Assert.Equal(expected, (int)side);
}
