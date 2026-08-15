#include "Misc/AutomationTest.h"
#include "SpaceMMOInventoryScreen.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Tests for the inventory screen's grouping, ordering and drop rules.
 *
 * The point of this screen is not what a character owns but <em>where it is</em>, so what is worth
 * testing is the grouping, the order, which goods are out of reach, and what may be dropped where —
 * none of which is visual, and all of which decides whether a hauler is told the truth.
 */

namespace
{
	/** Container ids, so the tests key on the same thing the screen does. */
	constexpr int64 CarriedId = 1;
	constexpr int64 HoldId = 2;
	constexpr int64 TychoHangarId = 10;
	constexpr int64 KeplerHangarId = 20;

	constexpr int32 TychoStationId = 3;
	constexpr int32 KeplerStationId = 9;

	FBackendInventoryContainer Container(
		const int64 InventoryId,
		const EBackendInventoryKind Kind,
		const int32 StationId = 0)
	{
		FBackendInventoryContainer Result;
		Result.InventoryId = InventoryId;
		Result.Kind = Kind;
		Result.StationId = StationId;

		return Result;
	}

	FBackendInventoryItem Stack(
		const TCHAR* Name,
		const int32 Quantity,
		const int64 InventoryId,
		const EBackendInventoryKind Kind,
		const int32 StationId = 0)
	{
		FBackendInventoryItem Item;
		Item.Name = Name;
		Item.ItemKey = Name;
		Item.ItemDefId = 4;
		Item.Quantity = Quantity;
		Item.InventoryId = InventoryId;
		Item.Kind = Kind;
		Item.StationId = StationId;

		return Item;
	}

	FBackendItemInstance Instance(
		const TCHAR* Name,
		const int32 Condition,
		const int64 InventoryId,
		const EBackendInventoryKind Kind,
		const int32 StationId = 0)
	{
		FBackendItemInstance Item;
		Item.Name = Name;
		Item.ItemKey = Name;
		Item.Id = 700 + Condition;
		Item.Condition = Condition;
		Item.InventoryId = InventoryId;
		Item.Kind = Kind;
		Item.StationId = StationId;

		return Item;
	}

	FBackendStation Station(const int32 Id, const TCHAR* Name)
	{
		FBackendStation Result;
		Result.Id = Id;
		Result.Name = Name;

		return Result;
	}

	TArray<FString> HeadingsOf(const TArray<FSpaceMMOInventoryLine>& Lines)
	{
		TArray<FString> Headings;

		for (const FSpaceMMOInventoryLine& Line : Lines)
		{
			if (Line.bIsHeading)
			{
				Headings.Add(Line.Label);
			}
		}

		return Headings;
	}

