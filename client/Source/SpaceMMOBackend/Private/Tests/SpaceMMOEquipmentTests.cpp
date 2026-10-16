#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "SpaceMMOBackendProtocol.h"
#include "SpaceMMOEquipmentScreen.h"
#include "SpaceMMOInventoryScreen.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Task 193 on the client: what is worn is read, shown in its slot, kept out of the inventory list, and asked
 * for in the server's words.
 *
 * <strong>The inventory is the server's own answer.</strong> RealEquipped is /characters/{id}/inventory as
 * the API answered it on 16 October, with a crude mining laser equipped, captured from the endpoint test that
 * equips one. MiningTool is 5 as a slot and 4 as a kind there; a client that confused the two enums would
 * pass every test built by hand and put the laser in Weapon 2's box (CLAUDE.md, "feed in the real value").
 */
namespace SpaceMMOEquipmentTests
{
	const TCHAR* RealEquipped = TEXT(R"({"stacks":[],"items":[{"inventoryId":2,"id":1,"itemDefId":1,"itemKey":"crude_mining_laser","name":"Crude Mining Laser","condition":100,"kind":3,"stationId":null,"category":4,"deployed":false,"style":0,"equippedSlot":5,"equipsAs":4}],"containers":[{"inventoryId":1,"kind":0,"stationId":null,"capacityM3":6},{"inventoryId":2,"kind":3,"stationId":null,"capacityM3":0}]})");

	/** The same laser before it was put on: the shape the server sends for one in the pockets. */
	const TCHAR* RealCarried = TEXT(R"({"stacks":[],"items":[{"inventoryId":1,"id":1,"itemDefId":1,"itemKey":"crude_mining_laser","name":"Crude Mining Laser","condition":100,"kind":0,"stationId":null,"category":4,"deployed":false,"style":0,"equippedSlot":null,"equipsAs":4}],"containers":[{"inventoryId":1,"kind":0,"stationId":null,"capacityM3":6}]})");

	FBackendItemInstance Worn(const int64 Id, const TCHAR* Name, const EBackendEquipmentSlot Where, const EBackendEquipKind Kind)
	{
		FBackendItemInstance Instance;
		Instance.Id = Id;
		Instance.InventoryId = 9;
		Instance.Name = Name;
		Instance.Condition = 100;
		Instance.Kind = EBackendInventoryKind::Equipped;
		Instance.bEquipped = true;
		Instance.EquippedSlot = Where;
		Instance.bEquippable = true;
		Instance.EquipsAs = Kind;

		return Instance;
	}

	FSpaceMMOInventoryLine Line(const EBackendEquipKind Kind, const bool bReachable = true)
	{
		FSpaceMMOInventoryLine Result;
		Result.InventoryId = 1;
		Result.InstanceId = 77;
		Result.Quantity = 1;
		Result.Label = TEXT("Something worn");
		Result.bReachable = bReachable;
		Result.bEquippable = true;
		Result.EquipsAs = Kind;

		return Result;
	}

	TSharedPtr<FJsonObject> Body(const FString& Json)
	{
		TSharedPtr<FJsonObject> Object;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Object);

		return Object;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOEquipmentReadsTheServersWordsTest,
	"SpaceMMO.Equipment.ReadsTheServersWords",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOEquipmentReadsTheServersWordsTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMOEquipmentTests;

	TArray<FBackendInventoryItem> Stacks;
	TArray<FBackendItemInstance> Instances;
	TArray<FBackendInventoryContainer> Containers;

	TestTrue(TEXT("The real inventory parses"), FSpaceMMOBackendProtocol::ParseInventory(RealEquipped, Stacks, Instances, Containers));

	if (!TestEqual(TEXT("One laser"), Instances.Num(), 1) || !TestEqual(TEXT("Two containers"), Containers.Num(), 2))
	{
		return false;
	}

