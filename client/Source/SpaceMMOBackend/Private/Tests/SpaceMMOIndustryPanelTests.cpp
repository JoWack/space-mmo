#include "Misc/AutomationTest.h"
#include "SpaceMMOBackendProtocol.h"
#include "SpaceMMOPlayerController.h"
#include "SpaceMMOStationWork.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace SpaceMMOIndustryPanelTests
{
	constexpr int32 Here = 5;

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

	/** In this station's hangar unless told otherwise: where a job takes its inputs from. */
	FBackendInventoryItem MakeIndustryItem(
		const TCHAR* Key, const int32 Quantity,
		const EBackendInventoryKind Kind = EBackendInventoryKind::StationHangar, const int32 StationId = Here)
	{
		FBackendInventoryItem Item;
		Item.ItemKey = Key;
		Item.Name = TEXT("Ferrite Ore");
		Item.Quantity = Quantity;
		Item.Kind = Kind;
		Item.StationId = StationId;

		return Item;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOIndustryPanelShowsHaveAgainstNeedTest,
	"SpaceMMO.Industry.PanelShowsHaveAgainstNeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOIndustryPanelShowsHaveAgainstNeedTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMOIndustryPanelTests;

	const TArray<FBackendRecipe> Recipes{ MakeRecipe(1, TEXT("Ferrite Plate"), TEXT("ferrite_ore"), 20) };
	const TArray<FBackendInventoryItem> Inventory{ MakeIndustryItem(TEXT("ferrite_ore"), 128) };

	const TArray<FSpaceMMORecipeRowText> Rows =
		FSpaceMMOStationWork::BuildRecipeRows(Recipes, Inventory, Here, 0, 1);

	TestEqual(TEXT("One row"), Rows.Num(), 1);
	TestEqual(TEXT("Names one run's output"), Rows[0].Title, FString(TEXT("Ferrite Plate ×4")));
	TestEqual(TEXT("One input listed"), Rows[0].Inputs.Num(), 1);

	// Held against required. Both numbers came from the server; the panel only puts them side by side.
	TestEqual(TEXT("Held"), Rows[0].Inputs[0].Held, 128);
	TestEqual(TEXT("Needed"), Rows[0].Inputs[0].Needed, 20);
	TestFalse(TEXT("Not short"), Rows[0].Inputs[0].IsShort());

	return true;
}

/**
 * Only what is in this station's hangar counts as held.
 *
 * A job takes its inputs from the hangar where it is started. The old panel added up every stack of
 * the item anywhere -- carried, in the ship's hold, at other stations -- and showed it as held, which
 * offered a count the server would refuse. With a count stepper capped by "held", that over-count would
 * have become a button that lies (task 173).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOIndustryHeldMeansThisHangarTest,
	"SpaceMMO.Industry.HeldMeansThisHangar",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOIndustryHeldMeansThisHangarTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMOIndustryPanelTests;

	const TArray<FBackendInventoryItem> Inventory{
		MakeIndustryItem(TEXT("ferrite_ore"), 30),
		MakeIndustryItem(TEXT("ferrite_ore"), 500, EBackendInventoryKind::ShipHold, 0),
		MakeIndustryItem(TEXT("ferrite_ore"), 500, EBackendInventoryKind::CharacterCarried, 0),
		MakeIndustryItem(TEXT("ferrite_ore"), 500, EBackendInventoryKind::StationHangar, Here + 1),
	};

	TestEqual(TEXT("Only this hangar's stack"),
		FSpaceMMOStationWork::HeldHere(Inventory, TEXT("ferrite_ore"), Here), 30);

	const FBackendRecipe Recipe = MakeRecipe(1, TEXT("Ferrite Plate"), TEXT("ferrite_ore"), 10);

	TestEqual(TEXT("Three runs from 30 at 10 each"), FSpaceMMOStationWork::MostRuns(Recipe, Inventory, Here), 3);

	return true;
}

/** The count scales what is needed, and stops at what the hangar can supply. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOIndustryRunsScaleAndStopAtWhatIsHeldTest,
	"SpaceMMO.Industry.RunsScaleAndStopAtWhatIsHeld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOIndustryRunsScaleAndStopAtWhatIsHeldTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMOIndustryPanelTests;

	const TArray<FBackendRecipe> Recipes{ MakeRecipe(1, TEXT("Ferrite Plate"), TEXT("ferrite_ore"), 10) };
	const TArray<FBackendInventoryItem> Inventory{ MakeIndustryItem(TEXT("ferrite_ore"), 34) };

	const FSpaceMMOStartBarText Three = FSpaceMMOStationWork::BuildStartBar(Recipes, Inventory, Here, 0, 3);

	TestEqual(TEXT("Three runs allowed"), Three.Runs, 3);
	TestEqual(TEXT("Three is the most"), Three.MostRuns, 3);
	TestEqual(TEXT("The label counts the whole output"), Three.Label, FString(TEXT("Start Ferrite Plate ×12")));
	TestTrue(TEXT("The note gives the most and the total time"), Three.Note.Contains(TEXT("3")) && Three.Note.Contains(TEXT("3 m 0 s")));

	const TArray<FSpaceMMORecipeRowText> Rows = FSpaceMMOStationWork::BuildRecipeRows(Recipes, Inventory, Here, 0, 3);

	TestEqual(TEXT("Needed follows the count"), Rows[0].Inputs[0].Needed, 30);

	// Asking for more than the hangar holds settles on the most it can supply, not on the request.
	const FSpaceMMOStartBarText TooMany = FSpaceMMOStationWork::BuildStartBar(Recipes, Inventory, Here, 0, 40);

	TestEqual(TEXT("Clamped to the most"), TooMany.Runs, 3);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOIndustryPanelShowsShortfallWithoutRefusingTest,
	"SpaceMMO.Industry.PanelShowsShortfallWithoutRefusing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOIndustryPanelShowsShortfallWithoutRefusingTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMOIndustryPanelTests;

	const TArray<FBackendRecipe> Recipes{ MakeRecipe(1, TEXT("Ferrite Plate"), TEXT("ferrite_ore"), 20) };
	const TArray<FBackendInventoryItem> Inventory{ MakeIndustryItem(TEXT("ferrite_ore"), 3) };

	const TArray<FSpaceMMORecipeRowText> Rows = FSpaceMMOStationWork::BuildRecipeRows(Recipes, Inventory, Here, 0, 1);

	TestTrue(TEXT("Shows the shortfall"), Rows[0].Inputs[0].IsShort());

	const FSpaceMMOStartBarText Bar = FSpaceMMOStationWork::BuildStartBar(Recipes, Inventory, Here, 0, 1);

	TestTrue(TEXT("Says it is short"), Bar.bShort && Bar.Note.Contains(TEXT("Ferrite Ore")));

	// Deliberately no verdict. Deciding "you cannot build this" here would be a second copy of the
	// skill, tool, material and fee gates, free to drift from the real ones -- so the recipe is still
	// listed, Start is still there at a count of one, and the server gives the only answer that counts.
	TestTrue(TEXT("Still offers the recipe"), Bar.bHasRecipe);
	TestEqual(TEXT("Still one run to ask for"), Bar.Runs, 1);

	// Four short inputs are counted rather than listed: listed, they ran out of the panel's left edge.
	FBackendRecipe Frame = MakeRecipe(2, TEXT("Composite Frame"), TEXT("ferric_regolith"), 10);

	for (const TCHAR* Key : {TEXT("grimhold_slag"), TEXT("terran_ferrite"), TEXT("luminous_amber")})
	{
		FBackendRecipeInput Input;
		Input.ItemKey = Key;
		Input.Name = Key;
		Input.Quantity = 10;
		Frame.Inputs.Add(Input);
	}

	const FSpaceMMOStartBarText Four = FSpaceMMOStationWork::BuildStartBar({ Frame }, Inventory, Here, 0, 1);

	TestEqual(TEXT("Four short inputs are counted"), Four.Note, FString(TEXT("short of 4 inputs in this hangar")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOIndustryPanelSelectionSurvivesAShrunkCatalogTest,
	"SpaceMMO.Industry.PanelSelectionSurvivesAShrunkCatalog",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOIndustryPanelSelectionSurvivesAShrunkCatalogTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMOIndustryPanelTests;

	const TArray<FBackendRecipe> Recipes{ MakeRecipe(1, TEXT("Ferrite Plate"), TEXT("ferrite_ore"), 20) };

	// Selection is remembered across re-fetches, so an index left over from a longer catalogue is an
	// ordinary state rather than a bug. Left unclamped it would read as "nothing is selected" while
	// Start quietly did nothing.
	const TArray<FSpaceMMORecipeRowText> Rows = FSpaceMMOStationWork::BuildRecipeRows(
		Recipes, TArray<FBackendInventoryItem>(), Here, 7, 1);

	TestTrue(TEXT("Something is still selected"), Rows[0].bSelected);
	TestTrue(TEXT("...and Start has a recipe"),
		FSpaceMMOStationWork::BuildStartBar(Recipes, TArray<FBackendInventoryItem>(), Here, 7, 1).bHasRecipe);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOIndustryPanelDistinguishesReadyFromWaitingTest,
	"SpaceMMO.Industry.PanelDistinguishesReadyFromWaiting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOIndustryPanelDistinguishesReadyFromWaitingTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMOIndustryPanelTests;

	FBackendIndustryJob Waiting;
	Waiting.Id = 1;
	Waiting.RecipeKey = TEXT("ferrite plate");
	Waiting.OutputName = TEXT("Ferrite Plate");
	Waiting.OutputQuantityTotal = 8;
	Waiting.Runs = 2;
	Waiting.SecondsRemaining = 90;
	Waiting.bIsClaimable = false;

	FBackendIndustryJob Ready;
	Ready.Id = 2;
	Ready.OutputName = TEXT("Crude Mining Laser");
	Ready.OutputQuantityTotal = 1;
	Ready.SecondsRemaining = 0;
	Ready.bIsClaimable = true;

	const TArray<FSpaceMMOJobRowText> Rows = FSpaceMMOStationWork::BuildJobRows(
		{ Waiting, Ready }, { MakeRecipe(1, TEXT("Ferrite Plate"), TEXT("ferrite_ore"), 20) });

	TestEqual(TEXT("Counts down the unfinished one"), Rows[0].Remaining, FString(TEXT("1 m 30 s")));
	TestFalse(TEXT("...which is not ready"), Rows[0].bReady);

	// Two runs of a 60 s recipe with 90 s left is a quarter done. The bar is the recipe's time times the
	// job's runs, which is how the server set the job's length.
	TestTrue(TEXT("A quarter done"), FMath::IsNearlyEqual(Rows[0].Progress, 0.25f, 0.001f));

	// Both flags come from the server, and the difference is the whole point of the row: one carries a
	// Claim button and the other does not.
	TestTrue(TEXT("Marks the finished one"), Rows[1].bReady);
	TestEqual(TEXT("Names everything it made"), Rows[1].Title, FString(TEXT("Crude Mining Laser ×1")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOIndustryPanelSpeaksWithNothingLoadedTest,
	"SpaceMMO.Industry.PanelSpeaksWithNothingLoaded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOIndustryPanelSpeaksWithNothingLoadedTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMOIndustryPanelTests;

	// What a player sees between joining and the catalogue arriving: no rows, and no Start bar offering
	// to start nothing. The overlay says "no recipes loaded" and "none running" in their place.
	const FSpaceMMOStartBarText Bar = FSpaceMMOStationWork::BuildStartBar(
		TArray<FBackendRecipe>(), TArray<FBackendInventoryItem>(), Here, 0, 1);

	TestFalse(TEXT("No Start bar"), Bar.bHasRecipe);
	TestEqual(TEXT("No recipe rows"),
		FSpaceMMOStationWork::BuildRecipeRows(TArray<FBackendRecipe>(), TArray<FBackendInventoryItem>(), Here, 0, 1).Num(), 0);

	TestEqual(TEXT("Seconds"), FSpaceMMOStationWork::Duration(40), FString(TEXT("40 s")));
	TestEqual(TEXT("Minutes"), FSpaceMMOStationWork::Duration(80), FString(TEXT("1 m 20 s")));
	TestEqual(TEXT("Hours"), FSpaceMMOStationWork::Duration(3900), FString(TEXT("1 h 5 m")));

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
	FSpaceMMOIndustryFormatsCreditsTest,
	"SpaceMMO.Industry.FormatsCredits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOIndustryFormatsCreditsTest::RunTest(const FString& Parameters)
{
	// Minor units are hundredths and the split is integer arithmetic. Routing a balance through a
	// double is how a credit goes missing, which in a player-driven economy is not a rounding
	// detail but a dupe.
	TestEqual(TEXT("Zero"), FSpaceMMOBackendProtocol::FormatCredits(0), FString(TEXT("0.00")));
	TestEqual(TEXT("Two credits"), FSpaceMMOBackendProtocol::FormatCredits(200), FString(TEXT("2.00")));

	// The case a naive divide gets wrong: a fraction below ten needs its leading zero.
	TestEqual(TEXT("Small fraction"), FSpaceMMOBackendProtocol::FormatCredits(105), FString(TEXT("1.05")));

	TestEqual(
		TEXT("Grouped"),
		FSpaceMMOBackendProtocol::FormatCredits(123456789),
		FString(TEXT("1,234,567.89")));

	TestEqual(
		TEXT("Negative keeps its sign and fraction"),
		FSpaceMMOBackendProtocol::FormatCredits(-1550),
		FString(TEXT("-15.50")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOIndustryParsesAFactionSaleTest,
	"SpaceMMO.Industry.ParsesAFactionSale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOIndustryParsesAFactionSaleTest::RunTest(const FString& Parameters)
{
	// The daily faucet budget can cut a sale short, so what came back is not what was asked for. A
	// client that echoed its own request would tell a player they had parted with material still
	// sitting in their hangar.
	const FString Json = TEXT(
		R"({"quantitySold":5,"paidMinorUnits":1000,"withheldMinorUnits":3000,"wasCapped":true})");

	int32 Sold = 0;
	int64 Paid = 0;

	TestTrue(TEXT("Parsed"), FSpaceMMOBackendProtocol::ParseFactionSale(Json, Sold, Paid));

	TestEqual(TEXT("Sold what the server took"), Sold, 5);
	TestEqual(TEXT("Paid what the server paid"), Paid, static_cast<int64>(1000));

	// A refused sale reads as zero rather than as a parse failure: nothing sold is an ordinary
	// answer, not an error.
	int32 NoneSold = -1;
	int64 NonePaid = -1;

	TestTrue(
		TEXT("Parsed a refusal"),
		FSpaceMMOBackendProtocol::ParseFactionSale(
			TEXT(R"({"quantitySold":0,"paidMinorUnits":0,"wasCapped":true})"), NoneSold, NonePaid));

	TestEqual(TEXT("Nothing sold"), NoneSold, 0);

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
