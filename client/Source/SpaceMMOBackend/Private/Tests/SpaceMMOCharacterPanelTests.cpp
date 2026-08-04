#include "Misc/AutomationTest.h"
#include "SpaceMMOPlayerController.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FBackendSkill MakeSkill(const TCHAR* Name, const int32 Level, const int64 Xp)
	{
		FBackendSkill Skill;
		Skill.Key = FString(Name).ToLower();
		Skill.Name = Name;
		Skill.Level = Level;
		Skill.Xp = Xp;

		return Skill;
	}

	FBackendInventoryItem MakeItem(const TCHAR* Name, const int32 Quantity)
	{
		FBackendInventoryItem Item;
		Item.ItemKey = FString(Name).ToLower();
		Item.Name = Name;
		Item.Quantity = Quantity;

		return Item;
	}

	/** True if any line contains the fragment. The panel's layout is free to change; its facts are not. */
	bool AnyLineContains(const TArray<FString>& Lines, const FString& Fragment)
	{
		return Lines.ContainsByPredicate(
			[&Fragment](const FString& Line) { return Line.Contains(Fragment); });
	}

	int32 IndexOfLineContaining(const TArray<FString>& Lines, const FString& Fragment)
	{
		return Lines.IndexOfByPredicate(
			[&Fragment](const FString& Line) { return Line.Contains(Fragment); });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOPanelShowsTrainedSkillsAndHeldItemsTest,
	"SpaceMMO.Panel.ShowsTrainedSkillsAndHeldItems",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOPanelShowsTrainedSkillsAndHeldItemsTest::RunTest(const FString& Parameters)
{
	const TArray<FBackendSkill> Skills{ MakeSkill(TEXT("Mining"), 4, 1234) };
	const TArray<FBackendInventoryItem> Inventory{ MakeItem(TEXT("Ferrite Ore"), 128) };

	const TArray<FString> Lines =
		ASpaceMMOPlayerController::BuildCharacterPanel(TEXT("Ayla"), Skills, Inventory);

	TestTrue(TEXT("Names the character"), AnyLineContains(Lines, TEXT("Ayla")));
	TestTrue(TEXT("Names the skill"), AnyLineContains(Lines, TEXT("Mining")));
	TestTrue(TEXT("Shows the level"), AnyLineContains(Lines, TEXT("4")));
	TestTrue(TEXT("Shows the xp"), AnyLineContains(Lines, TEXT("1,234")));
	TestTrue(TEXT("Names the item"), AnyLineContains(Lines, TEXT("Ferrite Ore")));
	TestTrue(TEXT("Shows the quantity"), AnyLineContains(Lines, TEXT("128")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOPanelSpeaksForANewCharacterTest,
	"SpaceMMO.Panel.SpeaksForANewCharacter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOPanelSpeaksForANewCharacterTest::RunTest(const FString& Parameters)
{
	// The state every player is in for their first few minutes. A panel that renders as blank space
	// here reads as broken, and the player has no way to tell that from "you own nothing yet".
	const TArray<FString> Lines = ASpaceMMOPlayerController::BuildCharacterPanel(
		TEXT("Boreth"), TArray<FBackendSkill>(), TArray<FBackendInventoryItem>());

	TestTrue(TEXT("Says nothing is trained"), AnyLineContains(Lines, TEXT("nothing trained")));
	TestTrue(TEXT("Says the hold is empty"), AnyLineContains(Lines, TEXT("empty")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOPanelHidesUntrainedSkillsTest,
	"SpaceMMO.Panel.HidesUntrainedSkills",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOPanelHidesUntrainedSkillsTest::RunTest(const FString& Parameters)
{
	// A character owns a row for every skill in the game from creation. Listing thirty untouched
	// zeroes would bury the one line that just changed, which is the only line anyone is looking at.
	const TArray<FBackendSkill> Skills{
		MakeSkill(TEXT("Mining"), 4, 1234),
		MakeSkill(TEXT("Refining"), 1, 0),
		MakeSkill(TEXT("Shipcrafting"), 1, 0),
	};

	const TArray<FString> Lines = ASpaceMMOPlayerController::BuildCharacterPanel(
		TEXT("Ayla"), Skills, TArray<FBackendInventoryItem>());

	TestTrue(TEXT("Keeps the trained one"), AnyLineContains(Lines, TEXT("Mining")));
	TestFalse(TEXT("Drops an untrained one"), AnyLineContains(Lines, TEXT("Refining")));
	TestFalse(TEXT("Drops the other"), AnyLineContains(Lines, TEXT("Shipcrafting")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOPanelOrdersByNameTest,
	"SpaceMMO.Panel.OrdersByName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOPanelOrdersByNameTest::RunTest(const FString& Parameters)
{
	// Response order is whatever the query returned. The panel refreshes after every gather, so an
	// unsorted list would reshuffle under the player's eyes at the exact moment they are reading it.
	const TArray<FBackendInventoryItem> Inventory{
		MakeItem(TEXT("Scrap Alloy"), 8),
		MakeItem(TEXT("Ferrite Ore"), 128),
	};

	const TArray<FString> Lines = ASpaceMMOPlayerController::BuildCharacterPanel(
		TEXT("Ayla"), TArray<FBackendSkill>(), Inventory);

	const int32 Ferrite = IndexOfLineContaining(Lines, TEXT("Ferrite Ore"));
	const int32 Scrap = IndexOfLineContaining(Lines, TEXT("Scrap Alloy"));

	TestTrue(TEXT("Both listed"), Ferrite != INDEX_NONE && Scrap != INDEX_NONE);
	TestTrue(TEXT("Ferrite before Scrap"), Ferrite < Scrap);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOPanelGroupsDigitsTest,
	"SpaceMMO.Panel.GroupsDigits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOPanelGroupsDigitsTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Zero"), ASpaceMMOPlayerController::GroupDigits(0), FString(TEXT("0")));
	TestEqual(TEXT("Below a thousand"), ASpaceMMOPlayerController::GroupDigits(999), FString(TEXT("999")));

	// The boundary a naive "insert every three from the left" gets wrong.
	TestEqual(TEXT("Exactly a thousand"),
		ASpaceMMOPlayerController::GroupDigits(1000), FString(TEXT("1,000")));

	TestEqual(TEXT("Seven digits"),
		ASpaceMMOPlayerController::GroupDigits(1234567), FString(TEXT("1,234,567")));

	// Level 99 in this project's curve (ADR-0004). If this line ever fails, the panel is misreporting
	// the single number a player spends years chasing.
	TestEqual(TEXT("Maximum skill xp"),
		ASpaceMMOPlayerController::GroupDigits(13034431), FString(TEXT("13,034,431")));

	// Beyond int32, which is why this takes int64 rather than clamping.
	TestEqual(TEXT("Past four billion"),
		ASpaceMMOPlayerController::GroupDigits(4294967296LL), FString(TEXT("4,294,967,296")));

	TestEqual(TEXT("Negative keeps its sign"),
		ASpaceMMOPlayerController::GroupDigits(-1234), FString(TEXT("-1,234")));

	return true;
}

#endif
