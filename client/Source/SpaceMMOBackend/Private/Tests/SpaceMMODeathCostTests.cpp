#include "Misc/AutomationTest.h"
#include "SpaceMMOCombatHud.h"
#include "SpaceMMODeathWire.h"
#include "SpaceMMOInventoryScreen.h"
#include "SpaceMMOLootComponent.h"
#include "SpaceMMOLootCrateActor.h"
#include "SpaceMMOLootScreen.h"
#include "SpaceMMOPlanet.h"
#include "SpaceMMOPlanetTerrain.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Task 103 on the client: what a death cost and a crate's contents read as the API writes them, what is sent reads as
 * the API reads it, the death panel's table, E's choice between a crate and a rock, and where a crate lies.
 *
 * <strong>The JSON is the API's own</strong>, captured on 19 October from <c>DeathEndpointTests</c>, which writes every
 * answer to its test output: a death costed (ore and Martian plate carried, a rifle worn), a death already costed,
 * the live crates, a stack taken with ore left, the last taken, and a crate gone.
 */
namespace SpaceMMODeathCostTests
{
	const TCHAR* RealCosts = TEXT(R"({"applied":true,"atStation":false,"lost":[{"itemDefId":2,"itemKey":"ferrite_ore","name":"Ferrite Ore","style":1,"dropped":3,"destroyed":3},{"itemDefId":3,"itemKey":"ferrite_plate","name":"Structural Plate","style":2,"dropped":3,"destroyed":2}],"worn":[{"itemInstanceId":2,"itemKey":"laser_rifle","name":"Laser Rifle","before":100,"after":90}],"container":{"id":1,"droppedByCharacterId":2,"droppedBy":"Intruder","x":-180.25,"y":3.5,"z":-1.125,"secondsLeft":599.7964405,"items":[{"itemDefId":2,"itemKey":"ferrite_ore","name":"Ferrite Ore","style":0,"shownStyle":1,"quantity":3,"volumeM3":0.4},{"itemDefId":3,"itemKey":"ferrite_plate","name":"Structural Plate","style":2,"shownStyle":2,"quantity":3,"volumeM3":0.2}]}})");
	const TCHAR* RealNothingOwed = TEXT(R"({"applied":false,"atStation":false,"lost":[],"worn":[],"container":null})");
	const TCHAR* RealContainers = TEXT(R"([{"id":1,"droppedByCharacterId":2,"droppedBy":"Intruder","x":-180.25,"y":3.5,"z":-1.125,"secondsLeft":599.9616572,"items":[{"itemDefId":2,"itemKey":"ferrite_ore","name":"Ferrite Ore","style":0,"shownStyle":1,"quantity":3,"volumeM3":0.4},{"itemDefId":3,"itemKey":"ferrite_plate","name":"Structural Plate","style":2,"shownStyle":2,"quantity":3,"volumeM3":0.2}]}])");
	const TCHAR* RealTookPlate = TEXT(R"({"took":true,"refusal":null,"taken":[{"itemDefId":3,"itemKey":"ferrite_plate","name":"Structural Plate","style":2,"shownStyle":2,"quantity":3,"volumeM3":0.2}],"left":{"id":1,"droppedByCharacterId":2,"droppedBy":"Intruder","x":-180.25,"y":3.5,"z":-1.125,"secondsLeft":599.8656276,"items":[{"itemDefId":2,"itemKey":"ferrite_ore","name":"Ferrite Ore","style":0,"shownStyle":1,"quantity":3,"volumeM3":0.4}]}})");
	const TCHAR* RealTookTheLast = TEXT(R"({"took":true,"refusal":null,"taken":[{"itemDefId":2,"itemKey":"ferrite_ore","name":"Ferrite Ore","style":0,"shownStyle":1,"quantity":3,"volumeM3":0.4}],"left":null})");
	const TCHAR* RealGone = TEXT(R"({"took":false,"refusal":"gone","taken":[],"left":null})");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMODeathReadsTheServersWordsTest,
	"SpaceMMO.Death.ReadsTheServersWords",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMODeathReadsTheServersWordsTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMODeathCostTests;