	// Kind 3 is the Equipped container. Before this, the client clamped anything past 2 to the pockets,
	// so a worn laser would have been listed as carried and the Equipped container as a second pair of pockets.
	TestEqual(TEXT("The laser is in the Equipped container"), Instances[0].Kind, EBackendInventoryKind::Equipped);
	TestEqual(TEXT("Which is listed as such"), Containers[1].Kind, EBackendInventoryKind::Equipped);

	TestTrue(TEXT("It is worn"), Instances[0].bEquipped);
	TestEqual(TEXT("In the mining tool slot, 5"), Instances[0].EquippedSlot, EBackendEquipmentSlot::MiningTool);
	TestTrue(TEXT("It is a thing worn at all"), Instances[0].bEquippable);
	TestEqual(TEXT("As a mining tool, 4"), Instances[0].EquipsAs, EBackendEquipKind::MiningTool);

	// And it lands in the mining box, not Weapon 2.
	const TArray<FSpaceMMOEquipmentSlotView> Views = USpaceMMOEquipmentScreen::Build(Instances);

	TestEqual(TEXT("Every slot has a view"), Views.Num(), 6);

	for (const FSpaceMMOEquipmentSlotView& View : Views)
	{
		const bool bMining = View.Slot == EBackendEquipmentSlot::MiningTool;

		TestEqual(*FString::Printf(TEXT("%s holds the laser only if it is the mining slot"), *View.SlotName),
			View.InstanceId, bMining ? int64(1) : int64(0));
	}

	// Null means not worn, and is read as such rather than as slot zero, Weapon 1.
	TArray<FBackendItemInstance> Loose;
	TestTrue(TEXT("The carried laser parses"), FSpaceMMOBackendProtocol::ParseInventory(RealCarried, Stacks, Loose, Containers));

