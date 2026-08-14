#include "Misc/AutomationTest.h"
#include "SpaceMMOInventoryScreen.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Tests for the inventory screen's grouping and ordering.
 *
 * The point of this screen is not what a character owns but <em>where it is</em>, so what is worth
 * testing is the grouping, the order, and which goods are marked out of reach — none of which is
 * visual, and all of which decides whether a hauler is told the truth.
 */

namespace
{
	FBackendInventoryItem Stack(
		const TCHAR* Name,
		const int32 Quantity,
		const EBackendInventoryKind Kind,
		const int32 StationId = 0)
	{
		FBackendInventoryItem Item;
		Item.Name = Name;
		Item.ItemKey = Name;
		Item.Quantity = Quantity;
		Item.Kind = Kind;
		Item.StationId = StationId;

		return Item;
	}

	FBackendItemInstance Instance(
		const TCHAR* Name,
		const int32 Condition,
		const EBackendInventoryKind Kind,
		const int32 StationId = 0)
	{
		FBackendItemInstance Item;
		Item.Name = Name;
		Item.ItemKey = Name;
		Item.Condition = Condition;
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

	/** The heading of the group a line belongs to, for asserting order without hard-coding indices. */
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
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOInventoryGroupsByContainerTest,
	"SpaceMMO.HUD.InventoryGroupsByContainer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOInventoryGroupsByContainerTest::RunTest(const FString& Parameters)
{
	const TArray<FBackendInventoryItem> Stacks = {
		Stack(TEXT("Silicate Dust"), 600, EBackendInventoryKind::StationHangar, 9),
		Stack(TEXT("Ferrite Ore"), 250, EBackendInventoryKind::ShipHold),
		Stack(TEXT("Ferrite Ore"), 1480, EBackendInventoryKind::StationHangar, 3),
	};

	const TArray<FBackendItemInstance> Instances = {
		Instance(TEXT("Crude Mining Laser"), 87, EBackendInventoryKind::CharacterCarried),
	};

	const TArray<FBackendStation> Stations = {
		Station(3, TEXT("Tycho Trading Hub")),
		Station(9, TEXT("Kepler Spaceport")),
	};

	// Docked at Tycho, so Kepler's hangar is real but out of reach.
	const TArray<FSpaceMMOInventoryLine> Lines =
		USpaceMMOInventoryScreen::Build(Stacks, Instances, Stations, 3);

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
		Instance(TEXT("Crude Mining Laser"), 40, EBackendInventoryKind::CharacterCarried),
		Instance(TEXT("Crude Mining Laser"), 87, EBackendInventoryKind::CharacterCarried),
	};

	const TArray<FSpaceMMOInventoryLine> Lines =
		USpaceMMOInventoryScreen::Build({}, Instances, {}, 0);

	TestEqual(TEXT("A heading and both lasers"), Lines.Num(), 3);

	// Sorted by name then condition, so the same tool twice can be told apart and does not reorder
	// itself between refreshes.
	TestEqual(TEXT("Worn one first"), Lines[1].Amount, FString(TEXT("cond 40")));
	TestEqual(TEXT("Then the good one"), Lines[2].Amount, FString(TEXT("cond 87")));

	// Quantities are grouped for reading; a hauler's hangar runs to five figures.
	const TArray<FSpaceMMOInventoryLine> Big = USpaceMMOInventoryScreen::Build(
		{ Stack(TEXT("Ferrite Ore"), 14800, EBackendInventoryKind::ShipHold) }, {}, {}, 0);

	TestEqual(TEXT("Quantity is grouped"), Big[1].Amount, FString(TEXT("14,800")));

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
		USpaceMMOInventoryScreen::Build({}, {}, {}, 0);

	TestEqual(TEXT("One line"), Lines.Num(), 1);
	TestFalse(TEXT("Which is not a heading"), Lines[0].bIsHeading);
	TestTrue(TEXT("And says so"), Lines[0].Label.Contains(TEXT("Nothing")));

	// A hangar whose station has not been fetched yet is still somewhere. Naming it by number tells
	// a player more than an empty heading, which reads as a bug.
	const TArray<FSpaceMMOInventoryLine> Unknown = USpaceMMOInventoryScreen::Build(
		{ Stack(TEXT("Ferrite Ore"), 10, EBackendInventoryKind::StationHangar, 41) }, {}, {}, 0);

	TestTrue(TEXT("Unknown station named by id"), Unknown[0].Label.Contains(TEXT("41")));

	// Undocked: every hangar is out of reach, including the one whose id happens to be zero.
	TestFalse(TEXT("Nothing is reachable while undocked"), Unknown[0].bReachable);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
