#include "Misc/AutomationTest.h"
#include "SpaceMMOStationSettings.h"
#include "Misc/FileHelper.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/Paths.h"
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


/**
 * A station is drawn as its kind, and one named station may differ from its kind.
 *
 * The resolution order is the whole behaviour: an override that the general case could beat would
 * not be an override.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOStationLookFollowsKeyThenKindTest,
	"SpaceMMO.Station.LookFollowsKeyThenKind",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOStationLookFollowsKeyThenKindTest::RunTest(const FString& Parameters)
{
	USpaceMMOStationSettings* const Settings = NewObject<USpaceMMOStationSettings>();

	// Emptied first. A config class loads DefaultGame.ini into every instance it constructs, so a
	// fresh object arrives already carrying the shipped mapping -- and a test that then asserted
	// "an unmapped kind resolves to nothing" would be asserting against whatever content happened
	// to be configured that week. These tests are about the resolution order, not the shipped map.
	Settings->MeshesByKind.Empty();
	Settings->MeshesByKey.Empty();

	const TSoftObjectPtr<UStaticMesh> Cube(
		FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube")));

	const TSoftObjectPtr<UStaticMesh> Cone(
		FSoftObjectPath(TEXT("/Engine/BasicShapes/Cone.Cone")));

	Settings->MeshesByKind.Add(TEXT("TradingHub"), Cube);
	Settings->MeshesByKey.Add(TEXT("station_special"), Cone);

	TestEqual(
		TEXT("A station with no override is drawn as its kind"),
		FStationAppearance::MeshFor(*Settings, TEXT("station_plain"), TEXT("TradingHub")).ToString(),
		Cube.ToString());

	TestEqual(
		TEXT("A station with its own mesh beats its kind"),
		FStationAppearance::MeshFor(*Settings, TEXT("station_special"), TEXT("TradingHub"))
			.ToString(),
		Cone.ToString());

	// Null rather than something arbitrary: the caller keeps the placeholder cube, because a
	// station that rendered as nothing reads as one that was never placed.
	TestTrue(
		TEXT("An unmapped kind resolves to nothing, so the placeholder stays"),
		FStationAppearance::MeshFor(*Settings, TEXT("station_plain"), TEXT("Housing")).IsNull());

	TestTrue(
		TEXT("A station with no kind at all resolves to nothing"),
		FStationAppearance::MeshFor(*Settings, FString(), FString()).IsNull());

	return true;
}

/**
 * How large a station stands follows its kind, and falls back rather than vanishing.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOStationSizeFollowsKindTest,
	"SpaceMMO.Station.SizeFollowsKind",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOStationSizeFollowsKindTest::RunTest(const FString& Parameters)
{
	USpaceMMOStationSettings* const Settings = NewObject<USpaceMMOStationSettings>();

	// As above: the shipped sizes arrive with the object, and this is a test of the fallback rules.
	Settings->SizeMetresByKind.Empty();

	Settings->DefaultSizeMetres = 25.0;
	Settings->SizeMetresByKind.Add(TEXT("Housing"), 8.0);

	// A nonsense size is refused rather than taken literally: a station drawn at zero metres is
	// invisible while the log cheerfully says it was placed.
	Settings->SizeMetresByKind.Add(TEXT("Broken"), 0.0);

	TestEqual(
		TEXT("A sized kind uses its size"),
		FStationAppearance::SizeMetresFor(*Settings, TEXT("Housing")),
		8.0,
		0.001);

	TestEqual(
		TEXT("An unsized kind falls back to the default"),
		FStationAppearance::SizeMetresFor(*Settings, TEXT("Spaceport")),
		25.0,
		0.001);

	TestEqual(
		TEXT("A zero size falls back rather than drawing nothing"),
		FStationAppearance::SizeMetresFor(*Settings, TEXT("Broken")),
		25.0,
		0.001);

	return true;
}

/**
 * Fitting a mesh to a size reproduces the constant it replaced, and survives a degenerate one.
 *
 * <strong>The first assertion is the regression.</strong> Station scale used to be a target divided
 * by the engine cube's known hundred centimetres, which is right exactly as long as every station is
 * that cube. This has to give the same answer for that case, or every station in the game changes
 * size the day it lands.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOStationScaleFitsTheMeshTest,
	"SpaceMMO.Station.ScaleFitsTheMesh",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOStationScaleFitsTheMeshTest::RunTest(const FString& Parameters)
{
	// The engine cube: 100 cm on each edge, so half-extents of 50.
	const FVector EngineCube(50.0, 50.0, 50.0);

	TestEqual(
		TEXT("A 25 m station on the engine cube still scales by 25"),
		FStationAppearance::UniformScaleForSize(EngineCube, 25.0 * 100.0),
		25.0,
		0.001);

	// The largest dimension is what is fitted, so nothing sticks out past the size asked for.
	TestEqual(
		TEXT("An oblong is fitted by its longest side"),
		FStationAppearance::UniformScaleForSize(FVector(50.0, 10.0, 10.0), 1000.0),
		10.0,
		0.001);

	TestEqual(
		TEXT("A mesh with no extent is left as authored"),
		FStationAppearance::UniformScaleForSize(FVector::ZeroVector, 2500.0),
		1.0,
		0.001);

	TestEqual(
		TEXT("A zero target leaves the mesh as authored"),
		FStationAppearance::UniformScaleForSize(EngineCube, 0.0),
		1.0,
		0.001);

	return true;
}

/**
 * Every station kind the content actually uses has a look configured.
 *
 * <strong>Config against content, which is where this class of thing goes wrong.</strong> Adding a
 * station of a new kind is a one-line change in <c>origin.json</c>, and nothing about it would look
 * broken: the station is placed, dockable and serving, and simply renders as a grey cube among
 * buildings. This is the check that notices.
 *
 * It reads the authored file rather than a list of kinds written out here, because a list would
 * have to be remembered and this cannot be.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOStationEveryAuthoredKindHasALookTest,
	"SpaceMMO.Station.EveryAuthoredKindHasALook",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOStationEveryAuthoredKindHasALookTest::RunTest(const FString& Parameters)
{
	const FString Path = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("data"), TEXT("universe"),
			TEXT("origin.json")));

	FString Text;

	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		// Not a silent pass. The content is the point, and a path that stopped resolving would
		// turn this green forever.
		AddError(FString::Printf(TEXT("Could not read the authored universe from %s"), *Path));

		return false;
	}

	TSharedPtr<FJsonObject> Root;

	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);

	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		AddError(TEXT("The authored universe is not valid JSON."));

		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Stations = nullptr;

	if (!Root->TryGetArrayField(TEXT("stations"), Stations) || Stations == nullptr)
	{
		AddError(TEXT("The authored universe has no stations array."));

		return false;
	}

	const USpaceMMOStationSettings* const Settings = GetDefault<USpaceMMOStationSettings>();

	if (Settings == nullptr)
	{
		AddError(TEXT("No station settings."));

		return false;
	}

	int32 Checked = 0;

	for (const TSharedPtr<FJsonValue>& Value : *Stations)
	{
		const TSharedPtr<FJsonObject> Station = Value.IsValid() ? Value->AsObject() : nullptr;

		if (!Station.IsValid())
		{
			continue;
		}

		FString Kind;
		FString Key;

		Station->TryGetStringField(TEXT("key"), Key);

		if (!Station->TryGetStringField(TEXT("kind"), Kind) || Kind.IsEmpty())
		{
			continue;
		}

		// The kind as the server sends it: WorldEndpoints serialises the enum by name, so the
		// string in content is the string the client matches on.
		TestFalse(
			FString::Printf(TEXT("Station kind '%s' has a mesh configured"), *Kind),
			FStationAppearance::MeshFor(*Settings, Key, Kind).IsNull());

		TestTrue(
			FString::Printf(TEXT("Station kind '%s' has a sensible size"), *Kind),
			FStationAppearance::SizeMetresFor(*Settings, Kind) > 0.0);

		++Checked;
	}

	TestTrue(TEXT("At least one authored station was checked"), Checked > 0);

	return true;
}

#endif
