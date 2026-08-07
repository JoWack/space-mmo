#include "Misc/AutomationTest.h"
#include "SpaceMMOBackendProtocol.h"
#include "SpaceMMOStationActor.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	const FBackendStation* Find(const TArray<FBackendStation>& Stations, const TCHAR* Key)
	{
		return Stations.FindByPredicate(
			[Key](const FBackendStation& Station) { return Station.Key == Key; });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOStationParsesBothPlacementsTest,
	"SpaceMMO.Station.ParsesBothPlacements",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOStationParsesBothPlacementsTest::RunTest(const FString& Parameters)
{
	// The shape the server actually sends: one record per station with nulls for whichever
	// position does not apply.
	const FString Json = TEXT(R"([
		{
			"id": 1, "key": "station_ground", "name": "Ground", "starSystemId": 1,
			"bodyId": 5, "kind": "TradingHub",
			"directionX": -2.0, "directionY": 0.0, "directionZ": 0.0,
			"systemX": null, "systemY": null, "systemZ": null,
			"dockingRangeKm": 5.0
		},
		{
			"id": 2, "key": "station_deep", "name": "Deep", "starSystemId": 1,
			"bodyId": null, "kind": "Spaceport",
			"directionX": null, "directionY": null, "directionZ": null,
			"systemX": 30.0, "systemY": 12.0, "systemZ": 4.0,
			"dockingRangeKm": 8.0
		}
	])");

	TArray<FBackendStation> Stations;

	TestTrue(TEXT("Parsed"), FSpaceMMOBackendProtocol::ParseStations(Json, Stations));
	TestEqual(TEXT("Both kept"), Stations.Num(), 2);

	const FBackendStation* Ground = Find(Stations, TEXT("station_ground"));
	const FBackendStation* Deep = Find(Stations, TEXT("station_deep"));

	if (Ground == nullptr || Deep == nullptr)
	{
		AddError(TEXT("A station went missing."));

		return false;
	}

	TestTrue(TEXT("Ground is placed"), Ground->bPlaced);
	TestTrue(TEXT("Ground is on a body"), Ground->bOnBody);

	// Normalised on arrival. The direction above is length two on purpose: unnormalised, every
	// station on the planet would be displaced by a common factor, which reads as a terrain fault
	// rather than a parsing one.
	TestTrue(
		TEXT("Direction is a unit vector"),
		FMath::IsNearlyEqual(Ground->Direction.Size(), 1.0, 0.0001));

	TestTrue(TEXT("Deep is placed"), Deep->bPlaced);
	TestFalse(TEXT("Deep is not on a body"), Deep->bOnBody);
	TestEqual(TEXT("Deep kept its coordinate"), Deep->Position.Kilometres.X, 30.0);
	TestEqual(TEXT("Deep kept its docking range"), Deep->DockingRangeKilometres, 8.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOStationKeepsUnplacedOnesTest,
	"SpaceMMO.Station.KeepsUnplacedOnes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOStationKeepsUnplacedOnesTest::RunTest(const FString& Parameters)
{
	// A station authored before anybody decided where it stands. Dropping it would make
	// unfinished content indistinguishable from missing content, and a zero direction has to be
	// treated as no position rather than normalised into a NaN.
	const FString Json = TEXT(R"([
		{ "id": 3, "key": "station_planned", "name": "Planned", "bodyId": 5, "kind": "TradingHub",
		  "dockingRangeKm": 5.0 },
		{ "id": 4, "key": "station_zero", "name": "Zero", "bodyId": 5, "kind": "TradingHub",
		  "directionX": 0.0, "directionY": 0.0, "directionZ": 0.0, "dockingRangeKm": 5.0 }
	])");

	TArray<FBackendStation> Stations;

	TestTrue(TEXT("Parsed"), FSpaceMMOBackendProtocol::ParseStations(Json, Stations));
	TestEqual(TEXT("Both kept, neither dropped"), Stations.Num(), 2);

	for (const FBackendStation& Station : Stations)
	{
		TestFalse(
			FString::Printf(TEXT("%s is unplaced"), *Station.Key), Station.bPlaced);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOStationDockingRangeTest,
	"SpaceMMO.Station.DockingRange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOStationDockingRangeTest::RunTest(const FString& Parameters)
{
	FBackendStation Station;
	Station.bPlaced = true;
	Station.DockingRangeKilometres = 5.0;

	const FSystemCoordinate At(FVector(100.0, 0.0, 0.0));

	TestTrue(
		TEXT("Alongside it"),
		ASpaceMMOStationActor::IsWithinDockingRange(Station, At, At));

	TestTrue(
		TEXT("Exactly at the limit still docks"),
		ASpaceMMOStationActor::IsWithinDockingRange(
			Station, At, FSystemCoordinate(FVector(105.0, 0.0, 0.0))));

	TestFalse(
		TEXT("Just past it does not"),
		ASpaceMMOStationActor::IsWithinDockingRange(
			Station, At, FSystemCoordinate(FVector(105.1, 0.0, 0.0))));

	// An unplaced station would otherwise sit at the system origin and accept anyone standing
	// near (0,0,0) -- which is exactly where a ship starts.
	FBackendStation Unplaced;
	Unplaced.bPlaced = false;
	Unplaced.DockingRangeKilometres = 5.0;

	TestFalse(
		TEXT("An unplaced station is never dockable"),
		ASpaceMMOStationActor::IsWithinDockingRange(
			Unplaced, FSystemCoordinate(), FSystemCoordinate()));

	// Zero range is what an unmigrated column looks like, and it must refuse rather than accept
	// only a perfect coordinate match.
	FBackendStation Sealed;
	Sealed.bPlaced = true;
	Sealed.DockingRangeKilometres = 0.0;

	TestFalse(
		TEXT("Zero range refuses even a direct hit"),
		ASpaceMMOStationActor::IsWithinDockingRange(Sealed, At, At));

	return true;
}

#endif
