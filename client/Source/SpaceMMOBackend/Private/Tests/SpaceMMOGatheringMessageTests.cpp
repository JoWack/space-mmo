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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOGatherToneMatchesTheWordingTest,
	"SpaceMMO.Gathering.ToneMatchesTheWording",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOGatherToneMatchesTheWordingTest::RunTest(const FString& Parameters)
{
	// The tone and the wording are decided separately and must agree, because a message reading
	// "+20 Ferrite Ore" in the colour of a refusal is worse than either mistake alone. Quantity is
	// the whole question: the server answers 200 either way and nothing else separates them.
	TestEqual(
		TEXT("A yield reads as a gain"),
		USpaceMMOGatheringComponent::GatherTone(20),
		ESpaceMMOMessageTone::Positive);

	// Both zero-quantity cases are warnings, including the one that is only a matter of waiting --
	// nothing was credited, and the player pressed a key expecting something.
	TestEqual(
		TEXT("Too soon reads as a warning"),
		USpaceMMOGatheringComponent::GatherTone(0),
		ESpaceMMOMessageTone::Warning);

	// Paired against the wording rather than asserted alone, so the two cannot drift apart: this
	// fails if either the tone or the text stops agreeing about what happened.
	const FString Yield =
		USpaceMMOGatheringComponent::FormatGatherMessage(20, 100, 180, TEXT("Ferrite Ore"));

	TestTrue(
		TEXT("The positive one is the one that says a quantity was gained"),
		Yield.Contains(TEXT("+20"))
			&& USpaceMMOGatheringComponent::GatherTone(20) == ESpaceMMOMessageTone::Positive);

	return true;
}


/**
 * A full pack is told apart from a cooldown and from a spent deposit.
 *
 * <strong>Zero has three reasons and only two of them are worth waiting out.</strong> This was
 * found in a playtest: capacity started binding, mining stopped, and the message said "give it a
 * moment" — so the answer looked like patience when it was in the player's own pockets. The only
 * way to discover otherwise was to move something to a hangar and try again.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOGatherMessageTellsAFullPackFromAWaitTest,
	"SpaceMMO.Gathering.MessageTellsAFullPackFromAWait",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOGatherMessageTellsAFullPackFromAWaitTest::RunTest(const FString& Parameters)
{
	const FString Full =
		USpaceMMOGatheringComponent::FormatGatherMessage(0, 0, 180, TEXT("Ferrite Ore"), true);

	const FString Waiting =
		USpaceMMOGatheringComponent::FormatGatherMessage(0, 0, 180, TEXT("Ferrite Ore"), false);

	TestNotEqual(
		TEXT("A full pack does not read as a cooldown"), Full, Waiting);

	TestTrue(
		TEXT("...and says what is actually wrong"),
		Full.Contains(TEXT("carrying")) || Full.Contains(TEXT("fit")));

	// Checked before the deposit, because a full pack is the player's problem wherever they are
	// standing. Being told a deposit is worked out while the answer is in your own pockets sends
	// somebody walking to another rock to meet the same wall.
	const FString FullAtASpentNode =
		USpaceMMOGatheringComponent::FormatGatherMessage(0, 0, 0, TEXT("Ferrite Ore"), true);

	TestEqual(
		TEXT("A full pack at a spent deposit still names the pack"), FullAtASpentNode, Full);

	// And a successful swing is unaffected, whatever the flag says: something arrived, so nothing
	// about room is worth saying.
	const FString Yield =
		USpaceMMOGatheringComponent::FormatGatherMessage(3, 15, 180, TEXT("Ferrite Ore"), true);

	TestTrue(TEXT("A yield still reports the yield"), Yield.Contains(TEXT("+3")));

	return true;
}

#endif
