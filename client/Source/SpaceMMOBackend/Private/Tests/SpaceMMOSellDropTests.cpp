#include "Misc/AutomationTest.h"
#include "SpaceMMOInventoryScreen.h"
#include "SpaceMMOStationOverlay.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FSpaceMMOInventoryLine Stack(const int32 ItemDefId, const int32 Quantity)
	{
		FSpaceMMOInventoryLine Line;
		Line.InventoryId = 4;
		Line.ItemDefId = ItemDefId;
		Line.Quantity = Quantity;
		Line.Label = TEXT("Ferrite Ore");
		Line.bReachable = true;

		return Line;
	}
}

/** Task 116. Dropping a stack onto the market lists it, or says why it cannot. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOSellDropAcceptsAReachableStackTest,
	"SpaceMMO.HUD.SellDropAcceptsAReachableStack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOSellDropAcceptsAReachableStackTest::RunTest(const FString& Parameters)
{
	TestEqual(
		TEXT("An ordinary stack is accepted"),
		USpaceMMOStationOverlay::RefuseSellDrop(Stack(9, 120), true),
		FString());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOSellDropSaysWhyItRefusedTest,
	"SpaceMMO.HUD.SellDropSaysWhyItRefused",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOSellDropSaysWhyItRefusedTest::RunTest(const FString& Parameters)
{
	// Every one of these is something the player did deliberately. A silent refusal is
	// indistinguishable from a drop that missed the panel, so each has to say something.
	auto Refused = [this](const TCHAR* What, const FSpaceMMOInventoryLine& Line, const bool bInCatalogue)
	{
		const FString Reason = USpaceMMOStationOverlay::RefuseSellDrop(Line, bInCatalogue);

		TestFalse(What, Reason.IsEmpty());
	};

	FSpaceMMOInventoryLine Heading = Stack(9, 120);
	Heading.bIsHeading = true;
	Refused(TEXT("A whole container"), Heading, true);

	// A hull or a mining laser: one object with its own condition, where the book moves quantities.
	// This is the same line the tradeable catalogue is drawn on.
	FSpaceMMOInventoryLine Instance = Stack(0, 1);
	Instance.InstanceId = 77;
	Refused(TEXT("An instance"), Instance, true);

	// The rule the API enforces rather than a guess at it, and the same one that dims the row.
	FSpaceMMOInventoryLine Away = Stack(9, 120);
	Away.bReachable = false;
	Refused(TEXT("Another station's hangar"), Away, true);

	// Nobody here trades it, so there is no book to list it on.
	Refused(TEXT("Outside the catalogue"), Stack(9, 120), false);

	// And an empty stack, which would place an order for nothing.
	Refused(TEXT("An empty stack"), Stack(9, 0), true);

	return true;
}

#endif
