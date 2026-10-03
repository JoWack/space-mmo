#include "Misc/AutomationTest.h"
#include "SpaceMMOBackendProtocol.h"
#include "SpaceMMOPlayerController.h"
#include "SpaceMMOStationWork.h"

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

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOQuestPanelShowsProgressTest,
	"SpaceMMO.Quests.PanelShowsProgress",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOQuestPanelShowsProgressTest::RunTest(const FString& Parameters)
{
	FBackendJournalEntry Entry = MakeEntry(TEXT("Salvage Rights"), EBackendQuestState::InProgress, 6, 10);
	Entry.RewardMinorUnits = 75000;

	const TArray<FSpaceMMOQuestRowText> Rows =
		FSpaceMMOStationWork::BuildQuestRows({ Entry }, TArray<FBackendAvailableQuest>());

	TestEqual(TEXT("One row"), Rows.Num(), 1);
	TestEqual(TEXT("Names the quest"), Rows[0].Name, FString(TEXT("Salvage Rights")));
	TestEqual(TEXT("Shows progress"), Rows[0].Progress, FString(TEXT("6/10")));
	TestTrue(TEXT("...and as a fraction"), FMath::IsNearlyEqual(Rows[0].Fraction, 0.6f, 0.001f));

	// The step is the thing to go and do. A count alone says how far along without saying at what.
	TestTrue(TEXT("Shows the step"), Rows[0].Description.Contains(TEXT("Collect scrap")));
	TestEqual(TEXT("Shows the reward"), Rows[0].Reward, FString(TEXT("750.00 cr")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOQuestPanelHidesFinishedQuestsTest,
	"SpaceMMO.Quests.PanelHidesFinishedQuests",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOQuestPanelHidesFinishedQuestsTest::RunTest(const FString& Parameters)
{
	const TArray<FSpaceMMOQuestRowText> Rows = FSpaceMMOStationWork::BuildQuestRows(
		{
			MakeEntry(TEXT("Salvage Rights"), EBackendQuestState::InProgress, 1, 10),
			MakeEntry(TEXT("Old News"), EBackendQuestState::Completed),
			MakeEntry(TEXT("Abandoned Thing"), EBackendQuestState::Abandoned),
		},
		TArray<FBackendAvailableQuest>());

	auto Has = [&Rows](const TCHAR* Name)
	{ return Rows.ContainsByPredicate([Name](const FSpaceMMOQuestRowText& Row) { return Row.Name == Name; }); };

	TestTrue(TEXT("Keeps the active one"), Has(TEXT("Salvage Rights")));
	TestFalse(TEXT("Drops the completed one"), Has(TEXT("Old News")));
	TestFalse(TEXT("Drops the abandoned one"), Has(TEXT("Abandoned Thing")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOQuestPanelMarksAHandInTest,
	"SpaceMMO.Quests.PanelMarksAHandIn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOQuestPanelMarksAHandInTest::RunTest(const FString& Parameters)
{
	const TArray<FSpaceMMOQuestRowText> Rows = FSpaceMMOStationWork::BuildQuestRows(
		{
			MakeEntry(TEXT("Salvage Rights"), EBackendQuestState::InProgress, 2, 10),
			MakeEntry(TEXT("First Tools"), EBackendQuestState::ReadyToTurnIn, 10, 10),
		},
		TArray<FBackendAvailableQuest>());

	// Finished work first, and as a hand-in rather than as 10/10: a player standing on a quest that is
	// done wants paying before anything else, and the row carries the button that does it.
	TestEqual(TEXT("The finished one leads"), Rows[0].Name, FString(TEXT("First Tools")));
	TestTrue(TEXT("Says it is ready"), Rows[0].Kind == ESpaceMMOQuestRowKind::Ready);
	TestTrue(TEXT("Not shown as a count"), Rows[0].Progress.IsEmpty());
	TestTrue(TEXT("The running one follows"), Rows[1].Kind == ESpaceMMOQuestRowKind::Active);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOQuestPanelSpeaksWhenEmptyTest,
	"SpaceMMO.Quests.PanelSpeaksWhenEmpty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOQuestPanelSpeaksWhenEmptyTest::RunTest(const FString& Parameters)
{
	FBackendAvailableQuest Offer = MakeAvailable(TEXT("Salvage Rights"));
	Offer.Description = TEXT("Gather 10 scrap alloy.");
	Offer.RewardMinorUnits = 50000;

	const TArray<FSpaceMMOQuestRowText> Rows =
		FSpaceMMOStationWork::BuildQuestRows(TArray<FBackendJournalEntry>(), { Offer });

	// An offer says what it asks and what it pays, so Accept is not a leap in the dark (task 173).
	TestEqual(TEXT("Offers what is available"), Rows.Num(), 1);
	TestTrue(TEXT("As an offer"), Rows[0].Kind == ESpaceMMOQuestRowKind::Offered);
	TestEqual(TEXT("By its key, which Accept names"), Rows[0].QuestKey, FString(TEXT("salvage rights")));
	TestEqual(TEXT("With what it asks"), Rows[0].Description, FString(TEXT("Gather 10 scrap alloy.")));
	TestEqual(TEXT("With what it pays"), Rows[0].Reward, FString(TEXT("500.00 cr")));

	return true;
}

/**
 * A quest you have and a quest you could take are told apart.
 *
 * <strong>From a playtest, 7 September.</strong> Joe pressed the accept key on Salvage Rights and
 * was told "Nothing to accept". It was true: the quest had been accepted three weeks earlier and
 * was sitting at 0/10. The panel listed it as a bare line under a header advertising the accept
 * key, so a quest that was already his read exactly like one being offered. Now an offer is its own
 * kind of row with an Accept button, and a held quest never has one.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOQuestPanelSeparatesHeldFromOfferedTest,
	"SpaceMMO.Quests.PanelSeparatesHeldFromOffered",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOQuestPanelSeparatesHeldFromOfferedTest::RunTest(const FString& Parameters)
{
	const TArray<FSpaceMMOQuestRowText> Rows = FSpaceMMOStationWork::BuildQuestRows(
		{ MakeEntry(TEXT("Salvage Rights"), EBackendQuestState::InProgress, 0, 10) },
		{ MakeAvailable(TEXT("First Tools")) });

	TestEqual(TEXT("Both listed"), Rows.Num(), 2);
	TestEqual(TEXT("Held first"), Rows[0].Name, FString(TEXT("Salvage Rights")));
	TestTrue(TEXT("...as held, at 0/10"), Rows[0].Kind == ESpaceMMOQuestRowKind::Active && Rows[0].Progress == TEXT("0/10"));
	TestTrue(TEXT("Offered after, as an offer"), Rows[1].Kind == ESpaceMMOQuestRowKind::Offered);

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
			"stepTargetKey": "scrap_alloy", "stepProgress": 6, "stepRequired": 10,
			"rewardMinorUnits": 50000
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
	TestEqual(TEXT("Reward"), Entries[0].RewardMinorUnits, static_cast<int64>(50000));

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
		{ "questKey": "intro_gather_scrap", "name": "Salvage Rights", "kind": 0,
		  "description": "Collect ten of something.", "rewardMinorUnits": 50000 },
		{ "name": "Nameless", "kind": 0 }
	])");

	TArray<FBackendAvailableQuest> Quests;

	TestTrue(TEXT("Parsed"), FSpaceMMOBackendProtocol::ParseAvailableQuests(Json, Quests));

	// The keyless entry is dropped. Accepting names a quest by key, so listing one without a key
	// would offer the player something no keypress could ever take.
	TestEqual(TEXT("Kept the usable one"), Quests.Num(), 1);
	TestEqual(TEXT("Key"), Quests[0].QuestKey, FString(TEXT("intro_gather_scrap")));

	// The keys QuestEndpointTests pins on the server's side (task 173).
	TestEqual(TEXT("Description"), Quests[0].Description, FString(TEXT("Collect ten of something.")));
	TestEqual(TEXT("Reward"), Quests[0].RewardMinorUnits, static_cast<int64>(50000));

	const FString Body =
		FSpaceMMOBackendProtocol::MakeAcceptQuestBody(11, TEXT("intro_gather_scrap"));

	TestTrue(TEXT("Names the character"), Body.Contains(TEXT("\"characterId\":11")));
	TestTrue(TEXT("Names the quest"), Body.Contains(TEXT("\"questKey\":\"intro_gather_scrap\"")));

	return true;
}

#endif
