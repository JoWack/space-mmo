#include "Misc/AutomationTest.h"
#include "SpaceMMOPlayerController.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** Most of these cases predate instances and are about stacks; this keeps them readable. */
	const TArray<FBackendItemInstance> NoInstances;

	/** A deposit that needs a tool, as the world endpoint sends one. */
	FBackendResourceNode MakeGatedNode()
	{
		FBackendResourceNode Node;
		Node.Key = TEXT("node_capital_ferrite_a");
		Node.ItemKey = TEXT("ferrite_ore");
		Node.ItemName = TEXT("Ferrite Ore");
		Node.SkillKey = TEXT("mining");
		Node.RequiredLevel = 1;
		Node.RequiredToolKey = TEXT("crude_mining_laser");
		Node.RequiredToolName = TEXT("Crude Mining Laser");

		return Node;
	}

	FBackendItemInstance MakeTool(const TCHAR* Key, const int32 Condition)
	{
		FBackendItemInstance Instance;
		Instance.ItemKey = Key;
		Instance.Name = TEXT("Crude Mining Laser");
		Instance.Condition = Condition;

		return Instance;
	}

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
		ASpaceMMOPlayerController::BuildCharacterPanel(
			TEXT("Ayla"), TEXT("1,234.56"), Skills, Inventory, NoInstances);

	TestTrue(TEXT("Names the character"), AnyLineContains(Lines, TEXT("Ayla")));
	TestTrue(TEXT("Shows the balance"), AnyLineContains(Lines, TEXT("1,234.56 cr")));
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
		TEXT("Boreth"), FString(), TArray<FBackendSkill>(), TArray<FBackendInventoryItem>(), NoInstances);

	TestTrue(TEXT("Says nothing is trained"), AnyLineContains(Lines, TEXT("nothing trained")));
	TestTrue(TEXT("Says the hold is empty"), AnyLineContains(Lines, TEXT("empty")));

	// No balance yet, because the character list has not come back. Printing a confident "0.00 cr"
	// here would be indistinguishable from being broke, and being broke is a state this game
	// specifically punishes -- a player would go looking for money they already had.
	TestFalse(TEXT("Invents no balance"), AnyLineContains(Lines, TEXT("cr")));

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
		TEXT("Ayla"), TEXT("500.00"), Skills, TArray<FBackendInventoryItem>(), NoInstances);

	TestTrue(TEXT("Keeps the trained one"), AnyLineContains(Lines, TEXT("Mining")));
	TestFalse(TEXT("Drops an untrained one"), AnyLineContains(Lines, TEXT("Refining")));
	TestFalse(TEXT("Drops the other"), AnyLineContains(Lines, TEXT("Shipcrafting")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMONearbySaysWhatARockNeedsTest,
	"SpaceMMO.Panel.NearbySaysWhatARockNeeds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMONearbySaysWhatARockNeedsTest::RunTest(const FString& Parameters)
{
	const TArray<FBackendSkill> Skills{ MakeSkill(TEXT("Mining"), 5, 400) };

	// Nothing in reach is an ordinary state, and must say so rather than leave a bare heading that
	// reads as a panel which failed to load.
	const TArray<FString> Empty =
		ASpaceMMOPlayerController::BuildNearbyPanel(
			FBackendResourceNode(), Skills, NoInstances);

	TestTrue(TEXT("Says nothing is in reach"), AnyLineContains(Empty, TEXT("nothing within reach")));

	const TArray<FBackendItemInstance> WithLaser{ MakeTool(TEXT("crude_mining_laser"), 100) };

	const TArray<FString> Carried =
		ASpaceMMOPlayerController::BuildNearbyPanel(MakeGatedNode(), Skills, WithLaser);

	TestTrue(TEXT("Names the item"), AnyLineContains(Carried, TEXT("Ferrite Ore")));

	// The skill has been in the payload since deposits existed and was never shown, so nothing ever
	// told a player that ferrite is mined and scrap is gathered.
	TestTrue(TEXT("Names the skill"), AnyLineContains(Carried, TEXT("mining")));
	TestTrue(TEXT("Names the tool"), AnyLineContains(Carried, TEXT("Crude Mining Laser")));
	TestTrue(TEXT("Says it is carried"), AnyLineContains(Carried, TEXT("carried")));

	const TArray<FString> Without =
		ASpaceMMOPlayerController::BuildNearbyPanel(MakeGatedNode(), Skills, NoInstances);

	TestTrue(TEXT("Says the tool is missing"), AnyLineContains(Without, TEXT("you have none")));

	// A broken tool is not a tool. GuardToolAsync ignores condition zero, so a panel that counted
	// one would promise a gather the server then refuses -- worse than saying nothing.
	const TArray<FBackendItemInstance> Broken{ MakeTool(TEXT("crude_mining_laser"), 0) };

	const TArray<FString> WithBroken =
		ASpaceMMOPlayerController::BuildNearbyPanel(MakeGatedNode(), Skills, Broken);

	TestTrue(
		TEXT("A broken tool does not count as carried"),
		AnyLineContains(WithBroken, TEXT("you have none")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMONearbySaysWhenYouAreTooLowTest,
	"SpaceMMO.Panel.NearbySaysWhenYouAreTooLow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMONearbySaysWhenYouAreTooLowTest::RunTest(const FString& Parameters)
{
	FBackendResourceNode Node = MakeGatedNode();
	Node.RequiredLevel = 15;

	const TArray<FBackendItemInstance> WithLaser{ MakeTool(TEXT("crude_mining_laser"), 100) };

	// A character with no mining at all: the skill row is absent from the response rather than
	// present at zero, which is the case a lookup that assumed a match would get wrong.
	const TArray<FString> TooLow = ASpaceMMOPlayerController::BuildNearbyPanel(
		Node, TArray<FBackendSkill>(), WithLaser);

	TestTrue(TEXT("Shows the requirement"), AnyLineContains(TooLow, TEXT("lv 15")));
	TestTrue(TEXT("Shows where they are"), AnyLineContains(TooLow, TEXT("you are lv 0")));

	// And says nothing about level when they clear it, because a panel that comments on everything
	// is one a player stops reading.
	const TArray<FString> HighEnough = ASpaceMMOPlayerController::BuildNearbyPanel(
		Node, TArray<FBackendSkill>{ MakeSkill(TEXT("Mining"), 20, 100000) }, WithLaser);

	TestFalse(TEXT("Silent when qualified"), AnyLineContains(HighEnough, TEXT("you are lv")));

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
		TEXT("Ayla"), TEXT("500.00"), TArray<FBackendSkill>(), Inventory, NoInstances);

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