	FBackendDeathCosts Costs;

	if (TestTrue(TEXT("A costed death parses"), FSpaceMMODeathWire::ParseDeathCosts(RealCosts, Costs)))
	{
		TestTrue(TEXT("Applied"), Costs.bApplied);
		TestFalse(TEXT("Not at a station"), Costs.bAtStation);
		TestEqual(TEXT("Two materials lost"), Costs.Lost.Num(), 2);

		if (Costs.Lost.Num() == 2)
		{
			TestEqual(TEXT("Ore shows its world, Terran"), Costs.Lost[0].Style, EBackendItemStyle::Terran);
			TestEqual(TEXT("Three ore dropped"), Costs.Lost[0].Dropped, 3);
			TestEqual(TEXT("Plate is Martian"), Costs.Lost[1].Style, EBackendItemStyle::Martian);
			TestEqual(TEXT("Two plate destroyed"), Costs.Lost[1].Destroyed, 2);
		}

		if (TestEqual(TEXT("One item worn"), Costs.Worn.Num(), 1))
		{
			TestEqual(TEXT("From 100"), Costs.Worn[0].Before, 100);
			TestEqual(TEXT("To 90"), Costs.Worn[0].After, 90);
			TestEqual(TEXT("The rifle"), Costs.Worn[0].Name, FString(TEXT("Laser Rifle")));
		}

		TestTrue(TEXT("A crate"), Costs.bHasContainer);
		TestEqual(TEXT("Where it lies"), Costs.Container.PositionKilometres, FVector(-180.25, 3.5, -1.125));
		TestEqual(TEXT("Whose"), Costs.Container.DroppedBy, FString(TEXT("Intruder")));
		TestTrue(TEXT("Ten minutes, near enough"), FMath::IsNearlyEqual(Costs.Container.SecondsLeft, 599.8, 0.1));

		if (TestEqual(TEXT("Two stacks in it"), Costs.Container.Items.Num(), 2))
		{
			// The stack's own style for taking it, and the world it shows for reading it: ore's stack is Common.
			TestEqual(TEXT("Ore's stack is Common"), Costs.Container.Items[0].Style, EBackendItemStyle::Common);
			TestEqual(TEXT("and shows Terran"), Costs.Container.Items[0].ShownStyle, EBackendItemStyle::Terran);
			TestEqual(TEXT("Plate's volume"), Costs.Container.Items[1].VolumeM3, 0.2);
		}
	}

	FBackendDeathCosts Owed;

	if (TestTrue(TEXT("Nothing owed parses"), FSpaceMMODeathWire::ParseDeathCosts(RealNothingOwed, Owed)))
	{
		TestFalse(TEXT("Not applied"), Owed.bApplied);
		TestFalse(TEXT("A null crate is no crate, not a crate of zeroes"), Owed.bHasContainer);
	}

	TArray<FBackendLootContainer> Live;

	if (TestTrue(TEXT("The live crates parse"), FSpaceMMODeathWire::ParseContainers(RealContainers, Live)))
	{
		TestEqual(TEXT("One"), Live.Num(), 1);
	}

	FBackendLootResult Plate;

	if (TestTrue(TEXT("A stack taken parses"), FSpaceMMODeathWire::ParseLootResult(RealTookPlate, Plate)))
	{
		TestTrue(TEXT("Took"), Plate.bTook);
		TestTrue(TEXT("No refusal"), Plate.Refusal.IsEmpty());
		TestEqual(TEXT("Three plate"), Plate.Taken.Num() == 1 ? Plate.Taken[0].Quantity : 0, 3);
		TestTrue(TEXT("Ore left"), Plate.bHasLeft && Plate.Left.Items.Num() == 1);
	}

	FBackendLootResult Last;

	if (TestTrue(TEXT("The last taken parses"), FSpaceMMODeathWire::ParseLootResult(RealTookTheLast, Last)))
	{
		TestFalse(TEXT("Nothing left: the crate goes"), Last.bHasLeft);
	}

	FBackendLootResult Gone;

