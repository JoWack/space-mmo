#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PhysicsEngine/BodySetup.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "SpaceMMOAirspace.h"
#include "SpaceMMOBackendProtocol.h"
#include "SpaceMMOBackendTypes.h"
#include "SpaceMMOCharacterPawn.h"
#include "EngineUtils.h"
#include "SpaceMMOPlanetActor.h"
#include "SpaceMMOPlanetTerrain.h"
#include "SpaceMMORenderOrigin.h"
#include "SpaceMMOSettlement.h"
#include "SpaceMMOStationActor.h"
#include "SpaceMMOStationMarkers.h"
#include "SpaceMMOWorldSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Borlash as the game builds it (tasks 168 and 169): the Capital's station spawned in a world from
 * what the server sends, wearing the Blueprint the importer made, measured against the manifest the
 * city's build script wrote.
 *
 * <strong>Every other check stops one step short of this.</strong> The importer compares meshes with
 * the manifest; the airspace tests use numbers typed into the test. Neither notices a Blueprint path
 * missing from DefaultGame.ini, berths placed in the wrong frame, a Y axis not flipped, or a docking
 * range that leaves a pilot undocked the moment they are set down -- and none of those can be seen
 * without flying to all four docks.
 */

namespace SpaceMMOSettlementTests
{
	// Captured 2 October from a server seeded with Borlash (task 169), verbatim: Borlash's entry from
	// GET /world/stations and the Capital's from GET /world/bodies. Integers where the server sends
	// integers ("directionY":0), which a hand-typed fixture would not.
	const TCHAR* const StationsPayload = TEXT(R"__([{"id":1,"key":"station_capital_hub","name":"Borlash","starSystemId":1,"bodyId":5,"kind":"Capital","directionX":0.026,"directionY":0,"directionZ":1,"systemX":null,"systemY":null,"systemZ":null,"dockingRangeKm":0.42}])__");

	const TCHAR* const BodiesPayload = TEXT(R"__([{"id":5,"key":"body_capital","name":"The Capital","starSystemId":1,"radiusKm":700,"systemX":60,"systemY":0,"systemZ":0,"lowColour":"0.16,0.19,0.15","highColour":"0.62,0.60,0.55","rockColour":"0.34,0.33,0.32","heightFrom":0.24,"heightTo":0.68,"slopeFrom":0.08,"slopeTo":0.19,"terrainSeed":20260805,"maxElevationKm":0.35,"baseFrequency":6,"terrainPads":[{"stationKey":"station_capital_hub","directionX":0.026,"directionY":0,"directionZ":1,"flatRadiusKm":0.42,"blendKm":0.15,"elevationKm":0.172}]}])__");

	bool ReadManifest(TSharedPtr<FJsonObject>& OutRoot)
	{
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(
			FPaths::ProjectDir(), TEXT("RawContent"), TEXT("Stations"), TEXT("A07_BorlashCity"),
			TEXT("Borlash_manifest.json")));

