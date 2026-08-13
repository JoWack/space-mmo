#include "Misc/AutomationTest.h"
#include "SpaceMMODepositPrompt.h"
#include "SpaceMMOOnFootReadout.h"
#include "SpaceMMOSkillsScreen.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Tests for the on-foot HUD's wording.
 *
 * The formatters are pure functions for the same reason the flight readout's is: a HUD is otherwise
 * only checkable by looking at it, and what is worth checking here is not visual — which blocker is
 * reported, whether a broken tool counts as carried, and what an untrained skill claims about
 * itself.
 */

namespace
{
	FBackendResourceNode GatedFerrite()
	{
		FBackendResourceNode Node;
		Node.Key = TEXT("ferrite_vein");
		Node.ItemName = TEXT("Ferrite Ore");
		Node.SkillKey = TEXT("mining");
		Node.RequiredLevel = 3;
		Node.RequiredToolKey = TEXT("crude_mining_laser");
		Node.RequiredToolName = TEXT("Crude Mining Laser");

		return Node;
	}

	FBackendSkill SkillAt(const TCHAR* Key, const int32 Level, const int64 Xp)
	{
		FBackendSkill Skill;
		Skill.Key = Key;
		Skill.Name = Key;
		Skill.Level = Level;
		Skill.Xp = Xp;

		return Skill;
	}