	if (TestTrue(TEXT("A refusal parses"), FSpaceMMODeathWire::ParseLootResult(RealGone, Gone)))
	{
		TestFalse(TEXT("Took nothing"), Gone.bTook);
		TestEqual(TEXT("Gone"), Gone.Refusal, FString(TEXT("gone")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMODeathSendsWhatTheServerReadsTest,
	"SpaceMMO.Death.SendsWhatTheServerReads",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMODeathSendsWhatTheServerReadsTest::RunTest(const FString& Parameters)
{
	// The bodies DeathEndpointTests posts as they are, character for character: the API reads these.
	FBackendLootItem Plate;
	Plate.ItemDefId = 3;
	Plate.Style = EBackendItemStyle::Martian;
	Plate.ShownStyle = EBackendItemStyle::Martian;

	TestEqual(TEXT("A stack named by its item and its stack's style number"),
		FSpaceMMODeathWire::MakeLootBody(1, {Plate}),
		FString(TEXT(R"({"characterId":1,"items":[{"itemDefId":3,"style":2}]})")));

	// Ore's stack is Common, whatever world it shows: the stack's own style is what the server keys it by.
	FBackendLootItem Ore;
	Ore.ItemDefId = 2;
	Ore.Style = EBackendItemStyle::Common;
	Ore.ShownStyle = EBackendItemStyle::Terran;

	TestEqual(TEXT("Ore by its stack's style, not the world it shows"),
		FSpaceMMODeathWire::MakeLootBody(1, {Ore}),
		FString(TEXT(R"({"characterId":1,"items":[{"itemDefId":2,"style":0}]})")));

	TestEqual(TEXT("Nothing named takes all"),
		FSpaceMMODeathWire::MakeLootBody(1, {}), FString(TEXT(R"({"characterId":1})")));

	TestEqual(TEXT("Where the body fell, to nine places"),
		FSpaceMMODeathWire::MakeDeathBody(2, FVector(-180.25, 3.5, -1.125)),
		FString(TEXT(R"({"characterId":2,"x":-180.250000000,"y":3.500000000,"z":-1.125000000})")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMODeathPanelListsWhatWasLostTest,
	"SpaceMMO.Death.ThePanelListsWhatWasLost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMODeathPanelListsWhatWasLostTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMODeathCostTests;
	using EKind = FSpaceMMODeathLine::EKind;

	auto Describe = [](const TArray<FSpaceMMODeathLine>& Lines)
	{
		TArray<FString> Out;

		for (const FSpaceMMODeathLine& Line : Lines)
		{
			const TCHAR* Kind = Line.Kind == EKind::Heading ? TEXT("H") : Line.Kind == EKind::Row ? TEXT("R") : TEXT("N");
			Out.Add(FString::Printf(TEXT("%s|%s|%s|%s"), Kind, *Line.Name, *Line.Middle, *Line.Right));
		}

		return FString::Join(Out, TEXT("\n"));
	};

	// Joe's table, from the server's own answer.
	FBackendDeathCosts Costs;
	FSpaceMMODeathWire::ParseDeathCosts(RealCosts, Costs);

	TestEqual(TEXT("Each material, then the worn gear"), Describe(USpaceMMOCombatHud::DeathLines(Costs)), FString(
		TEXT("H|Lost|Dropped|Destroyed\n")
		TEXT("R|Ferrite Ore|3|3\n")
		TEXT("R|Structural Plate|3|2\n")
		TEXT("N|Dropped where you fell for 10 minutes. Anyone can take it.||\n")
		TEXT("H|Worn gear||−10 condition\n")
		TEXT("R|Laser Rifle||100 → 90")));

	FBackendDeathCosts Docked;
	Docked.bApplied = true;
	Docked.bAtStation = true;

	TestEqual(TEXT("At a station, only that"), Describe(USpaceMMOCombatHud::DeathLines(Docked)),
		FString(TEXT("N|You died at a station: nothing was lost.||")));

	FBackendDeathCosts EmptyPockets = Costs;
	EmptyPockets.Lost.Reset();

	TestEqual(TEXT("Nothing carried, and the gear still wore"), Describe(USpaceMMOCombatHud::DeathLines(EmptyPockets)), FString(
		TEXT("N|You carried no materials.||\n")
		TEXT("H|Worn gear||−10 condition\n")
		TEXT("R|Laser Rifle||100 → 90")));

	// Nowhere to drop it -- a character the world never placed -- destroys the lot, and says nothing of a crate.
	FBackendDeathCosts Unplaced = Costs;
	Unplaced.Worn.Reset();

	for (FBackendDeathLoss& Loss : Unplaced.Lost)
	{
		Loss.Destroyed += Loss.Dropped;
		Loss.Dropped = 0;
	}

	TestFalse(TEXT("No crate, no word of one"), Describe(USpaceMMOCombatHud::DeathLines(Unplaced)).Contains(TEXT("Dropped where")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMODeathLootWordsTest,
	"SpaceMMO.Death.LootWordsAndTimes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMODeathLootWordsTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMODeathCostTests;

	TestEqual(TEXT("Ten minutes"), USpaceMMOLootScreen::TimeLeft(600.0), FString(TEXT("10:00")));
	TestEqual(TEXT("The mock's 9:12"), USpaceMMOLootScreen::TimeLeft(552.0), FString(TEXT("9:12")));
	TestEqual(TEXT("A part second still shows as one"), USpaceMMOLootScreen::TimeLeft(0.2), FString(TEXT("0:01")));
	TestEqual(TEXT("Gone"), USpaceMMOLootScreen::TimeLeft(0.0), FString(TEXT("0:00")));
	TestEqual(TEXT("Never negative"), USpaceMMOLootScreen::TimeLeft(-3.0), FString(TEXT("0:00")));

	TestEqual(TEXT("Whose"), USpaceMMOLootScreen::Title(TEXT("Ayla1204")), FString(TEXT("Dropped by Ayla1204")));

	TArray<FBackendLootContainer> Live;
	FSpaceMMODeathWire::ParseContainers(RealContainers, Live);

	TestEqual(TEXT("What the prompt says is in it"), USpaceMMOLootPrompt::Contents(Live[0].Items),
		FString(TEXT("Ferrite Ore 3 · Structural Plate 3")));

	FBackendLootResult Plate;
	FSpaceMMODeathWire::ParseLootResult(RealTookPlate, Plate);

	TestEqual(TEXT("What a looter is told"), USpaceMMOLootComponent::LootMessage(Plate.Taken, Plate.Refusal),
		FString(TEXT("Took 3 Structural Plate")));
	TestEqual(TEXT("Full pockets"), USpaceMMOLootComponent::LootMessage({}, TEXT("no_room")),
		FString(TEXT("No room in your pockets")));
	TestEqual(TEXT("Gone"), USpaceMMOLootComponent::LootMessage({}, TEXT("gone")), FString(TEXT("It's gone")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMODeathCrateTakesETest,
	"SpaceMMO.Death.ACrateTakesEOnlyWhenNearerThanTheRock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMODeathCrateTakesETest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("A crate and no rock"), USpaceMMOLootComponent::CrateTakesE(true, 4.0, false, 0.0));
	TestTrue(TEXT("The crate nearer"), USpaceMMOLootComponent::CrateTakesE(true, 2.0, true, 6.0));
	TestFalse(TEXT("The rock nearer"), USpaceMMOLootComponent::CrateTakesE(true, 4.0, true, 1.5));
	TestFalse(TEXT("No crate"), USpaceMMOLootComponent::CrateTakesE(false, 0.0, true, 3.0));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMODeathBeamFadesTest,
	"SpaceMMO.Death.TheBeamFadesOverItsLastMinute",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMODeathBeamFadesTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Full for nine minutes"), ASpaceMMOLootCrateActor::BeamStrength(600.0), 1.0f);
	TestEqual(TEXT("Full at a minute"), ASpaceMMOLootCrateActor::BeamStrength(60.0), 1.0f);
	TestEqual(TEXT("Half at thirty seconds"), ASpaceMMOLootCrateActor::BeamStrength(30.0), 0.5f);
	TestEqual(TEXT("Nothing at the end"), ASpaceMMOLootCrateActor::BeamStrength(0.0), 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMODeathCrateLiesBesideTheBodyTest,
	"SpaceMMO.Death.ACrateLiesBesideTheBodyOnTheGround",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMODeathCrateLiesBesideTheBodyTest::RunTest(const FString& Parameters)
{
	FPlanetConfig Planet;
	Planet.Centre = FSystemCoordinate(FVector(-200.0, 0.0, 0.0));
	Planet.RadiusKilometres = 20.0;

	// Real relief: half a kilometre, so "on the ground" means the height function's ground and not the sphere's.
	FPlanetTerrainConfig Terrain;
	Terrain.Seed = 20260801;
	Terrain.MaxElevationKilometres = 0.5;

	int32 Checked = 0;

	for (const FVector& Direction : {FVector(1, 0.2, 0.1), FVector(-0.3, 1, 0.4), FVector(0.1, -0.5, 1), FVector(0.7, 0.7, -0.2)})
	{
		const FSystemCoordinate Feet = FPlanetTerrain::SurfacePosition(Planet, Terrain, Direction);
		const FVector Up = Direction.GetSafeNormal();
		const FVector Right = FVector::CrossProduct(Up, FVector(0.0, 0.0, 1.0)).GetSafeNormal();

		const FSystemCoordinate Crate = ASpaceMMOLootCrateActor::BesideTheBody(Planet, Terrain, Feet, Right);

		const double Altitude = FPlanetTerrain::AltitudeAboveGroundKilometres(Planet, Terrain, Crate) * 1000.0;
		const double Apart = FVector::Distance(Crate.Kilometres, Feet.Kilometres) * 1000.0;

		// On the ground to the centimetre; a metre away, give or take the slope it was let down onto.
		TestTrue(FString::Printf(TEXT("On the ground (%.4f m)"), Altitude), FMath::Abs(Altitude) < 0.01);
		TestTrue(FString::Printf(TEXT("Beside, not under (%.3f m)"), Apart), Apart > 0.9 && Apart < 1.5);

		// To the right, not anywhere a metre off.
		TestTrue(TEXT("On the right"), FVector::DotProduct(Crate.Kilometres - Feet.Kilometres, Right) > 0.0);

		++Checked;
	}

	TestEqual(TEXT("Four places measured"), Checked, 4);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMODeathOnlyThePocketsTakeLootTest,
	"SpaceMMO.Death.OnlyThePocketsTakeWhatIsDraggedFromACrate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMODeathOnlyThePocketsTakeLootTest::RunTest(const FString& Parameters)
{
	FBackendInventoryContainer Pockets;
	Pockets.InventoryId = 10;
	Pockets.Kind = EBackendInventoryKind::CharacterCarried;

	FBackendInventoryContainer Hold;
	Hold.InventoryId = 11;
	Hold.Kind = EBackendInventoryKind::ShipHold;

	FBackendInventoryItem Ore;
	Ore.InventoryId = 10;
	Ore.Kind = EBackendInventoryKind::CharacterCarried;
	Ore.ItemDefId = 2;
	Ore.Name = TEXT("Ferrite Ore");
	Ore.Quantity = 4;

	FBackendInventoryItem Held = Ore;
	Held.InventoryId = 11;
	Held.Kind = EBackendInventoryKind::ShipHold;

	const TArray<FSpaceMMOInventoryLine> Lines =
		USpaceMMOInventoryScreen::Build({Ore, Held}, {}, {Pockets, Hold}, {}, 0);

	int32 Carried = 0;
	int32 Elsewhere = 0;

	for (const FSpaceMMOInventoryLine& Line : Lines)
	{
		(Line.InventoryId == 10 ? Carried : Elsewhere) += Line.bCarried == (Line.InventoryId == 10) ? 1 : 0;

		TestEqual(FString::Printf(TEXT("%s in %lld"), *Line.Label, Line.InventoryId), Line.bCarried, Line.InventoryId == 10);
	}

	// The heading and the ore in each: a drop lands on either.
	TestEqual(TEXT("The pockets' heading and ore take it"), Carried, 2);
	TestEqual(TEXT("The hold's do not"), Elsewhere, 2);

	return true;
}

#endif