	const FSpaceMMOInventoryLine* LineNamed(
		const TArray<FSpaceMMOInventoryLine>& Lines, const TCHAR* Label)
	{
		return Lines.FindByPredicate(
			[Label](const FSpaceMMOInventoryLine& Line) { return Line.Label == Label; });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOInventoryGroupsByContainerTest,
	"SpaceMMO.HUD.InventoryGroupsByContainer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOInventoryGroupsByContainerTest::RunTest(const FString& Parameters)
{
	const TArray<FBackendInventoryItem> Stacks = {
		Stack(TEXT("Silicate Dust"), 600, KeplerHangarId,
			EBackendInventoryKind::StationHangar, KeplerStationId),
		Stack(TEXT("Ferrite Ore"), 250, HoldId, EBackendInventoryKind::ShipHold),
		Stack(TEXT("Ferrite Ore"), 1480, TychoHangarId,
			EBackendInventoryKind::StationHangar, TychoStationId),
	};

	const TArray<FBackendItemInstance> Instances = {
		Instance(TEXT("Crude Mining Laser"), 87, CarriedId,
			EBackendInventoryKind::CharacterCarried),
	};

	const TArray<FBackendStation> Stations = {
		Station(TychoStationId, TEXT("Tycho Trading Hub")),
		Station(KeplerStationId, TEXT("Kepler Spaceport")),
	};

	// Docked at Tycho, so Kepler's hangar is real but out of reach.
	const TArray<FSpaceMMOInventoryLine> Lines =
		USpaceMMOInventoryScreen::Build(Stacks, Instances, {}, Stations, TychoStationId);

	// Carried, hold, the hangar being stood in, then everywhere else. What is to hand goes at the
	// top, which is the order the questions get asked in.
	const TArray<FString> Expected = {
		TEXT("CARRIED"),
		TEXT("SHIP HOLD"),
		TEXT("HANGAR — Tycho Trading Hub"),
		TEXT("HANGAR — Kepler Spaceport"),
	};

	TestEqual(TEXT("Grouped and ordered by container"), HeadingsOf(Lines), Expected);

	// A hangar at another station is listed and dimmed rather than hidden. The API refuses transfers
	// from elsewhere, so it is genuinely unusable -- but ore two planets away is exactly what a
	// hauling game wants a player to feel, and hiding it would make the goods simply vanish.
	for (const FSpaceMMOInventoryLine& Line : Lines)
	{
		if (Line.Label.Contains(TEXT("Kepler")) || Line.Label == TEXT("Silicate Dust"))
		{
			TestFalse(TEXT("Goods at another station are out of reach"), Line.bReachable);
		}
		else
		{
			TestTrue(TEXT("Everything else is reachable"), Line.bReachable);
		}
	}

	// Every line carries its container, headings included. That is what makes a whole group a drop
	// target without a widget per group to drop onto.
	const FSpaceMMOInventoryLine* Heading = LineNamed(Lines, TEXT("SHIP HOLD"));

	TestTrue(TEXT("The hold's heading knows its container"),
		Heading != nullptr && Heading->InventoryId == HoldId);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOInventoryListsEmptyContainersTest,
	"SpaceMMO.HUD.InventoryListsEmptyContainers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOInventoryListsEmptyContainersTest::RunTest(const FString& Parameters)
{
	// A container holding nothing must still appear, because it is where a first haul goes and no
	// list of contents can mention it. This is the case that made drag-to-transfer unbuildable
	// before the API listed containers separately.
	const TArray<FBackendInventoryContainer> Containers = {
		Container(CarriedId, EBackendInventoryKind::CharacterCarried),
		Container(TychoHangarId, EBackendInventoryKind::StationHangar, TychoStationId),
	};

	const TArray<FBackendInventoryItem> Stacks = {
		Stack(TEXT("Ferrite Ore"), 1480, TychoHangarId,
			EBackendInventoryKind::StationHangar, TychoStationId),
	};

	const TArray<FSpaceMMOInventoryLine> Lines = USpaceMMOInventoryScreen::Build(
		Stacks, {}, Containers, { Station(TychoStationId, TEXT("Tycho Trading Hub")) },
		TychoStationId);

	TestEqual(TEXT("Both containers appear"), HeadingsOf(Lines).Num(), 2);

	const FSpaceMMOInventoryLine* Carried = LineNamed(Lines, TEXT("CARRIED"));

	TestTrue(TEXT("The empty one is there"), Carried != nullptr);
	TestTrue(TEXT("And can be dropped into"),
		Carried != nullptr && Carried->InventoryId == CarriedId && Carried->bReachable);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOInventoryDropRulesTest,
	"SpaceMMO.HUD.InventoryDropRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOInventoryDropRulesTest::RunTest(const FString& Parameters)
{
	const TArray<FSpaceMMOInventoryLine> Lines = USpaceMMOInventoryScreen::Build(
		{
			Stack(TEXT("Ferrite Ore"), 1480, TychoHangarId,
				EBackendInventoryKind::StationHangar, TychoStationId),
			Stack(TEXT("Silicate Dust"), 600, KeplerHangarId,
				EBackendInventoryKind::StationHangar, KeplerStationId),
		},
		{},
		{ Container(CarriedId, EBackendInventoryKind::CharacterCarried) },
		{
			Station(TychoStationId, TEXT("Tycho Trading Hub")),
			Station(KeplerStationId, TEXT("Kepler Spaceport")),
		},
		TychoStationId);

	const FSpaceMMOInventoryLine* Ore = LineNamed(Lines, TEXT("Ferrite Ore"));
	const FSpaceMMOInventoryLine* Distant = LineNamed(Lines, TEXT("Silicate Dust"));
	const FSpaceMMOInventoryLine* Carried = LineNamed(Lines, TEXT("CARRIED"));
	const FSpaceMMOInventoryLine* Hangar = LineNamed(Lines, TEXT("HANGAR — Tycho Trading Hub"));

	TestTrue(TEXT("Fixtures found"),
		Ore != nullptr && Distant != nullptr && Carried != nullptr && Hangar != nullptr);

	if (Ore == nullptr || Distant == nullptr || Carried == nullptr || Hangar == nullptr)
	{
		return false;
	}

	// The move this whole feature exists for: ore out of the hangar you are standing in, into your
	// pockets.
	TestTrue(TEXT("Hangar to carried"), USpaceMMOInventoryScreen::CanDrop(*Ore, *Carried));

	// Back where it already is. Harmless to send and confusing to watch: the server would move goods
	// from a place to itself and the screen would redraw identically, reading as a lost drop.
	TestFalse(TEXT("Not into its own container"), USpaceMMOInventoryScreen::CanDrop(*Ore, *Hangar));

	// Goods at a station the player is not docked at cannot be picked up, which is the rule the API
	// enforces rather than a guess at it.
	TestFalse(TEXT("Cannot drag from out of reach"),
		USpaceMMOInventoryScreen::CanDrop(*Distant, *Carried));

	// A heading is a destination, not cargo.
	TestFalse(TEXT("Cannot drag a heading"), USpaceMMOInventoryScreen::CanDrop(*Carried, *Hangar));

	// And nothing may be dropped into a container that is out of reach.
	const FSpaceMMOInventoryLine* Far = LineNamed(Lines, TEXT("HANGAR — Kepler Spaceport"));

	TestTrue(TEXT("Distant hangar listed"), Far != nullptr);
	TestFalse(TEXT("Cannot drop somewhere unreachable"),
		Far != nullptr && USpaceMMOInventoryScreen::CanDrop(*Ore, *Far));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOInventorySeparatesStacksFromInstancesTest,
	"SpaceMMO.HUD.InventorySeparatesStacksFromInstances",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOInventorySeparatesStacksFromInstancesTest::RunTest(const FString& Parameters)
{
	// Two lasers at different condition are two things, and a quantity of 2 would say they are one.
	// ADR-0006 insures each instance against its own acquisition value, so this is load-bearing.
	const TArray<FBackendItemInstance> Instances = {
		Instance(TEXT("Crude Mining Laser"), 40, CarriedId,
			EBackendInventoryKind::CharacterCarried),
		Instance(TEXT("Crude Mining Laser"), 87, CarriedId,
			EBackendInventoryKind::CharacterCarried),
	};

	const TArray<FSpaceMMOInventoryLine> Lines =
		USpaceMMOInventoryScreen::Build({}, Instances, {}, {}, 0);

	TestEqual(TEXT("A heading and both lasers"), Lines.Num(), 3);

	// Sorted by name then condition, so the same tool twice can be told apart and does not reorder
	// itself between refreshes.
	TestEqual(TEXT("Worn one first"), Lines[1].Amount, FString(TEXT("cond 40")));
	TestEqual(TEXT("Then the good one"), Lines[2].Amount, FString(TEXT("cond 87")));

	// An instance moves whole, so it carries no quantity to be asked about.
	TestTrue(TEXT("Instances are draggable"), Lines[1].CanDrag());
	TestTrue(TEXT("And identified by instance"), Lines[1].InstanceId != 0);

	// Quantities are grouped for reading; a hauler's hangar runs to five figures.
	const TArray<FSpaceMMOInventoryLine> Big = USpaceMMOInventoryScreen::Build(
		{ Stack(TEXT("Ferrite Ore"), 14800, HoldId, EBackendInventoryKind::ShipHold) },
		{}, {}, {}, 0);

	TestEqual(TEXT("Quantity is grouped"), Big[1].Amount, FString(TEXT("14,800")));
	TestEqual(TEXT("And kept as a number for the prompt"), Big[1].Quantity, 14800);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOInventorySaysWhenThereIsNothingTest,
	"SpaceMMO.HUD.InventorySaysWhenThereIsNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOInventorySaysWhenThereIsNothingTest::RunTest(const FString& Parameters)
{
	// A blank rectangle reads as a screen that failed to load rather than as a character who owns
	// nothing, and a new player sees this one first.
	const TArray<FSpaceMMOInventoryLine> Lines =
		USpaceMMOInventoryScreen::Build({}, {}, {}, {}, 0);

	TestEqual(TEXT("One line"), Lines.Num(), 1);
	TestFalse(TEXT("Which is not a heading"), Lines[0].bIsHeading);
	TestTrue(TEXT("And says so"), Lines[0].Label.Contains(TEXT("Nothing")));

	// A hangar whose station has not been fetched yet is still somewhere. Naming it by number tells
	// a player more than an empty heading, which reads as a bug.
	const TArray<FSpaceMMOInventoryLine> Unknown = USpaceMMOInventoryScreen::Build(
		{ Stack(TEXT("Ferrite Ore"), 10, 41, EBackendInventoryKind::StationHangar, 41) },
		{}, {}, {}, 0);

	TestTrue(TEXT("Unknown station named by id"), Unknown[0].Label.Contains(TEXT("41")));

	// Undocked: every hangar is out of reach, including the one whose id happens to be zero.
	TestFalse(TEXT("Nothing is reachable while undocked"), Unknown[0].bReachable);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