	FBackendItemInstance ToolAt(const TCHAR* Key, const int32 Condition)
	{
		FBackendItemInstance Instance;
		Instance.ItemKey = Key;
		Instance.Condition = Condition;

		return Instance;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOOnFootReadoutSaysWhoAndHowMuchTest,
	"SpaceMMO.HUD.OnFootReadoutSaysWhoAndHowMuch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOOnFootReadoutSaysWhoAndHowMuchTest::RunTest(const FString& Parameters)
{
	const FSpaceMMOOnFootReadoutText Text =
		USpaceMMOOnFootReadout::Build(TEXT("Joe Wacker"), TEXT("12,480"));

	TestEqual(TEXT("Name in caps"), Text.Name, FString(TEXT("JOE WACKER")));

	// The unit belongs to the value, like "m/s" on the flight readout. A bare number beside a name
	// reads as an identifier.
	TestEqual(TEXT("Credits carry their unit"), Text.Credits, FString(TEXT("12,480 cr")));
	TestTrue(TEXT("And say they mean something"), Text.bHasCredits);

	// Before the character list arrives there is no balance, and a confident "0 cr" is
	// indistinguishable from being broke.
	const FSpaceMMOOnFootReadoutText Unknown =
		USpaceMMOOnFootReadout::Build(FString(), FString());

	TestEqual(TEXT("No character yet"), Unknown.Name, FString(TEXT("Not identified")));
	TestTrue(TEXT("And no balance invented"), Unknown.Credits.IsEmpty());
	TestFalse(TEXT("Which the flag reports"), Unknown.bHasCredits);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMODepositPromptNamesTheBlockerTest,
	"SpaceMMO.HUD.DepositPromptNamesTheBlocker",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMODepositPromptNamesTheBlockerTest::RunTest(const FString& Parameters)
{
	// Nothing in reach: the whole prompt collapses rather than leaving a header over empty ground.
	const FSpaceMMODepositPromptText Empty = USpaceMMODepositPrompt::Build(
		FBackendResourceNode(), {}, {}, TEXT("E"));

	TestFalse(TEXT("Nothing in reach"), Empty.bHasDeposit);
	TestTrue(TEXT("And nothing said"), Empty.ItemName.IsEmpty());
	TestFalse(TEXT("And nothing to press"), Empty.bCanGather);

	// Levelled and equipped: no blockers at all, and the key hint is live.
	const FSpaceMMODepositPromptText Ready = USpaceMMODepositPrompt::Build(
		GatedFerrite(),
		{ SkillAt(TEXT("mining"), 5, 400) },
		{ ToolAt(TEXT("crude_mining_laser"), 87) },
		TEXT("E"));

	TestEqual(TEXT("The rock is named"), Ready.ItemName, FString(TEXT("Ferrite Ore")));
	TestEqual(TEXT("Skill and level as one phrase"), Ready.Requirement,
		FString(TEXT("mining  ·  lv 3")));
	TestTrue(TEXT("No level blocker"), Ready.LevelBlocker.IsEmpty());
	TestTrue(TEXT("No tool blocker"), Ready.ToolBlocker.IsEmpty());
	TestTrue(TEXT("Gatherable"), Ready.bCanGather);
	TestEqual(TEXT("Key comes from the bindings"), Ready.GatherKey, FString(TEXT("E")));

	// Under-levelled and empty-handed: both blockers, separately, so the Blueprint can colour them
	// without colouring the rest. That separation is the reason this is a widget rather than text.
	const FSpaceMMODepositPromptText Blocked = USpaceMMODepositPrompt::Build(
		GatedFerrite(), { SkillAt(TEXT("mining"), 1, 20) }, {}, TEXT("E"));

	TestEqual(TEXT("Level blocker"), Blocked.LevelBlocker, FString(TEXT("you are lv 1")));
	TestEqual(TEXT("Tool blocker"), Blocked.ToolBlocker, FString(TEXT("you have none")));
	TestFalse(TEXT("Not gatherable"), Blocked.bCanGather);

	// The key hint survives being blocked, dimmed rather than removed: somebody who cannot mine this
	// yet still needs to see what it is they are being stopped from doing.
	TestEqual(TEXT("Hint still there"), Blocked.GatherKey, FString(TEXT("E")));

	// A broken tool is not a tool. GuardToolAsync ignores condition zero, so a prompt that counted
	// one would promise a gather the server then refuses.
	const FSpaceMMODepositPromptText Broken = USpaceMMODepositPrompt::Build(
		GatedFerrite(),
		{ SkillAt(TEXT("mining"), 5, 400) },
		{ ToolAt(TEXT("crude_mining_laser"), 0) },
		TEXT("E"));

	TestEqual(TEXT("A broken laser is none"), Broken.ToolBlocker, FString(TEXT("you have none")));
	TestFalse(TEXT("And blocks the gather"), Broken.bCanGather);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOSkillsScreenShowsEverySkillTest,
	"SpaceMMO.HUD.SkillsScreenShowsEverySkill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOSkillsScreenShowsEverySkillTest::RunTest(const FString& Parameters)
{
	FBackendSkill Mining = SkillAt(TEXT("Mining"), 2, 100);
	Mining.XpToNextLevel = 74;
	Mining.ProgressToNextLevel = 0.1868f;

	FBackendSkill Untouched = SkillAt(TEXT("Astronomy"), 1, 0);
	Untouched.XpToNextLevel = 83;
	Untouched.ProgressToNextLevel = 0.0f;

	const TArray<FSpaceMMOSkillRowText> Rows =
		USpaceMMOSkillsScreen::Build({ Untouched, Mining });

	// Every skill, unlike the always-on panel, which filters to Xp > 0. A screen opened on purpose
	// is where the full list belongs -- otherwise the eight combat skills are undiscoverable.
	TestEqual(TEXT("Both skills listed"), Rows.Num(), 2);

	// Trained first, even though Astronomy sorts before Mining alphabetically: a list that is mostly
	// zeroes should still open on what the player has actually done.
	TestEqual(TEXT("Trained first"), Rows[0].Name, FString(TEXT("Mining")));
	TestTrue(TEXT("And is marked trained"), Rows[0].bTrained);
	TestEqual(TEXT("Level"), Rows[0].Level, FString(TEXT("lv 2")));
	TestEqual(TEXT("Grouped XP"), Rows[0].Xp, FString(TEXT("100 xp")));
	TestEqual(TEXT("What is left to earn"), Rows[0].ToNext, FString(TEXT("74 to lv 3")));

	// Level 1 is a real level, not an absence -- every character has every skill from creation, and
	// a dash here would claim something the server does not.
	TestEqual(TEXT("Untouched is level 1"), Rows[1].Level, FString(TEXT("lv 1")));
	TestFalse(TEXT("But not trained"), Rows[1].bTrained);
	TestEqual(TEXT("With a full level to go"), Rows[1].ToNext, FString(TEXT("83 to lv 2")));

	// A server too old to send progress must draw no bars rather than empty ones. Zero would mean
	// "just started this level", which is a different and wrong claim.
	FBackendSkill Old = SkillAt(TEXT("Mining"), 2, 100);
	Old.ProgressToNextLevel = -1.0f;

	const TArray<FSpaceMMOSkillRowText> Legacy = USpaceMMOSkillsScreen::Build({ Old });

	TestTrue(TEXT("No bar without progress"), Legacy[0].Progress < 0.0f);
	TestTrue(TEXT("And nothing claimed about the next level"), Legacy[0].ToNext.IsEmpty());

	// At the cap there is nothing left to earn, and "0 to lv 100" is not a thing to print.
	FBackendSkill Capped = SkillAt(TEXT("Mining"), 99, 13034431);
	Capped.XpToNextLevel = 0;
	Capped.ProgressToNextLevel = 1.0f;

	const TArray<FSpaceMMOSkillRowText> AtCap = USpaceMMOSkillsScreen::Build({ Capped });

	TestTrue(TEXT("Nothing to go at the cap"), AtCap[0].ToNext.IsEmpty());
	TestTrue(TEXT("But the bar is full"), AtCap[0].Progress > 0.999f);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
