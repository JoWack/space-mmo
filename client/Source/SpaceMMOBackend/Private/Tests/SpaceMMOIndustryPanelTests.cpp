#include "Misc/AutomationTest.h"
#include "SpaceMMOBackendProtocol.h"
#include "SpaceMMOPlayerController.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FBackendRecipe MakeRecipe(
		const int32 Id, const TCHAR* OutputName, const TCHAR* InputKey, const int32 InputQuantity)
	{
		FBackendRecipe Recipe;
		Recipe.Id = Id;
		Recipe.Key = FString(OutputName).ToLower();
		Recipe.OutputName = OutputName;
		Recipe.OutputQuantity = 4;
		Recipe.SkillName = TEXT("Refining");
		Recipe.RequiredLevel = 1;
		Recipe.JobSeconds = 60;

		FBackendRecipeInput Input;
		Input.ItemKey = InputKey;
		Input.Name = TEXT("Ferrite Ore");
		Input.Quantity = InputQuantity;

		Recipe.Inputs.Add(Input);

		return Recipe;
	}

	FBackendInventoryItem MakeItem(const TCHAR* Key, const int32 Quantity)
	{
		FBackendInventoryItem Item;
		Item.ItemKey = Key;
		Item.Name = TEXT("Ferrite Ore");
		Item.Quantity = Quantity;

		return Item;
	}

	bool AnyLineContains(const TArray<FString>& Lines, const FString& Fragment)
	{
		return Lines.ContainsByPredicate(
			[&Fragment](const FString& Line) { return Line.Contains(Fragment); });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOIndustryPanelShowsHaveAgainstNeedTest,
	"SpaceMMO.Industry.PanelShowsHaveAgainstNeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOIndustryPanelShowsHaveAgainstNeedTest::RunTest(const FString& Parameters)
{
	const TArray<FBackendRecipe> Recipes{ MakeRecipe(1, TEXT("Ferrite Plate"), TEXT("ferrite_ore"), 20) };
	const TArray<FBackendInventoryItem> Inventory{ MakeItem(TEXT("ferrite_ore"), 128) };

	const TArray<FString> Lines = ASpaceMMOPlayerController::BuildIndustryPanel(
		Recipes, TArray<FBackendIndustryJob>(), Inventory, 0);

	TestTrue(TEXT("Names the output"), AnyLineContains(Lines, TEXT("Ferrite Plate")));

	// Held over required, in that order, for the selected recipe. Both numbers came from the
	// server; the panel only puts them next to each other.
	TestTrue(TEXT("Shows held over required"), AnyLineContains(Lines, TEXT("128/20")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOIndustryPanelShowsShortfallWithoutRefusingTest,
	"SpaceMMO.Industry.PanelShowsShortfallWithoutRefusing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOIndustryPanelShowsShortfallWithoutRefusingTest::RunTest(const FString& Parameters)
{
	const TArray<FBackendRecipe> Recipes{ MakeRecipe(1, TEXT("Ferrite Plate"), TEXT("ferrite_ore"), 20) };
	const TArray<FBackendInventoryItem> Inventory{ MakeItem(TEXT("ferrite_ore"), 3) };

	const TArray<FString> Lines = ASpaceMMOPlayerController::BuildIndustryPanel(
		Recipes, TArray<FBackendIndustryJob>(), Inventory, 0);

	TestTrue(TEXT("Shows the shortfall"), AnyLineContains(Lines, TEXT("3/20")));

	// Deliberately no verdict. Deciding "you cannot build this" here would be a second copy of the
	// skill, tool, material and fee gates, free to drift from the real ones — so the recipe is still
	// listed, the player may still press, and the server gives the only answer that counts.
	TestTrue(TEXT("Still offers the recipe"), AnyLineContains(Lines, TEXT("Ferrite Plate")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOIndustryPanelSelectionSurvivesAShrunkCatalogTest,
	"SpaceMMO.Industry.PanelSelectionSurvivesAShrunkCatalog",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOIndustryPanelSelectionSurvivesAShrunkCatalogTest::RunTest(const FString& Parameters)
{
	const TArray<FBackendRecipe> Recipes{ MakeRecipe(1, TEXT("Ferrite Plate"), TEXT("ferrite_ore"), 20) };

	// Selection is remembered across re-fetches, so an index left over from a longer catalog is an
	// ordinary state rather than a bug. Left unclamped it would read as "nothing is selected" while
	// the start key quietly did nothing.
	const TArray<FString> Lines = ASpaceMMOPlayerController::BuildIndustryPanel(
		Recipes, TArray<FBackendIndustryJob>(), TArray<FBackendInventoryItem>(), 7);

	TestTrue(TEXT("Something is still marked"), AnyLineContains(Lines, TEXT(">")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOIndustryPanelDistinguishesReadyFromWaitingTest,
	"SpaceMMO.Industry.PanelDistinguishesReadyFromWaiting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOIndustryPanelDistinguishesReadyFromWaitingTest::RunTest(const FString& Parameters)
{
	FBackendIndustryJob Waiting;
	Waiting.Id = 1;
	Waiting.OutputName = TEXT("Ferrite Plate");
	Waiting.OutputQuantityTotal = 4;
	Waiting.SecondsRemaining = 42;
	Waiting.bIsClaimable = false;

	FBackendIndustryJob Ready;
	Ready.Id = 2;
	Ready.OutputName = TEXT("Crude Mining Laser");
	Ready.OutputQuantityTotal = 1;
	Ready.SecondsRemaining = 0;
	Ready.bIsClaimable = true;

	const TArray<FString> Lines = ASpaceMMOPlayerController::BuildIndustryPanel(
		TArray<FBackendRecipe>(), { Waiting, Ready }, TArray<FBackendInventoryItem>(), 0);

	TestTrue(TEXT("Counts down the unfinished one"), AnyLineContains(Lines, TEXT("42s")));

	// Both flags come from the server, and the difference is the whole point of the line: one is
	// worth pressing Z for and the other is not.
	TestTrue(TEXT("Marks the finished one"), AnyLineContains(Lines, TEXT("READY")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOIndustryPanelSpeaksWithNothingLoadedTest,
	"SpaceMMO.Industry.PanelSpeaksWithNothingLoaded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOIndustryPanelSpeaksWithNothingLoadedTest::RunTest(const FString& Parameters)
{
	// What a player sees between joining and the catalog arriving. Blank rows here would be
	// indistinguishable from the feature being broken.
	const TArray<FString> Lines = ASpaceMMOPlayerController::BuildIndustryPanel(
		TArray<FBackendRecipe>(), TArray<FBackendIndustryJob>(), TArray<FBackendInventoryItem>(), 0);

	TestTrue(TEXT("Says the catalog is empty"), AnyLineContains(Lines, TEXT("no recipes")));
	TestTrue(TEXT("Says nothing is running"), AnyLineContains(Lines, TEXT("none running")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOIndustryParsesTheCatalogTest,
	"SpaceMMO.Industry.ParsesTheCatalog",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOIndustryParsesTheCatalogTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"([
		{
			"id": 2, "key": "refine_ferrite_plate",
			"outputItemDefId": 3, "outputItemKey": "ferrite_plate", "outputName": "Ferrite Plate",
			"outputQuantity": 4,
			"skillKey": "refining", "skillName": "Refining",
			"requiredLevel": 1, "jobSeconds": 60, "xpPerRun": 600,
			"requiredToolItemDefId": null, "requiredToolKey": null, "requiredToolName": null,
			"inputs": [
				{ "itemDefId": 2, "itemKey": "ferrite_ore", "name": "Ferrite Ore", "quantity": 20 }
			]
		},
		{
			"id": 9, "outputName": "Nameless", "outputQuantity": 1, "inputs": []
		}
	])");

	TArray<FBackendRecipe> Recipes;

	TestTrue(TEXT("Parsed"), FSpaceMMOBackendProtocol::ParseRecipes(Json, Recipes));

	// The second entry has no key and is dropped. A key is the only name for a recipe that means
	// the same thing in two differently-seeded databases.
	TestEqual(TEXT("Kept only the usable one"), Recipes.Num(), 1);

	TestEqual(TEXT("Id"), Recipes[0].Id, 2);
	TestEqual(TEXT("Output"), Recipes[0].OutputName, FString(TEXT("Ferrite Plate")));
	TestEqual(TEXT("Skill"), Recipes[0].SkillName, FString(TEXT("Refining")));
	TestEqual(TEXT("Seconds"), Recipes[0].JobSeconds, 60);

	// A null tool is the ordinary case, not a parse failure.
	TestTrue(TEXT("No tool required"), Recipes[0].RequiredToolName.IsEmpty());

	TestEqual(TEXT("One input"), Recipes[0].Inputs.Num(), 1);
	TestEqual(TEXT("Input quantity"), Recipes[0].Inputs[0].Quantity, 20);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOIndustryParsesJobsTest,
	"SpaceMMO.Industry.ParsesJobs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOIndustryParsesJobsTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"([
		{
			"id": 7, "recipeId": 2, "recipeKey": "refine_ferrite_plate",
			"outputName": "Ferrite Plate", "outputQuantityTotal": 8, "runs": 2,
			"stationId": 1, "state": 0,
			"startedAt": "2026-08-04T01:00:00+00:00", "completesAt": "2026-08-04T01:02:00+00:00",
			"isClaimable": false, "secondsRemaining": 42
		},
		{
			"id": 0, "outputName": "Unclaimable", "isClaimable": true, "secondsRemaining": 0
		}
	])");

	TArray<FBackendIndustryJob> Jobs;

	TestTrue(TEXT("Parsed"), FSpaceMMOBackendProtocol::ParseIndustryJobs(Json, Jobs));

	// The zero-id entry is dropped. Showing it would offer a player something to collect and then
	// refuse every attempt, since there is no job to name in the claim.
	TestEqual(TEXT("Kept only the claimable-by-id one"), Jobs.Num(), 1);

	TestEqual(TEXT("Id"), Jobs[0].Id, static_cast<int64>(7));
	TestEqual(TEXT("Total output"), Jobs[0].OutputQuantityTotal, 8);
	TestEqual(TEXT("Seconds left"), Jobs[0].SecondsRemaining, 42);
	TestFalse(TEXT("Not yet claimable"), Jobs[0].bIsClaimable);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOIndustryBuildsRequestBodiesTest,
	"SpaceMMO.Industry.BuildsRequestBodies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOIndustryBuildsRequestBodiesTest::RunTest(const FString& Parameters)
{
	const FString Start = FSpaceMMOBackendProtocol::MakeStartJobBody(11, 2, 1, 3);

	TestTrue(TEXT("Names the character"), Start.Contains(TEXT("\"characterId\":11")));
	TestTrue(TEXT("Names the recipe"), Start.Contains(TEXT("\"recipeId\":2")));
	TestTrue(TEXT("Names the runs"), Start.Contains(TEXT("\"runs\":3")));

	// Job ids are int64. Formatting one through a 32-bit path would wrap silently somewhere past
	// two billion jobs, and the symptom would be claiming somebody else's work.
	const FString Claim = FSpaceMMOBackendProtocol::MakeClaimJobBody(11, 4294967296LL);

	TestTrue(TEXT("Carries a large job id intact"), Claim.Contains(TEXT("\"jobId\":4294967296")));

	return true;
}

#endif
