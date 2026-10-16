using SpaceMMO.Domain.Items;
using Xunit;

namespace SpaceMMO.Domain.Tests.Items;

/// <summary>What goes in which slot (task 193).</summary>
public sealed class EquipmentTests
{
    [Fact]
    public void EveryKindHasASlot_AndEverySlotTakesSomeKind()
    {
        // A kind with no slot is an item nobody can wear; a slot no kind fits is a box on the screen that
        // can never fill. Both are a missing line in SlotsFor, and both would look like an empty slot.
        HashSet<EquipmentSlot> reached = [];

        foreach (EquipKind kind in Enum.GetValues<EquipKind>())
        {
            IReadOnlyList<EquipmentSlot> slots = Equipment.SlotsFor(kind);

            Assert.NotEmpty(slots);
            reached.UnionWith(slots);
        }

        Assert.Equal(Enum.GetValues<EquipmentSlot>().ToHashSet(), reached);
    }

    [Fact]
    public void AWeapon_GoesInEitherWeaponSlot_AndNothingElse()
    {
        Assert.True(Equipment.Fits(EquipKind.Weapon, EquipmentSlot.Weapon1));
        Assert.True(Equipment.Fits(EquipKind.Weapon, EquipmentSlot.Weapon2));
        Assert.False(Equipment.Fits(EquipKind.Weapon, EquipmentSlot.MiningTool));
        Assert.False(Equipment.Fits(EquipKind.Weapon, EquipmentSlot.Head));
    }

    [Fact]
    public void ArmourAndTools_GoOnlyInTheirOwnSlot()
    {
        Assert.True(Equipment.Fits(EquipKind.Head, EquipmentSlot.Head));
        Assert.False(Equipment.Fits(EquipKind.Head, EquipmentSlot.Body));
        Assert.False(Equipment.Fits(EquipKind.Legs, EquipmentSlot.Body));
        Assert.True(Equipment.Fits(EquipKind.MiningTool, EquipmentSlot.MiningTool));
        Assert.False(Equipment.Fits(EquipKind.MiningTool, EquipmentSlot.Weapon1));
    }

    [Fact]
    public void ASecondWeapon_FillsWeapon2_WhenWeapon1IsTaken()
    {
        Assert.Equal(EquipmentSlot.Weapon1, Equipment.Choose(EquipKind.Weapon, []));
        Assert.Equal(EquipmentSlot.Weapon2, Equipment.Choose(EquipKind.Weapon, [EquipmentSlot.Weapon1]));
        Assert.Equal(EquipmentSlot.Weapon1, Equipment.Choose(EquipKind.Weapon, [EquipmentSlot.Weapon2]));
    }

    [Fact]
    public void WithBothWeaponSlotsTaken_ChoosingReplacesWeapon1_ButAutoEquipDoesNot()
    {
        // The difference between double-clicking a third weapon, which swaps one out, and claiming one from
        // a job, which must never take off what somebody chose to wear (Joe, 16 October).
        EquipmentSlot[] full = [EquipmentSlot.Weapon1, EquipmentSlot.Weapon2];

        Assert.Equal(EquipmentSlot.Weapon1, Equipment.Choose(EquipKind.Weapon, full));
        Assert.Null(Equipment.FirstEmpty(EquipKind.Weapon, full));
    }

    [Fact]
    public void AnOccupiedOtherSlot_DoesNotStopAToolGoingIn()
    {
        Assert.Equal(
            EquipmentSlot.MiningTool,
            Equipment.FirstEmpty(EquipKind.MiningTool, [EquipmentSlot.Weapon1, EquipmentSlot.Head]));
        Assert.Null(Equipment.FirstEmpty(EquipKind.MiningTool, [EquipmentSlot.MiningTool]));
    }

    [Theory]
    [InlineData(ItemCategory.Weapon, EquipKind.Weapon, true)]
    [InlineData(ItemCategory.Armor, EquipKind.Head, true)]
    [InlineData(ItemCategory.Armor, EquipKind.Body, true)]
    [InlineData(ItemCategory.Armor, EquipKind.Legs, true)]
    [InlineData(ItemCategory.Tool, EquipKind.MiningTool, true)]
    [InlineData(ItemCategory.Tool, EquipKind.Weapon, false)]
    [InlineData(ItemCategory.Weapon, EquipKind.Head, false)]
    [InlineData(ItemCategory.Armor, EquipKind.MiningTool, false)]
    [InlineData(ItemCategory.Raw, EquipKind.MiningTool, false)]
    public void WhatACategoryMayEquipAs(ItemCategory category, EquipKind kind, bool allowed) =>
        Assert.Equal(allowed, Equipment.CategoryMayBe(category, kind));

    [Fact]
    public void EveryEquippableCategory_HasSomeKindItMayBe_AndNoOtherCategoryHasAny()
    {
        // Otherwise the validator would demand an equipsAs that no value satisfies, or allow one on ore.
        foreach (ItemCategory category in Enum.GetValues<ItemCategory>())
        {
            bool anyKind = Enum.GetValues<EquipKind>().Any(k => Equipment.CategoryMayBe(category, k));

            Assert.Equal(Equipment.IsEquippable(category), anyKind);
        }
    }
}
