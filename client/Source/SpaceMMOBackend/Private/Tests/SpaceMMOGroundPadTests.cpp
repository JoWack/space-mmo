#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "SpaceMMOBackendProtocol.h"
#include "SpaceMMOBackendTypes.h"
#include "SpaceMMOPlanetTerrain.h"
#include "SpaceMMOWorldSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The ground a city stands on (task 168), measured against the terrain it levels and the city it
 * carries -- not against the numbers that configure it.
 *
 * Here rather than beside the terrain tests because they read the authored JSON, and Json is linked in
 * this module and not in SpaceMMOCore.
 */

// Named, not anonymous: in a unity build an anonymous namespace's helpers are visible to every test file
// compiled after this one, and SpaceMMOSettlementTests.cpp's own ReadVector then became ambiguous.
namespace SpaceMMOGroundPadTests
{
	bool ReadJsonFile(const FString& Path, TSharedPtr<FJsonObject>& OutRoot)
	{
		FString Text;

		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			return false;
		}

		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);

		return FJsonSerializer::Deserialize(Reader, OutRoot) && OutRoot.IsValid();
	}

	FString RepoPath(const TCHAR* A, const TCHAR* B, const TCHAR* C, const TCHAR* D = nullptr,
		const TCHAR* E = nullptr)
	{
		FString Path = FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), A, B, C);

		if (D != nullptr)
		{
			Path = FPaths::Combine(Path, D);
		}

		if (E != nullptr)
		{
			Path = FPaths::Combine(Path, E);
		}

		return FPaths::ConvertRelativePathToFull(Path);
	}

	FVector ReadVector(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		const TArray<TSharedPtr<FJsonValue>>* Components = nullptr;

		if (!Object->TryGetArrayField(Field, Components) || Components == nullptr || Components->Num() != 3)
		{
			return FVector::ZeroVector;
		}

		return FVector((*Components)[0]->AsNumber(), (*Components)[1]->AsNumber(),
			(*Components)[2]->AsNumber());
	}

	/** A body's ground exactly as the paint pass builds it, without any pad. */
	FPlanetTerrainConfig AuthoredTerrain(const TSharedPtr<FJsonObject>& Body)
	{
		FPlanetTerrainConfig Terrain = USpaceMMOWorldSubsystem::StartingPlanetTerrain();
		const TSharedPtr<FJsonObject>* Shape = nullptr;

		if (Body->TryGetObjectField(TEXT("terrain"), Shape) && Shape != nullptr)
		{
			Terrain.Seed = static_cast<int64>((*Shape)->GetNumberField(TEXT("seed")));
			Terrain.MaxElevationKilometres = (*Shape)->GetNumberField(TEXT("maxElevationKm"));
			Terrain.BaseFrequency = (*Shape)->GetNumberField(TEXT("baseFrequency"));
		}

		Terrain.Pads.Reset();

		return Terrain;
	}

	FVector AlongSurface(const FVector& Centre, const FVector& Tangent, const double Metres,
		const double RadiusKilometres)
	{
		const double Angle = Metres / 1000.0 / RadiusKilometres;

		return (Centre * FMath::Cos(Angle) + Tangent * FMath::Sin(Angle)).GetSafeNormal();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOAuthoredPadsFitTheirCitiesTest,
	"SpaceMMO.Terrain.AuthoredPadsFitTheirCities",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOAuthoredPadsFitTheirCitiesTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMOGroundPadTests;

	// Three ways an authored pad can be wrong and look right in the JSON:
	//
	//  - too small for the city on it, so its corner docks stand on the slope of their own hill;
	//  - too low, so a hillock of the natural ground pokes up through the paving -- the city is
	//    built 30 cm above its pad and nothing else stops it;
	//  - so far from the land around it that the blend ring is a cliff, with no step-up to climb.
	//
	// Each is measured: the first against the manifest the city's own build script wrote, the other
	// two against the real terrain of the real body, evaluated the way the game evaluates it.
	TSharedPtr<FJsonObject> Universe;
	TSharedPtr<FJsonObject> Borlash;

	if (!TestTrue(TEXT("origin.json reads"),
			ReadJsonFile(RepoPath(TEXT("data"), TEXT("universe"), TEXT("origin.json")), Universe))
		|| !TestTrue(TEXT("Borlash's manifest reads"),
			ReadJsonFile(RepoPath(TEXT("client"), TEXT("RawContent"), TEXT("Stations"),
				TEXT("A07_BorlashCity"), TEXT("Borlash_manifest.json")), Borlash)))
	{
		return false;
	}

	const double CityNeedsMetres =
		Borlash->GetObjectField(TEXT("pad_required"))->GetNumberField(TEXT("flat_radius_m"));

	const double RadiusKilometres = USpaceMMOWorldSubsystem::StartingPlanet().RadiusKilometres;

	TMap<FString, TSharedPtr<FJsonObject>> Bodies;

	for (const TSharedPtr<FJsonValue>& Value : Universe->GetArrayField(TEXT("bodies")))
	{
		const TSharedPtr<FJsonObject> Body = Value->AsObject();
		Bodies.Add(Body->GetStringField(TEXT("key")), Body);
	}

	int32 Measured = 0;

	for (const TSharedPtr<FJsonValue>& Value : Universe->GetArrayField(TEXT("stations")))
	{
		const TSharedPtr<FJsonObject> Station = Value->AsObject();
		const TSharedPtr<FJsonObject>* PadObject = nullptr;

		if (!Station->TryGetObjectField(TEXT("pad"), PadObject) || PadObject == nullptr)
		{
			continue;
		}

		const FString Key = Station->GetStringField(TEXT("key"));
		const double FlatKm = (*PadObject)->GetNumberField(TEXT("flatRadiusKm"));
		const double BlendKm = (*PadObject)->GetNumberField(TEXT("blendKm"));
		const double ElevationKm = (*PadObject)->GetNumberField(TEXT("elevationKm"));

		// The one city with a manifest. A second levelled station would want its own, and saying so
		// here is better than letting it pass against Borlash's numbers.
		if (Key == TEXT("station_capital_hub"))
		{
			TestTrue(
				*FString::Printf(TEXT("%s is flat for %.0f m and Borlash needs %.0f m"),
					*Key, FlatKm * 1000.0, CityNeedsMetres),
				FlatKm * 1000.0 >= CityNeedsMetres);
		}
		else
		{
			AddError(FString::Printf(TEXT("%s levels ground but no city manifest is checked against it."),
				*Key));
		}

		const TSharedPtr<FJsonObject>* Body = Bodies.Find(Station->GetStringField(TEXT("body")));

		if (!TestNotNull(*FString::Printf(TEXT("%s's body is authored"), *Key), Body))
		{
			continue;
		}

		const FPlanetTerrainConfig Land = AuthoredTerrain(*Body);
		FPlanetTerrainConfig Levelled = Land;
		const FVector Centre = ReadVector(Station, TEXT("direction")).GetSafeNormal();

		Levelled.Pads.Add(FPlanetTerrain::MakePad(Key, Centre, FlatKm, BlendKm, ElevationKm, RadiusKilometres));

		const FVector East = FVector::CrossProduct(
			FMath::Abs(Centre.Z) < 0.9 ? FVector::ZAxisVector : FVector::XAxisVector, Centre).GetSafeNormal();
		const FVector North = FVector::CrossProduct(Centre, East).GetSafeNormal();

		double HighestLandInside = 0.0;
		double SteepestDegrees = 0.0;

		for (int32 Ray = 0; Ray < 24; ++Ray)
		{
			const double Bearing = 2.0 * UE_DOUBLE_PI * Ray / 24.0;
			const FVector Tangent = (East * FMath::Cos(Bearing) + North * FMath::Sin(Bearing)).GetSafeNormal();

			for (double Metres = 0.0; Metres <= FlatKm * 1000.0; Metres += 10.0)
			{
				HighestLandInside = FMath::Max(HighestLandInside,
					FPlanetTerrain::ElevationKilometres(Land, AlongSurface(Centre, Tangent, Metres, RadiusKilometres)));
			}

			double Previous = FPlanetTerrain::ElevationKilometres(
				Levelled, AlongSurface(Centre, Tangent, FlatKm * 1000.0, RadiusKilometres));

			for (double Metres = FlatKm * 1000.0 + 2.0; Metres <= (FlatKm + BlendKm) * 1000.0 + 20.0; Metres += 2.0)
			{
				const double Height = FPlanetTerrain::ElevationKilometres(
					Levelled, AlongSurface(Centre, Tangent, Metres, RadiusKilometres));

				SteepestDegrees = FMath::Max(SteepestDegrees,
					FMath::RadiansToDegrees(FMath::Atan(FMath::Abs(Height - Previous) * 1000.0 / 2.0)));
				Previous = Height;
			}
		}

		TestTrue(
			*FString::Printf(TEXT("%s stands at %.1f m and the highest ground under it is %.1f m"),
				*Key, ElevationKm * 1000.0, HighestLandInside * 1000.0),
			ElevationKm >= HighestLandInside - 0.0005);

		TestTrue(
			*FString::Printf(TEXT("%s blends into the land at %.1f degrees at its steepest"), *Key,
				SteepestDegrees),
			SteepestDegrees <= 25.0);

		++Measured;
	}

	TestTrue(TEXT("Measured at least one pad: Borlash needs one"), Measured > 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOTerrainPadsArriveFromTheWireTest,
	"SpaceMMO.Terrain.PadsArriveFromTheWire",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOTerrainPadsArriveFromTheWireTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMOGroundPadTests;

	// Captured from a running server after seeding, not written by hand (task 168): Ares' and the
	// Capital's entries from GET /world/bodies on 2 October, verbatim -- including "directionY":0 and
	// "directionZ":1 as integers, which a hand-typed fixture would have written as 0.0 and 1.0. The direction is the station's as stored -- not unit
	// length -- and the three sizes are different numbers, so a parser that read one field into
	// another fails here rather than in a playtest.
	const FString Payload = TEXT(R"__([{"id":2,"key":"body_ares","name":"Ares","starSystemId":1,"radiusKm":339,"systemX":40,"systemY":-180,"systemZ":30,"lowColour":"0.35,0.12,0.07","highColour":"0.72,0.42,0.26","rockColour":"0.18,0.15,0.14","heightFrom":0.22,"heightTo":0.75,"slopeFrom":0.15,"slopeTo":0.45,"terrainSeed":20260802,"maxElevationKm":0.5,"baseFrequency":12,"terrainPads":[]},{"id":5,"key":"body_capital","name":"The Capital","starSystemId":1,"radiusKm":700,"systemX":60,"systemY":0,"systemZ":0,"lowColour":"0.16,0.19,0.15","highColour":"0.62,0.60,0.55","rockColour":"0.34,0.33,0.32","heightFrom":0.24,"heightTo":0.68,"slopeFrom":0.08,"slopeTo":0.19,"terrainSeed":20260805,"maxElevationKm":0.35,"baseFrequency":6,"terrainPads":[{"stationKey":"station_capital_hub","directionX":0.026,"directionY":0,"directionZ":1,"flatRadiusKm":0.42,"blendKm":0.15,"elevationKm":0.172}]}])__");

	TArray<FBackendBody> Bodies;

	TestTrue(TEXT("Parsed"), FSpaceMMOBackendProtocol::ParseBodies(Payload, Bodies));

	const FBackendBody* Capital =
		Bodies.FindByPredicate([](const FBackendBody& Body) { return Body.Key == TEXT("body_capital"); });

	if (!TestNotNull(TEXT("The Capital arrived"), Capital))
	{
		return false;
	}

	if (!TestEqual(TEXT("One pad"), Capital->TerrainPads.Num(), 1))
	{
		return false;
	}

	const FBackendTerrainPad& Pad = Capital->TerrainPads[0];

	TestEqual(TEXT("Station"), Pad.StationKey, FString(TEXT("station_capital_hub")));
	TestEqual(TEXT("Direction X"), Pad.Direction.X, 0.026);
	TestEqual(TEXT("Direction Y"), Pad.Direction.Y, 0.0);
	TestEqual(TEXT("Direction Z"), Pad.Direction.Z, 1.0);
	TestEqual(TEXT("Flat"), Pad.FlatRadiusKilometres, 0.42);
	TestEqual(TEXT("Blend"), Pad.BlendKilometres, 0.15);
	TestEqual(TEXT("Elevation"), Pad.ElevationKilometres, 0.172);

	// And a body that levels nothing arrives with no pads, rather than with a default one.
	for (const FBackendBody& Body : Bodies)
	{
		if (Body.Key != TEXT("body_capital"))
		{
			TestEqual(*FString::Printf(TEXT("%s levels nothing"), *Body.Key), Body.TerrainPads.Num(), 0);
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOCityWaterAboveGroundTest,
	"SpaceMMO.Terrain.CityWaterStandsAboveTheGround",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOCityWaterAboveGroundTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMOGroundPadTests;

	// Water drawn below the ground under it is not water: the terrain covers it. Borlash's canal was
	// drawn 0.40 m under the paving where the levelled ground is only 0.03 m under it, because the
	// ground is a sphere and the city is a plane, and nothing measured it until 2 October.
	//
	// Measured against the game's own terrain -- the pad the server sends, evaluated by the function the
	// mesh and the physics use -- at the distance from the centre where each surface comes nearest it,
	// which is where the ground stands highest under that water.
	TSharedPtr<FJsonObject> Universe;
	TSharedPtr<FJsonObject> Borlash;

	if (!TestTrue(TEXT("origin.json reads"),
			ReadJsonFile(RepoPath(TEXT("data"), TEXT("universe"), TEXT("origin.json")), Universe))
		|| !TestTrue(TEXT("Borlash's manifest reads"),
			ReadJsonFile(RepoPath(TEXT("client"), TEXT("RawContent"), TEXT("Stations"),
				TEXT("A07_BorlashCity"), TEXT("Borlash_manifest.json")), Borlash)))
	{
		return false;
	}

	TSharedPtr<FJsonObject> Station;
	TSharedPtr<FJsonObject> Body;

	for (const TSharedPtr<FJsonValue>& Value : Universe->GetArrayField(TEXT("stations")))
	{
		if (Value->AsObject()->GetStringField(TEXT("key")) == TEXT("station_capital_hub"))
		{
			Station = Value->AsObject();
		}
	}

	if (!TestTrue(TEXT("Borlash's station is authored"), Station.IsValid()))
	{
		return false;
	}

	for (const TSharedPtr<FJsonValue>& Value : Universe->GetArrayField(TEXT("bodies")))
	{
		if (Value->AsObject()->GetStringField(TEXT("key")) == Station->GetStringField(TEXT("body")))
		{
			Body = Value->AsObject();
		}
	}

	if (!TestTrue(TEXT("Its body is authored"), Body.IsValid()))
	{
		return false;
	}

	const TSharedPtr<FJsonObject> PadObject = Station->GetObjectField(TEXT("pad"));
	const double RadiusKilometres = USpaceMMOWorldSubsystem::StartingPlanet().RadiusKilometres;
	const FVector Centre = ReadVector(Station, TEXT("direction")).GetSafeNormal();

	FPlanetTerrainConfig Levelled = AuthoredTerrain(Body);
	Levelled.Pads.Add(FPlanetTerrain::MakePad(TEXT("station_capital_hub"), Centre,
		PadObject->GetNumberField(TEXT("flatRadiusKm")), PadObject->GetNumberField(TEXT("blendKm")),
		PadObject->GetNumberField(TEXT("elevationKm")), RadiusKilometres));

	FPlanetConfig Planet = USpaceMMOWorldSubsystem::StartingPlanet();
	Planet.Centre = FSystemCoordinate(FVector::ZeroVector);

	// The city's origin: the levelled ground at its centre, where its plane touches the sphere.
	const FVector Origin = FPlanetTerrain::SurfacePosition(Planet, Levelled, Centre).Kilometres;

	const FVector East = FVector::CrossProduct(
		FMath::Abs(Centre.Z) < 0.9 ? FVector::ZAxisVector : FVector::XAxisVector, Centre).GetSafeNormal();
	const FVector North = FVector::CrossProduct(Centre, East).GetSafeNormal();

	int32 Measured = 0;

	for (const TSharedPtr<FJsonValue>& Value : Borlash->GetArrayField(TEXT("water")))
	{
		const TSharedPtr<FJsonObject> Water = Value->AsObject();
		const FString Name = Water->GetStringField(TEXT("name"));
		const double SurfaceMetres = Water->GetNumberField(TEXT("surface_m"));
		const double NearestMetres = Water->GetNumberField(TEXT("nearest_centre_m"));

		double HighestGroundMetres = -1.0e9;

		for (int32 Ray = 0; Ray < 24; ++Ray)
		{
			const double Bearing = 2.0 * UE_DOUBLE_PI * Ray / 24.0;
			const FVector Tangent = (East * FMath::Cos(Bearing) + North * FMath::Sin(Bearing)).GetSafeNormal();
			const FVector Ground = FPlanetTerrain::SurfacePosition(
				Planet, Levelled, AlongSurface(Centre, Tangent, NearestMetres, RadiusKilometres)).Kilometres;

			HighestGroundMetres =
				FMath::Max(HighestGroundMetres, FVector::DotProduct(Ground - Origin, Centre) * 1000.0);
		}

		TestTrue(
			*FString::Printf(TEXT("Borlash's %s at %+.2f m clears the ground under its nearest edge, %+.3f m"),
				*Name, SurfaceMetres, HighestGroundMetres),
			SurfaceMetres - HighestGroundMetres >= 0.05);

		++Measured;
	}

	// The canal and the lagoon at least: a manifest that lists no water has stopped measuring it.
	TestTrue(TEXT("Borlash's water is listed to be measured"), Measured >= 2);

	return true;
}

#endif
