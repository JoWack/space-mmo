#include "Misc/AutomationTest.h"
#include "SpaceMMOBackendProtocol.h"
#include "SpaceMMOPlayerController.h"

#if WITH_DEV_AUTOMATION_TESTS

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

	bool AnyLineContains(const TArray<FString>& Lines, const FString& Fragment)
	{
		return Lines.ContainsByPredicate(
			[&Fragment](const FString& Line) { return Line.Contains(Fragment); });
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