		FString Text;

		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			return false;
		}

		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);

		return FJsonSerializer::Deserialize(Reader, OutRoot) && OutRoot.IsValid();
	}

	FVector ReadVector(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		const TArray<TSharedPtr<FJsonValue>>& Components = Object->GetArrayField(Field);

		return Components.Num() == 3
			? FVector(Components[0]->AsNumber(), Components[1]->AsNumber(), Components[2]->AsNumber())
			: FVector::ZeroVector;
	}

	/** A game world for one test, torn down however the test leaves. */
	struct FScopedWorld
	{
		UWorld* World = nullptr;

		FScopedWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("SpaceMMOSettlementTest"));

			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);

			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
		}

		~FScopedWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};

	/** Across the ground, ignoring height: how far apart two points are as somebody walking sees it. */
	double Across(const FSystemCoordinate& A, const FSystemCoordinate& B, const FVector& Up)
	{
		return FVector::VectorPlaneProject(A.Kilometres - B.Kilometres, Up).Size();
	}

	/**
	 * Borlash in a world of its own, built the way the game builds it: the Capital's station spawned
	 * from the captured payloads as SpaceMMODepositSubsystem spawns every station, on a planet shaped
	 * as the paint pass shapes it -- the world's own planet included, so a character stands on the same
	 * ground the city does.
	 */
	struct FBorlashWorld
	{
		FScopedWorld Scoped;
		TSharedPtr<FJsonObject> Manifest;
		FBackendStation Station;
		FPlanetConfig Planet;
		FPlanetTerrainConfig Terrain;
		USpaceMMORenderOriginSubsystem* Origin = nullptr;
		ASpaceMMOStationActor* Placed = nullptr;
		const ASpaceMMOSettlementActor* Settlement = nullptr;
		const FSpaceMMOAirspaceZone* Zone = nullptr;

		bool Build(FAutomationTestBase& Test)
		{
			TArray<FBackendStation> Stations;
			TArray<FBackendBody> Bodies;

			if (!Test.TestTrue(TEXT("Borlash's manifest reads"), ReadManifest(Manifest))
				|| !Test.TestTrue(TEXT("The stations parse"), FSpaceMMOBackendProtocol::ParseStations(StationsPayload, Stations))
				|| !Test.TestTrue(TEXT("The bodies parse"), FSpaceMMOBackendProtocol::ParseBodies(BodiesPayload, Bodies))
				|| !Test.TestEqual(TEXT("One station"), Stations.Num(), 1)
				|| !Test.TestEqual(TEXT("One body"), Bodies.Num(), 1))
			{
				return false;
			}

			Station = Stations[0];
			const FBackendBody& Capital = Bodies[0];

			// The planet as the paint pass shapes it (SpaceMMOTerrainPaintSubsystem.cpp): drawn at the
			// starting planet's radius, centred where the body is, wearing its own seed and its pad.
			Planet = USpaceMMOWorldSubsystem::StartingPlanet();
			Planet.Centre = FSystemCoordinate(Capital.SystemPositionKilometres);

			Terrain = USpaceMMOWorldSubsystem::StartingPlanetTerrain();
			Terrain.Seed = Capital.TerrainSeed;
			Terrain.MaxElevationKilometres = Capital.MaxElevationKilometres;
			Terrain.BaseFrequency = Capital.BaseFrequency;

			for (const FBackendTerrainPad& Pad : Capital.TerrainPads)
			{
				Terrain.Pads.Add(FPlanetTerrain::MakePad(Pad.StationKey, Pad.Direction, Pad.FlatRadiusKilometres,
					Pad.BlendKilometres, Pad.ElevationKilometres, Planet.RadiusKilometres));
			}

			UWorld* const World = Scoped.World;

			// The world's own planet, which its subsystem spawned as the starting planet with no backend
			// to reshape it. Reshaped here as the paint pass would, or a character would stand on ground
			// the city was never levelled onto.
			for (TActorIterator<ASpaceMMOPlanetActor> It(World); It; ++It)
			{
				It->SetPlanetConfig(Planet);
				It->SetTerrainConfig(Terrain);
			}

			Origin = World->GetSubsystem<USpaceMMORenderOriginSubsystem>();

			if (!Test.TestNotNull(TEXT("A render origin"), Origin))
			{
				return false;
			}

			// The camera near the city, as it would be, so the world's centimetres stay small.
			Origin->SetRenderOrigin(FPlanetTerrain::SurfacePosition(Planet, Terrain, Station.Direction));

			Placed = World->SpawnActorDeferred<ASpaceMMOStationActor>(
				ASpaceMMOStationActor::StaticClass(), FTransform::Identity, nullptr, nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

			if (!Test.TestNotNull(TEXT("The station spawned"), Placed))
			{
				return false;
			}

			Placed->Configure(Station, Planet, Terrain);
			Placed->FinishSpawning(FTransform::Identity);

			// A path missing from DefaultGame.ini, or one without its _C, leaves the cube and fails here.
			Settlement = Placed->GetSettlement();

			if (!Test.TestNotNull(TEXT("The Capital's station is drawn as a settlement"), Settlement))
			{
				return false;
			}

			// Once ticked, the station has read its settlement and declared the airspace over it.
			Placed->Tick(0.0f);

			const USpaceMMOAirspaceSubsystem* const Airspace = World->GetSubsystem<USpaceMMOAirspaceSubsystem>();

			Zone = Airspace != nullptr
				? Airspace->GetZones().FindByPredicate(
					[this](const FSpaceMMOAirspaceZone& Each) { return Each.Key == Station.Key; })
				: nullptr;

			if (!Test.TestNotNull(TEXT("Borlash declared its airspace"), Zone))
			{
				return false;
			}

			// The city's collision, built before anything asks it. The editor makes a mesh's collision in the
			// background the first time the mesh is loaded, and a component registered before that has no body:
			// the first test to load Borlash in a session stood a character in a city nothing could touch, and
			// the second, in the same build, passed. The game's own city is long built by the time anybody
			// arrives, so this is the test catching up with it, not a fix to the city.
			TInlineComponentArray<UStaticMeshComponent*> Meshes(Settlement);
			int32 Solid = 0;
			int32 WithBodies = 0;

			for (UStaticMeshComponent* Mesh : Meshes)
			{
				UBodySetup* const Setup = Mesh->GetStaticMesh() != nullptr ? Mesh->GetStaticMesh()->GetBodySetup() : nullptr;

				if (Setup == nullptr || Setup->AggGeom.ConvexElems.Num() == 0)
				{
					continue;
				}

				++Solid;

				if (Mesh->GetBodyInstance() == nullptr || !Mesh->GetBodyInstance()->IsValidBodyInstance())
				{
					Setup->CreatePhysicsMeshes();
					Mesh->RecreatePhysicsState();
				}

				WithBodies += Mesh->GetBodyInstance() != nullptr && Mesh->GetBodyInstance()->IsValidBodyInstance() ? 1 : 0;
			}

			// And the world once, as the game's would, so the physics scene has taken them in.
			World->Tick(LEVELTICK_All, 1.0f / 60.0f);

			// Solid to queries, asked of the built thing: a line straight down through the square stops on
			// its paving. A test of standing on a floor proves nothing in a world that cannot see the floor.
			const FVector Up = Station.Direction.GetSafeNormal();
			const FVector Square = Origin->ToWorldLocation(Placed->GetSystemPosition()) + Up * 5000.0;
			FHitResult Line;

			return Test.TestEqual(TEXT("Every solid part of the city has a body"), WithBodies, Solid)
				&& Test.TestTrue(TEXT("A line straight down onto the square stops on the city"),
					World->LineTraceSingleByChannel(Line, Square + Up * 1000.0, Square - Up * 7000.0, ECC_Pawn)
					&& Line.GetActor() == Settlement)
				&& Zone != nullptr;

		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOBorlashStandsAsBuiltTest,
	"SpaceMMO.Settlement.BorlashStandsAsBuilt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOBorlashStandsAsBuiltTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMOSettlementTests;

	FBorlashWorld City;

	if (!City.Build(*this))
	{
		return false;
	}

	const TSharedPtr<FJsonObject>& Manifest = City.Manifest;
	const FBackendStation& Station = City.Station;
	const USpaceMMORenderOriginSubsystem* const Origin = City.Origin;
	ASpaceMMOStationActor* const Placed = City.Placed;
	const ASpaceMMOSettlementActor* const Settlement = City.Settlement;
	const FSpaceMMOAirspaceZone* const Zone = City.Zone;

	TestEqual(TEXT("It is Borlash's Blueprint"), Settlement->GetClass()->GetName(),
		FString(TEXT("BP_Station_Borlash_C")));

	// Every mesh with hulls is solid to what moves. Ships and characters sweep the pawn channel against
	// simple collision only, so a mesh that ignored it would be walked and flown straight through.
	TInlineComponentArray<UStaticMeshComponent*> Meshes(Settlement);
	int32 Solid = 0;

	for (const UStaticMeshComponent* Mesh : Meshes)
	{
		const UBodySetup* const Body =
			Mesh->GetStaticMesh() != nullptr ? Mesh->GetStaticMesh()->GetBodySetup() : nullptr;

		if (Body == nullptr || Body->AggGeom.ConvexElems.Num() == 0)
		{
			continue;
		}

		++Solid;

		TestEqual(*FString::Printf(TEXT("%s blocks ships and people"), *Mesh->GetName()),
			Mesh->GetCollisionResponseToChannel(ECC_Pawn), ECR_Block);
	}

	TestTrue(TEXT("Borlash has solid parts at all"), Solid > 0);

	const TSharedPtr<FJsonObject> Sky = Manifest->GetObjectField(TEXT("airspace"));

	TestEqual(TEXT("The airspace is named for the messages"), Zone->Name, FString(TEXT("Borlash")));
	TestEqual(TEXT("No-fly radius as built"),
		Zone->NoFlyRadiusKilometres * 1000.0, Sky->GetNumberField(TEXT("no_fly_radius_m")), 1e-6);
	TestEqual(TEXT("No-fly ceiling as built"),
		Zone->NoFlyCeilingKilometres * 1000.0, Sky->GetNumberField(TEXT("no_fly_ceiling_m")), 1e-6);
	TestEqual(TEXT("Platform as built"), Zone->PlatformHalfWidthKilometres * 1000.0,
		Sky->GetObjectField(TEXT("no_disembark"))->GetNumberField(TEXT("square_half_m")), 1e-6);
	TestEqual(TEXT("Floor as built"),
		Zone->FloorKilometres * 1000.0, Manifest->GetNumberField(TEXT("floor_m")), 1e-6);

	const TArray<TSharedPtr<FJsonValue>>& Rows = Manifest->GetArrayField(TEXT("berths"));
	TestEqual(TEXT("Every dock is a berth"), Zone->Berths.Num(), Rows.Num());

	TArray<const USpaceMMOBerthComponent*> Berths;
	Settlement->GetBerths(Berths);

	const FVector Up = Station.Direction.GetSafeNormal();
	const FSystemCoordinate Centre = Placed->GetSystemPosition();
	constexpr double ShipHull = 0.002;

	for (const TSharedPtr<FJsonValue>& Value : Rows)
	{
		const TSharedPtr<FJsonObject> Row = Value->AsObject();
		const FString Key = Row->GetStringField(TEXT("key"));

		const USpaceMMOBerthComponent* const* Found = Berths.FindByPredicate(
			[&Key](const USpaceMMOBerthComponent* Berth) { return Berth->BerthKey == Key; });

		if (!TestNotNull(*FString::Printf(TEXT("%s is a berth"), *Key), Found))
		{
			continue;
		}

		// What the flight readout calls it after the city's name (task 169).
		TestEqual(*FString::Printf(TEXT("%s carries its short name"), *Key),
			(*Found)->ShortName, Row->GetStringField(TEXT("short_name")));

		const FSystemCoordinate Pad =
			FSystemCoordinate::FromLocalCentimetres((*Found)->GetComponentLocation(), Origin->GetRenderOrigin());

		// Where the airspace thinks the pad is, against where the city's build script put it: Blender's
		// north is Unreal's -Y (the manifest's "units"), and heights are above the walking floor.
		const FVector Built = ReadVector(Row, TEXT("pad_m"));
		const FVector Expected(Built.X / 1000.0, -Built.Y / 1000.0, Built.Z / 1000.0 - Zone->FloorKilometres);
		const FVector Seen = FSpaceMMOAirspace::ToLocal(*Zone, Pad);

		TestTrue(*FString::Printf(TEXT("%s is where it was built, in the airspace's frame: %s against %s"),
				*Key, *Seen.ToCompactString(), *Expected.ToCompactString()),
			Seen.Equals(Expected, 0.00001));

		const double PadRadius = Row->GetNumberField(TEXT("pad_radius_m")) / 1000.0;
		const FSystemCoordinate Resting(Pad.Kilometres + Up * ShipHull);

		// What a pilot at this dock meets, in the order they meet it.
		TestFalse(*FString::Printf(TEXT("%s can be flown to"), *Key),
			FSpaceMMOAirspace::IsInNoFly(*Zone, Resting, ShipHull));
		TestTrue(*FString::Printf(TEXT("A ship on %s is at a berth"), *Key), Placed->IsAtBerth(Resting));
		TestFalse(*FString::Printf(TEXT("A ship on %s docks rather than being left there"), *Key),
			FSpaceMMOAirspace::AllowsStepOut(*Zone, Resting));

		FSystemCoordinate Ashore;

		if (TestTrue(*FString::Printf(TEXT("%s has somewhere to set a pilot down"), *Key),
				Placed->PilotArrivalNear(Resting, 0.05, Ashore)))
		{
			const double FromPad = Across(Ashore, Pad, Up);

			TestTrue(*FString::Printf(TEXT("The pilot is set down beside %s, %.1f m from its middle"), *Key,
					FromPad * 1000.0),
				FromPad > PadRadius && FromPad < PadRadius + 0.003);

			// On the city side of it. The other side is the platform's corner, and past that, a drop.
			TestTrue(*FString::Printf(TEXT("The pilot is set down on the city side of %s"), *Key),
				Across(Ashore, Centre, Up) < Across(Pad, Centre, Up));

			// The range on foot is what keeps them docked: short of the dock and they are undocked the
			// moment they arrive, with the hangar out of reach.
			TestTrue(*FString::Printf(TEXT("A pilot set down at %s, %.0f m out, is still docked"), *Key,
					Across(Ashore, Centre, Up) * 1000.0),
				ASpaceMMOStationActor::IsWithinDockingRange(Station, Centre, Ashore));
		}

		FSystemCoordinate Parking;

		if (TestTrue(*FString::Printf(TEXT("A ship can be brought to %s"), *Key),
				Placed->ShipPlacementNear(Resting, 0.05, ShipHull, Parking)))
		{
			TestTrue(*FString::Printf(TEXT("A ship summoned at %s comes to its pad"), *Key),
				Across(Parking, Pad, Up) < 0.0001);
		}
	}

	// And the city, which is what the berths exist to keep ships out of.
	const FSystemCoordinate OverSquare(Centre.Kilometres + Up * 0.05);

	TestTrue(TEXT("The air over the square is closed"), FSpaceMMOAirspace::IsInNoFly(*Zone, OverSquare, ShipHull));
	TestFalse(TEXT("The square is not a berth"), Placed->IsAtBerth(OverSquare));

	// The readout's promise, kept (task 169): wherever a ship's line says READY, G docks. Sampled round
	// every pad and up to eighty metres over it, with the line built exactly as the flight readout
	// builds it, against the rule the server docks by.
	TArray<FSpaceMMODockMark> Docks;
	Placed->GetDockMarks(Docks);

	TestEqual(TEXT("Every berth is a dock on the readout"), Docks.Num(), Berths.Num());

	int32 Ready = 0;
	int32 Broken = 0;
	const FVector East = FVector::VectorPlaneProject(FVector::ForwardVector, Up).GetSafeNormal();
	const FVector North = FVector::CrossProduct(Up, East);

	for (const FSpaceMMODockMark& Dock : Docks)
	{
		for (double X = -0.040; X <= 0.0401; X += 0.005)
		{
			for (double Y = -0.040; Y <= 0.0401; Y += 0.005)
			{
				for (const double Height : {0.002, 0.010, 0.019, 0.030, 0.060, 0.080})
				{
					const FSystemCoordinate Ship(Dock.Pad.Kilometres + East * X + North * Y + Up * Height);

					FSpaceMMOStationMarkerView View;
					View.Name = Station.Name;
					View.DistanceKilometres = (Ship.Kilometres - Centre.Kilometres).Size();
					View.DockingRangeKilometres = Station.DockingRangeKilometres;

					double DockDistance = 0.0;
					const int32 Nearest = FSpaceMMOStationLine::NearestDock(Docks, Ship, DockDistance);
					View.DockName = Docks[Nearest].ShortName;
					View.DockDistanceKilometres = DockDistance;
					View.DockReachKilometres = Docks[Nearest].ReachKilometres;

					FString Name;
					double Shown = 0.0;
					double Range = 0.0;
					FSpaceMMOStationLine::ForShip(View, Name, Shown, Range);

					if (!FSpaceMMOStationLine::Format(Name, Shown, Range, true, TEXT("The Capital")).EndsWith(TEXT("READY")))
					{
						continue;
					}

					++Ready;

					if (!Placed->IsAtBerth(Ship))
					{
						++Broken;
					}
				}
			}
		}
	}

	TestTrue(TEXT("The readout says READY somewhere round the pads"), Ready > 0);
	TestEqual(TEXT("Nowhere it says READY does docking refuse"), Broken, 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOPilotPutUnderTheCityTest,
	"SpaceMMO.Settlement.APilotPutUnderTheCityStandsOnIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOPilotPutUnderTheCityTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMOSettlementTests;

	// Task 175. Ayla logged out before Borlash existed, and the server put her back where she had been
	// -- which was now under the city's paving. The terrain caught her inside the floor, and she stood
	// there, half under the city, until a jump.
	FBorlashWorld City;

	if (!City.Build(*this))
	{
		return false;
	}

	UWorld* const World = City.Scoped.World;

	auto PutBack = [&](const FSystemCoordinate& Where) -> ASpaceMMOCharacterPawn*
	{
		// Spawned where a connection is spawned, then put back where the record says, as
		// ASpaceMMOPlayerController::RestoreWhereabouts does.
		ASpaceMMOCharacterPawn* const Character = World->SpawnActorDeferred<ASpaceMMOCharacterPawn>(
			ASpaceMMOCharacterPawn::StaticClass(), FTransform::Identity, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

		if (Character == nullptr)
		{
			return nullptr;
		}

		Character->SetStartingSystemPosition(FVector(60.0, 0.0, 20.55));
		Character->FinishSpawning(FTransform::Identity);
		Character->ResumeAt(Where);

		// A few frames, standing still: the first meets the ground, the next whatever is on it.
		for (int32 Frame = 0; Frame < 4; ++Frame)
		{
			Character->Tick(1.0f / 60.0f);
		}

		return Character;
	};

	// Exactly where the server put her: "Put back at (60.491, -0.004, 20.158) km" (2 October), 33 m
	// from the city's centre and 8 m under its paving.
	const ASpaceMMOCharacterPawn* const Ayla = PutBack(FSystemCoordinate(FVector(60.491, -0.004, 20.158)));

	if (!TestNotNull(TEXT("Ayla spawned"), Ayla))
	{
		return false;
	}

	// What the world's collision says is at her spot, asked directly: a line straight down through the
	// paving, and the capsule the footing uses, where she stands. Printed, because a test of standing on a
	// floor proves nothing in a world whose queries cannot see the floor.
	{
		const FVector Up = City.Station.Direction.GetSafeNormal();
		const FVector At = City.Origin->ToWorldLocation(Ayla->GetSystemPosition());
		FHitResult Line;
		const bool bLine = World->LineTraceSingleByChannel(Line, At + Up * 1000.0, At - Up * 1000.0, ECC_Pawn);

		FHitResult Capsule;
		const bool bCapsule = World->SweepSingleByChannel(Capsule, At + Up * 100.0, At + Up * 30.0,
			FRotationMatrix::MakeFromZ(Up).ToQuat(), ECC_Pawn, FCollisionShape::MakeCapsule(34.0f, 90.0f));

		AddInfo(FString::Printf(
			TEXT("At her feet: line %d on %s, %.1f cm above the feet; capsule %d on %s, starting inside %d, depth %.1f cm, way out %s."),
			bLine ? 1 : 0, *GetNameSafe(Line.GetActor()), FVector::DotProduct(Line.ImpactPoint - At, Up),
			bCapsule ? 1 : 0, *GetNameSafe(Capsule.GetActor()), Capsule.bStartPenetrating ? 1 : 0,
			Capsule.PenetrationDepth, *Capsule.Normal.ToCompactString()));
	}

	const FVector Feet = FSpaceMMOAirspace::ToLocal(*City.Zone, Ayla->GetSystemPosition());

	TestTrue(*FString::Printf(TEXT("Put back under the square, she stands on its paving: feet %.3f m from the floor"),
			Feet.Z * 1000.0),
		FMath::Abs(Feet.Z) < 0.00002);
	TestTrue(TEXT("And she is standing, not falling"), Ayla->IsOnGround());

	// Beyond the platform the terrain is the floor, and nothing may lift anybody off it: 320 m east of
	// the centre the levelled ground is 2.5 m below the city's plane, and that is where she belongs.
	const FSystemCoordinate OpenGround = FSystemCoordinate(
		City.Zone->Origin.Kilometres + City.Zone->Rotation.RotateVector(FVector(0.32, 0.0, -0.008)));

	const ASpaceMMOCharacterPawn* const Outside = PutBack(OpenGround);

	if (TestNotNull(TEXT("A second character spawned"), Outside))
	{
		const FVector Out = FSpaceMMOAirspace::ToLocal(*City.Zone, Outside->GetSystemPosition());
		const double Ground = FPlanetTerrain::SurfaceRadiusKilometres(City.Planet, City.Terrain,
			(Outside->GetSystemPosition().Kilometres - City.Planet.Centre.Kilometres).GetSafeNormal());
		const double Standing = (Outside->GetSystemPosition().Kilometres - City.Planet.Centre.Kilometres).Size();

		TestTrue(*FString::Printf(TEXT("On open ground she stands on the terrain, %.2f m below the city's floor"),
				-Out.Z * 1000.0),
			Out.Z < -0.002 && FMath::Abs(Standing - Ground) < 0.0005);
	}

	return true;
}

#endif
