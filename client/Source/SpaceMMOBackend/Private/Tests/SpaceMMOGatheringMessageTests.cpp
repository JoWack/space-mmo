#include "Misc/AutomationTest.h"
#include "SpaceMMOGatheringComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOGatherMessageReportsAYieldTest,
	"SpaceMMO.Gathering.MessageReportsAYield",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOGatherMessageReportsAYieldTest::RunTest(const FString& Parameters)
{
	const FString Message =
		USpaceMMOGatheringComponent::FormatGatherMessage(20, 100, 180, TEXT("Ferrite Ore"));

	// Every number the server decided has to survive to the screen. This whole feature ran
	// correctly for a whole session while telling the player nothing, so the wording is the
	// feature as far as anyone playing is concerned.
	TestTrue(TEXT("Says how much"), Message.Contains(TEXT("20")));
	TestTrue(TEXT("Names the item"), Message.Contains(TEXT("Ferrite Ore")));
	TestTrue(TEXT("Says the xp"), Message.Contains(TEXT("100")));
	TestTrue(TEXT("Says what is left"), Message.Contains(TEXT("180")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOGatherMessageDistinguishesTooSoonFromSpentTest,
	"SpaceMMO.Gathering.MessageDistinguishesTooSoonFromSpent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOGatherMessageDistinguishesTooSoonFromSpentTest::RunTest(const FString& Parameters)
{
	// Both arrive as a 200 with a zero quantity, and they mean opposite things: one is worth
	// waiting out, the other means walk away. Telling a player the wrong one leaves them standing
	// at a dead rock pressing a key.
	const FString TooSoon =
		USpaceMMOGatheringComponent::FormatGatherMessage(0, 0, 180, TEXT("Ferrite Ore"));

	const FString Spent =
		USpaceMMOGatheringComponent::FormatGatherMessage(0, 0, 0, TEXT("Ferrite Ore"));

	TestNotEqual(TEXT("The two refusals read differently"), TooSoon, Spent);

	TestTrue(TEXT("Too soon suggests waiting"), TooSoon.Contains(TEXT("moment")));
	TestTrue(TEXT("Spent says the deposit is done"), Spent.Contains(TEXT("worked out")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOGatherMessageSurvivesAMissingItemNameTest,
	"SpaceMMO.Gathering.MessageSurvivesAMissingItemName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOGatherMessageSurvivesAMissingItemNameTest::RunTest(const FString& Parameters)
{
	// The name comes from the deposit payload, and a deposit whose itemName field was absent
	// parses fine — the parser only insists on a direction. An empty name must not produce
	// "+20    (+100 xp)", which reads as a bug rather than as ore.
	const FString Message =
		USpaceMMOGatheringComponent::FormatGatherMessage(20, 100, 180, FString());

	TestTrue(TEXT("Falls back to a word"), Message.Contains(TEXT("ore")));
	TestTrue(TEXT("Still says how much"), Message.Contains(TEXT("20")));

	return true;
}

#endif
