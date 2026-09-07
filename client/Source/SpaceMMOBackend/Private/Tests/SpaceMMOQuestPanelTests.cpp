#include "Misc/AutomationTest.h"
#include "SpaceMMOPanelTestHelpers.h"
#include "SpaceMMOBackendProtocol.h"
#include "SpaceMMOPlayerController.h"

#if WITH_DEV_AUTOMATION_TESTS

// Shared, because a unity build can put two of these files in one translation
// unit, where two anonymous namespaces are the same namespace and a second copy
// of a helper is a redefinition.
using SpaceMMOPanelTests::AnyLineContains;
using SpaceMMOPanelTests::IndexOfLineContaining;

namespace
{
	FBackendJournalEntry MakeEntry(
		const TCHAR* Name,
		const EBackendQuestState State,
		const int32 Progress = 0,
		const int32 Required = 0)
	{
		FBackendJournalEntry Entry;
		Entry.QuestKey = FString(Name).ToLower();
		Entry.Name = Name;
		Entry.State = State;
		Entry.StepProgress = Progress;
		Entry.StepRequired = Required;
		Entry.StepDescription = TEXT("Collect scrap from the surface.");

		return Entry;
	}

	FBackendAvailableQuest MakeAvailable(const TCHAR* Name)
	{
		FBackendAvailableQuest Quest;
		Quest.QuestKey = FString(Name).ToLower();
		Quest.Name = Name;

		return Quest;
	}

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOQuestPanelShowsProgressTest,
	"SpaceMMO.Quests.PanelShowsProgress",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOQuestPanelShowsProgressTest::RunTest(const FString& Parameters)
{
	const TArray<FBackendJournalEntry> Journal{
		MakeEntry(TEXT("Salvage Rights"), EBackendQuestState::InProgress, 6, 10),
	};

	const TArray<FString> Lines = ASpaceMMOPlayerController::BuildQuestPanel(
		Journal, TArray<FBackendAvailableQuest>());

	TestTrue(TEXT("Names the quest"), AnyLineContains(Lines, TEXT("Salvage Rights")));
	TestTrue(TEXT("Shows progress"), AnyLineContains(Lines, TEXT("6/10")));

	// The authored line is what tells a player what to actually do. A count with no description
	// says how far through something they are without saying what it is.
	TestTrue(TEXT("Shows the step"), AnyLineContains(Lines, TEXT("Collect scrap")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOQuestPanelHidesFinishedQuestsTest,
	"SpaceMMO.Quests.PanelHidesFinishedQuests",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOQuestPanelHidesFinishedQuestsTest::RunTest(const FString& Parameters)
{
	// A journal accumulates every quest a character has ever taken. Listing the finished ones
	// buries the one line saying what to do next, which is the only line being looked for.
	const TArray<FBackendJournalEntry> Journal{
		MakeEntry(TEXT("Old News"), EBackendQuestState::Completed),
		MakeEntry(TEXT("Abandoned Thing"), EBackendQuestState::Abandoned),
		MakeEntry(TEXT("Salvage Rights"), EBackendQuestState::InProgress, 1, 10),
	};

	const TArray<FString> Lines = ASpaceMMOPlayerController::BuildQuestPanel(
		Journal, TArray<FBackendAvailableQuest>());

	TestTrue(TEXT("Keeps the active one"), AnyLineContains(Lines, TEXT("Salvage Rights")));
	TestFalse(TEXT("Drops the completed one"), AnyLineContains(Lines, TEXT("Old News")));
	TestFalse(TEXT("Drops the abandoned one"), AnyLineContains(Lines, TEXT("Abandoned Thing")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOQuestPanelMarksAHandInTest,
	"SpaceMMO.Quests.PanelMarksAHandIn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOQuestPanelMarksAHandInTest::RunTest(const FString& Parameters)
{
	const TArray<FBackendJournalEntry> Journal{
		MakeEntry(TEXT("An Errand"), EBackendQuestState::ReadyToTurnIn, 10, 10),
	};

	const TArray<FString> Lines = ASpaceMMOPlayerController::BuildQuestPanel(
		Journal, TArray<FBackendAvailableQuest>());

	// Finished work, unpaid. Rendering it as 10/10 alongside the unfinished ones would leave a
	// player waiting for a counter that is never going to move.
	TestTrue(TEXT("Says it is ready"), AnyLineContains(Lines, TEXT("READY TO HAND IN")));
	TestFalse(TEXT("Not shown as a count"), AnyLineContains(Lines, TEXT("10/10")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOQuestPanelSpeaksWhenEmptyTest,
	"SpaceMMO.Quests.PanelSpeaksWhenEmpty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOQuestPanelSpeaksWhenEmptyTest::RunTest(const FString& Parameters)
{
	// A brand-new character, and a character who has finished everything, look the same here.
	const TArray<FString> Lines = ASpaceMMOPlayerController::BuildQuestPanel(
		TArray<FBackendJournalEntry>(), { MakeAvailable(TEXT("Salvage Rights")) });

	TestTrue(TEXT("Says nothing is active"), AnyLineContains(Lines, TEXT("none active")));

	// Naming what could be taken is the entire route out of an empty journal: accepting needs a
	// key, and nothing else in the client knows any.
	TestTrue(TEXT("Offers what is available"), AnyLineContains(Lines, TEXT("Salvage Rights")));

	return true;
}

/**
 * A quest you have and a quest you could take are told apart.
 *
 * <strong>From a playtest, 7 September.</strong> Joe pressed the accept key on Salvage Rights and
 * was told "Nothing to accept". It was true: the quest had been accepted three weeks earlier and
 * was sitting at 0/10. The panel listed it as a bare line under a header advertising the accept
 * key, so a quest that was already his read exactly like one being offered, and the whole quest
 * system read as broken.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOQuestPanelSeparatesHeldFromOfferedTest,
	"SpaceMMO.Quests.PanelSeparatesHeldFromOffered",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOQuestPanelSeparatesHeldFromOfferedTest::RunTest(const FString& Parameters)
{
	const TArray<FBackendJournalEntry> Journal{
		MakeEntry(TEXT("Salvage Rights"), EBackendQuestState::InProgress, 0, 10),
	};

	const TArray<FString> Held = ASpaceMMOPlayerController::BuildQuestPanel(
		Journal, TArray<FBackendAvailableQuest>());

	TestTrue(TEXT("The held one is headed"), AnyLineContains(Held, TEXT("ACTIVE")));

	// The exact fault: with nothing on offer, nothing may suggest there is. The header advertised
	// a key that could not do anything, which is what invited pressing it.
	TestFalse(TEXT("No offer heading"), AnyLineContains(Held, TEXT("AVAILABLE")));
	TestFalse(TEXT("...and no hint for a key with nothing to do"), AnyLineContains(Held, TEXT("J accepts")));

	// Both at once, which is the case the headings exist for: two lines that would otherwise be
	// indistinguishable, one of them yours and one of them not.
	const TArray<FString> Both = ASpaceMMOPlayerController::BuildQuestPanel(
		Journal, { MakeAvailable(TEXT("First Tools")) });

	const int32 Active = IndexOfLineContaining(Both, TEXT("ACTIVE"));
	const int32 Offered = IndexOfLineContaining(Both, TEXT("AVAILABLE"));

	TestTrue(TEXT("Both headings appear"), Active != INDEX_NONE && Offered != INDEX_NONE);

	TestTrue(
		TEXT("What you hold comes before what you could take"),
		Active < Offered);

	TestTrue(
		TEXT("...and the held quest sits under the held heading"),
		IndexOfLineContaining(Both, TEXT("Salvage Rights")) > Active
			&& IndexOfLineContaining(Both, TEXT("Salvage Rights")) < Offered);

	TestTrue(
		TEXT("...and the offered one under the offer heading"),
		IndexOfLineContaining(Both, TEXT("First Tools")) > Offered);

	TestTrue(TEXT("The hint returns with something to accept"), AnyLineContains(Both, TEXT("J accepts")));

	return true;
}

/**
 * The refusal says which quest is in the way, and what would move it.
 *
 * "Nothing to accept" was true and unusable: the chain hands out one quest at a time, so having
 * nothing on offer is caused by holding the current one, and the message named neither.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOQuestAcceptRefusalNamesTheReasonTest,
	"SpaceMMO.Quests.AcceptRefusalNamesTheReason",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOQuestAcceptRefusalNamesTheReasonTest::RunTest(const FString& Parameters)
{
	const FString Running = ASpaceMMOPlayerController::AcceptRefusal({
		MakeEntry(TEXT("Salvage Rights"), EBackendQuestState::InProgress, 0, 10),
	});

	TestTrue(TEXT("Names the quest in the way"), Running.Contains(TEXT("Salvage Rights")));

	// The step, not just the name. Being told which quest is blocking says why the key did nothing
	// without saying what would ever change it.
	TestTrue(TEXT("...and what to go and do"), Running.Contains(TEXT("Collect scrap")));

	// Finished work waiting to be paid is a different instruction entirely: there is nothing left
	// to gather, and telling somebody to gather would send them back to a deposit for nothing.
	const FString Ready = ASpaceMMOPlayerController::AcceptRefusal({
		MakeEntry(TEXT("An Errand"), EBackendQuestState::ReadyToTurnIn, 10, 10),
	});

	TestTrue(TEXT("A finished quest asks to be handed in"), Ready.Contains(TEXT("hand it in")));
	TestFalse(TEXT("...and does not repeat the step"), Ready.Contains(TEXT("Collect scrap")));

	// Finished quests are not reasons. A character whose journal is all history has nothing in the
	// way, and naming an old quest would be a refusal about something that ended weeks ago.
	const FString Nothing = ASpaceMMOPlayerController::AcceptRefusal({
		MakeEntry(TEXT("Old News"), EBackendQuestState::Completed),
	});

	TestTrue(TEXT("History explains nothing"), Nothing.IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOQuestParsesTheJournalTest,
	"SpaceMMO.Quests.ParsesTheJournal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOQuestParsesTheJournalTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"([
		{
			"questKey": "intro_gather_scrap", "name": "Salvage Rights",
			"kind": 0, "state": 0, "stepOrdinal": 1, "completedAt": null,
			"stepDescription": "Collect 10 scrap.", "stepObjective": 0,
			"stepTargetKey": "scrap_alloy", "stepProgress": 6, "stepRequired": 10
		},
		{
			"questKey": "npc_errand", "name": "An Errand",
			"kind": 0, "state": 3, "stepOrdinal": 1, "completedAt": null,
			"stepDescription": null, "stepObjective": null,
			"stepTargetKey": null, "stepProgress": 0, "stepRequired": null
		}
	])");

	TArray<FBackendJournalEntry> Entries;

	TestTrue(TEXT("Parsed"), FSpaceMMOBackendProtocol::ParseJournal(Json, Entries));
	TestEqual(TEXT("Both entries"), Entries.Num(), 2);

	TestEqual(TEXT("Progress"), Entries[0].StepProgress, 6);
	TestEqual(TEXT("Required"), Entries[0].StepRequired, 10);
	TestEqual(TEXT("State"), Entries[0].State, EBackendQuestState::InProgress);

	// The state that arrives as 3. Mapping it to anything else would render finished work as
	// still in progress, or worse as abandoned.
	TestEqual(TEXT("Ready to turn in"), Entries[1].State, EBackendQuestState::ReadyToTurnIn);

	// Null step fields are the ordinary shape of a quest with no active step, not a parse failure.
	TestTrue(TEXT("No step description"), Entries[1].StepDescription.IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOQuestParsesAvailableTest,
	"SpaceMMO.Quests.ParsesAvailable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOQuestParsesAvailableTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"([
		{ "questKey": "intro_gather_scrap", "name": "Salvage Rights", "kind": 0 },
		{ "name": "Nameless", "kind": 0 }
	])");

	TArray<FBackendAvailableQuest> Quests;

	TestTrue(TEXT("Parsed"), FSpaceMMOBackendProtocol::ParseAvailableQuests(Json, Quests));

	// The keyless entry is dropped. Accepting names a quest by key, so listing one without a key
	// would offer the player something no keypress could ever take.
	TestEqual(TEXT("Kept the usable one"), Quests.Num(), 1);
	TestEqual(TEXT("Key"), Quests[0].QuestKey, FString(TEXT("intro_gather_scrap")));

	const FString Body =
		FSpaceMMOBackendProtocol::MakeAcceptQuestBody(11, TEXT("intro_gather_scrap"));

	TestTrue(TEXT("Names the character"), Body.Contains(TEXT("\"characterId\":11")));
	TestTrue(TEXT("Names the quest"), Body.Contains(TEXT("\"questKey\":\"intro_gather_scrap\"")));

	return true;
}

#endif