	if (TestEqual(TEXT("One laser"), Loose.Num(), 1))
	{
		TestFalse(TEXT("A null slot is not worn"), Loose[0].bEquipped);
		TestTrue(TEXT("But still wearable"), Loose[0].bEquippable);
		TestTrue(TEXT("And no slot shows it"),
			USpaceMMOEquipmentScreen::Build(Loose).ContainsByPredicate([](const FSpaceMMOEquipmentSlotView& View) { return !View.IsEmpty(); }) == false);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOEquipmentKeepsWornGearOutOfTheInventoryTest,
	"SpaceMMO.Equipment.KeepsWornGearOutOfTheInventory",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOEquipmentKeepsWornGearOutOfTheInventoryTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMOEquipmentTests;

	TArray<FBackendInventoryItem> Stacks;
	TArray<FBackendItemInstance> Instances;
	TArray<FBackendInventoryContainer> Containers;

	FSpaceMMOBackendProtocol::ParseInventory(RealEquipped, Stacks, Instances, Containers);

	// Joe, 16 October: worn gear is not carried. It is on the equipment screen, so the inventory lists the
	// pockets, empty, and neither the laser nor a heading for where it is.
	const TArray<FSpaceMMOInventoryLine> Lines = USpaceMMOInventoryScreen::Build(Stacks, Instances, Containers, {}, 0);

	TestFalse(TEXT("No line names the worn laser"),
		Lines.ContainsByPredicate([](const FSpaceMMOInventoryLine& L) { return L.InstanceId == 1; }));
	TestFalse(TEXT("No group for the Equipped container"),
		Lines.ContainsByPredicate([](const FSpaceMMOInventoryLine& L) { return L.InventoryId == 2; }));
	TestTrue(TEXT("The pockets are still there to drop into"),
		Lines.ContainsByPredicate([](const FSpaceMMOInventoryLine& L) { return L.bIsHeading && L.InventoryId == 1; }));

	// A laser in the pockets is listed, and carries what a double-click and a drop onto a slot need.
	TArray<FBackendItemInstance> Loose;
	FSpaceMMOBackendProtocol::ParseInventory(RealCarried, Stacks, Loose, Containers);

	const TArray<FSpaceMMOInventoryLine> Carried = USpaceMMOInventoryScreen::Build(Stacks, Loose, Containers, {}, 0);
	const FSpaceMMOInventoryLine* Laser = Carried.FindByPredicate([](const FSpaceMMOInventoryLine& L) { return L.InstanceId == 1; });

	if (TestNotNull(TEXT("The carried laser is listed"), Laser))
	{
		TestTrue(TEXT("As something worn"), Laser->bEquippable);
		TestEqual(TEXT("As a mining tool"), Laser->EquipsAs, EBackendEquipKind::MiningTool);
		TestTrue(TEXT("Which the mining slot takes"), USpaceMMOEquipmentScreen::CanEquip(*Laser, EBackendEquipmentSlot::MiningTool));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOEquipmentSlotRulesMirrorTheServerTest,
	"SpaceMMO.Equipment.SlotRulesMirrorTheServer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOEquipmentSlotRulesMirrorTheServerTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMOEquipmentTests;

	// The server's Equipment.Fits, as a table. A slot that lit for a drop the server then refuses is a drag
	// that looked like it worked.
	const EBackendEquipmentSlot Slots[] = {
		EBackendEquipmentSlot::Weapon1, EBackendEquipmentSlot::Weapon2, EBackendEquipmentSlot::Head,
		EBackendEquipmentSlot::Body, EBackendEquipmentSlot::Legs, EBackendEquipmentSlot::MiningTool,
	};

	struct FFitRow
	{
		EBackendEquipKind Key;
		TArray<EBackendEquipmentSlot> Value;
	};

	const FFitRow Expected[] = {
		{EBackendEquipKind::Weapon, {EBackendEquipmentSlot::Weapon1, EBackendEquipmentSlot::Weapon2}},
		{EBackendEquipKind::Head, {EBackendEquipmentSlot::Head}},
		{EBackendEquipKind::Body, {EBackendEquipmentSlot::Body}},
		{EBackendEquipKind::Legs, {EBackendEquipmentSlot::Legs}},
		{EBackendEquipKind::MiningTool, {EBackendEquipmentSlot::MiningTool}},
	};

	for (const auto& Row : Expected)
	{
		for (const EBackendEquipmentSlot Where : Slots)
		{
			TestEqual(
				*FString::Printf(TEXT("Kind %d in %s"), static_cast<int32>(Row.Key), *USpaceMMOEquipmentScreen::SlotName(Where)),
				USpaceMMOEquipmentScreen::Fits(Row.Key, Where), Row.Value.Contains(Where));
		}
	}

	// A drop onto a slot: worn, of the right kind, and reachable. A laser in a hangar at another station
	// cannot be put on from here, and the slot does not light for it.
	TestTrue(TEXT("A rifle onto Weapon 2"), USpaceMMOEquipmentScreen::CanEquip(Line(EBackendEquipKind::Weapon), EBackendEquipmentSlot::Weapon2));
	TestFalse(TEXT("A rifle onto the head"), USpaceMMOEquipmentScreen::CanEquip(Line(EBackendEquipKind::Weapon), EBackendEquipmentSlot::Head));
	TestFalse(TEXT("Out of reach"), USpaceMMOEquipmentScreen::CanEquip(Line(EBackendEquipKind::MiningTool, false), EBackendEquipmentSlot::MiningTool));

	FSpaceMMOInventoryLine Ore = Line(EBackendEquipKind::Weapon);
	Ore.InstanceId = 0;
	Ore.ItemDefId = 2;
	Ore.bEquippable = false;
	TestFalse(TEXT("A stack of ore is never worn"), USpaceMMOEquipmentScreen::CanEquip(Ore, EBackendEquipmentSlot::Weapon1));

	// Between slots: a weapon between the two weapon slots, and nothing else.
	FSpaceMMOEquipmentSlotView Rifle;
	Rifle.Slot = EBackendEquipmentSlot::Weapon1;
	Rifle.InstanceId = 5;

	TestTrue(TEXT("Weapon 1 to Weapon 2"), USpaceMMOEquipmentScreen::CanMoveBetween(Rifle, EBackendEquipmentSlot::Weapon2));
	TestFalse(TEXT("Onto itself"), USpaceMMOEquipmentScreen::CanMoveBetween(Rifle, EBackendEquipmentSlot::Weapon1));
	TestFalse(TEXT("Into the mining slot"), USpaceMMOEquipmentScreen::CanMoveBetween(Rifle, EBackendEquipmentSlot::MiningTool));

	FSpaceMMOEquipmentSlotView Laser;
	Laser.Slot = EBackendEquipmentSlot::MiningTool;
	Laser.InstanceId = 6;
	TestFalse(TEXT("A laser into a weapon slot"), USpaceMMOEquipmentScreen::CanMoveBetween(Laser, EBackendEquipmentSlot::Weapon1));

	FSpaceMMOEquipmentSlotView Empty;
	Empty.Slot = EBackendEquipmentSlot::Weapon2;
	TestFalse(TEXT("An empty slot carries nothing"), USpaceMMOEquipmentScreen::CanMoveBetween(Empty, EBackendEquipmentSlot::Weapon1));

	// Taking off into a container: any one within reach. A hangar at another station is not.
	FSpaceMMOInventoryLine Hold = Line(EBackendEquipKind::Weapon);
	Hold.bIsHeading = true;
	Hold.InventoryId = 2;
	TestTrue(TEXT("Into the hold"), USpaceMMOInventoryScreen::CanTakeOffInto(Hold));

	FSpaceMMOInventoryLine Far = Hold;
	Far.bReachable = false;
	TestFalse(TEXT("Into a hangar elsewhere"), USpaceMMOInventoryScreen::CanTakeOffInto(Far));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOEquipmentFillsTheApprovedSlotsTest,
	"SpaceMMO.Equipment.FillsTheApprovedSlots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOEquipmentFillsTheApprovedSlotsTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMOEquipmentTests;

	// Joe's mock, 16 October: a Laser Rifle in Weapon 1, Weapon 2 empty, the laser in the mining slot.
	const TArray<FSpaceMMOEquipmentSlotView> Views = USpaceMMOEquipmentScreen::Build({
		Worn(10, TEXT("Laser Rifle"), EBackendEquipmentSlot::Weapon1, EBackendEquipKind::Weapon),
		Worn(11, TEXT("Crude Mining Laser"), EBackendEquipmentSlot::MiningTool, EBackendEquipKind::MiningTool),
	});

	if (!TestEqual(TEXT("Six slots"), Views.Num(), 6))
	{
		return false;
	}

	TestEqual(TEXT("Weapon 1 is named"), Views[0].SlotName, FString(TEXT("Weapon 1")));
	TestEqual(TEXT("And holds the rifle"), Views[0].ItemName, FString(TEXT("Laser Rifle")));
	TestTrue(TEXT("Weapon 2 is empty"), Views[1].IsEmpty());
	TestEqual(TEXT("The mining slot is called Mining, as in the mock"), Views[5].SlotName, FString(TEXT("Mining")));
	TestEqual(TEXT("And holds the laser"), Views[5].InstanceId, int64(11));

	// The mock's words, box by box: a weapon slot's name alone over its item, "— empty —" when there is
	// none; every other slot's name with a colon on the item's line, and a bare dash.
	TestEqual(TEXT("Weapon 1 alone"), USpaceMMOEquipmentScreen::NameLabel(EBackendEquipmentSlot::Weapon1), FString(TEXT("Weapon 1")));
	TestEqual(TEXT("Laser Rifle under it"), USpaceMMOEquipmentScreen::ItemLabel(Views[0]), FString(TEXT("Laser Rifle")));
	TestEqual(TEXT("Weapon 2 empty"), USpaceMMOEquipmentScreen::ItemLabel(Views[1]), FString(TEXT("— empty —")));
	TestEqual(TEXT("Head with a colon"), USpaceMMOEquipmentScreen::NameLabel(EBackendEquipmentSlot::Head), FString(TEXT("Head:")));
	TestEqual(TEXT("And a dash"), USpaceMMOEquipmentScreen::ItemLabel(Views[2]), FString(TEXT("—")));
	TestEqual(TEXT("Mining with a colon"), USpaceMMOEquipmentScreen::NameLabel(EBackendEquipmentSlot::MiningTool), FString(TEXT("Mining:")));
	TestEqual(TEXT("And its laser"), USpaceMMOEquipmentScreen::ItemLabel(Views[5]), FString(TEXT("Crude Mining Laser")));

	// Something in the Equipped container with no slot -- mid-refresh, or a slot this client does not know --
	// is shown nowhere rather than guessed into a box.
	FBackendItemInstance Unplaced = Worn(12, TEXT("Visor"), EBackendEquipmentSlot::Head, EBackendEquipKind::Head);
	Unplaced.bEquipped = false;

	TestTrue(TEXT("Unplaced is not shown"), USpaceMMOEquipmentScreen::Build({Unplaced})[2].IsEmpty());

	// And something with a slot that is not in the Equipped container is not worn either.
	FBackendItemInstance Stray = Worn(13, TEXT("Visor"), EBackendEquipmentSlot::Head, EBackendEquipKind::Head);
	Stray.Kind = EBackendInventoryKind::CharacterCarried;

	TestTrue(TEXT("Carried is not worn"), USpaceMMOEquipmentScreen::Build({Stray})[2].IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOEquipmentAsksInTheServersShapeTest,
	"SpaceMMO.Equipment.AsksInTheServersShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOEquipmentAsksInTheServersShapeTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMOEquipmentTests;

	// The field names EquipRequest and UnequipRequest bind, and the slot as the number the server reads.
	const TSharedPtr<FJsonObject> Named = Body(FSpaceMMOBackendProtocol::MakeEquipBody(42, EBackendEquipmentSlot::Weapon2));

	if (TestTrue(TEXT("Equip parses"), Named.IsValid()))
	{
		TestEqual(TEXT("The item"), static_cast<int64>(Named->GetNumberField(TEXT("itemInstanceId"))), int64(42));
		TestEqual(TEXT("Weapon 2 is 1"), static_cast<int32>(Named->GetNumberField(TEXT("slot"))), 1);
	}

	// A double-click names no slot, and sends none rather than null or zero, which would be Weapon 1.
	const TSharedPtr<FJsonObject> Anywhere = Body(FSpaceMMOBackendProtocol::MakeEquipBody(42, TOptional<EBackendEquipmentSlot>()));

	if (TestTrue(TEXT("Equip anywhere parses"), Anywhere.IsValid()))
	{
		TestFalse(TEXT("No slot sent"), Anywhere->HasField(TEXT("slot")));
	}

	const TSharedPtr<FJsonObject> Off = Body(FSpaceMMOBackendProtocol::MakeUnequipBody(EBackendEquipmentSlot::MiningTool, 7));

	if (TestTrue(TEXT("Unequip parses"), Off.IsValid()))
	{
		TestEqual(TEXT("The mining slot is 5"), static_cast<int32>(Off->GetNumberField(TEXT("slot"))), 5);
		TestEqual(TEXT("Into the hold"), static_cast<int64>(Off->GetNumberField(TEXT("toInventoryId"))), int64(7));
	}

	const TSharedPtr<FJsonObject> Pockets = Body(FSpaceMMOBackendProtocol::MakeUnequipBody(EBackendEquipmentSlot::Head, 0));

	if (TestTrue(TEXT("Unequip to the pockets parses"), Pockets.IsValid()))
	{
		TestFalse(TEXT("No destination sent"), Pockets->HasField(TEXT("toInventoryId")));
	}

	return true;
}

#endif
