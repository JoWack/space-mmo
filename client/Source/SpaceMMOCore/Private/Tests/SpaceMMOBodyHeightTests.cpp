#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/SpringArmComponent.h"
#include "Misc/AutomationTest.h"
#include "SpaceMMOCharacterPawn.h"
#include "SpaceMMOPlanetActor.h"
#include "SpaceMMOPlanetTerrain.h"
#include "SpaceMMORenderOrigin.h"
#include "SpaceMMOWalkModel.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * Task 177: a race is as tall as its body, to the server's sweeps and to its own cameras.
 *
 * Joe, 5 October: the capsule's height follows the body, its radius stays 34 cm, and both cameras scale
 * with the body's height from the human's 160 and 165 cm. The heights are read from the config the game
 * reads, every race in it, rather than written here, so authoring a new body tests it too.
 */
namespace SpaceMMOBodyHeightTests
{
	/** A game world for one test, torn down however the test leaves: the settlement tests' arrangement. */
	struct FScopedWorld
	{
		UWorld* World = nullptr;

		FScopedWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("SpaceMMOBodyHeightTest"));

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

	/**
	 * A character as the server holds one: spawned, begun by hand -- a world with no game mode begins play
	 * for nobody -- and given its race the way the owning client's word arrives.
	 */
	ASpaceMMOCharacterPawn* Spawn(UWorld& World, const int32 Race)
	{
		ASpaceMMOCharacterPawn* const Pawn = World.SpawnActorDeferred<ASpaceMMOCharacterPawn>(
			ASpaceMMOCharacterPawn::StaticClass(), FTransform::Identity, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

		if (Pawn != nullptr)
		{
			Pawn->FinishSpawning(FTransform::Identity);
			Pawn->DispatchBeginPlay();
			Pawn->SetBodyRace(Race);
		}

		return Pawn;
	}

	/** A client's input reaching the server: the RPC itself, run as the net driver runs it. */
	void Send(ASpaceMMOCharacterPawn& Walker, const FWalkInput& Input)
	{
		FWalkInput Parameters = Input;
		Walker.ProcessEvent(Walker.FindFunctionChecked(TEXT("ServerSendWalkInput")), &Parameters);
	}

	const UCameraComponent* FirstPersonCamera(const ASpaceMMOCharacterPawn& Pawn)
	{
		TInlineComponentArray<UCameraComponent*> Cameras(&Pawn);

		for (const UCameraComponent* Camera : Cameras)
		{
			if (Camera->GetFName() == TEXT("FirstPersonCamera"))
			{
				return Camera;
			}
		}

		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOEachRaceSeesFromItsOwnEyesTest,
	"SpaceMMO.Character.EachRaceSeesFromItsOwnEyes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOEachRaceSeesFromItsOwnEyesTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMOBodyHeightTests;

	// The human's framing is the reference, and stays exactly what it was.
	const FVector2D Human = ASpaceMMOCharacterPawn::ViewHeightsFor(180.0, 180.0);
	TestEqual(TEXT("A 180 cm body sees from 160 cm in third person"), Human.X, 160.0, 1e-9);
	TestEqual(TEXT("And from 165 cm in first person"), Human.Y, 165.0, 1e-9);

	const FVector2D Unusable = ASpaceMMOCharacterPawn::ViewHeightsFor(0.0, 180.0);
	TestEqual(TEXT("An unusable height leaves the framing as it was"), Unusable.X, 160.0, 1e-9);

	FScopedWorld Scoped;

	const TArray<FSpaceMMOCharacterBody>& Bodies = GetDefault<ASpaceMMOCharacterPawn>()->GetRaceBodies();

	if (!TestTrue(TEXT("DefaultGame.ini names race bodies"), Bodies.Num() > 0))
	{
		return false;
	}

	for (const FSpaceMMOCharacterBody& Body : Bodies)
	{
		ASpaceMMOCharacterPawn* const Pawn = Spawn(*Scoped.World, Body.Race);

		if (!TestNotNull(TEXT("A character spawned"), Pawn))
		{
			return false;
		}

		const USpringArmComponent* const Boom = Pawn->FindComponentByClass<USpringArmComponent>();
		const UCameraComponent* const Eyes = FirstPersonCamera(*Pawn);

		if (!TestNotNull(TEXT("It has a camera boom"), Boom) || !TestNotNull(TEXT("And first-person eyes"), Eyes))
		{
			return false;
		}

		const double Scale = Body.HeightCentimetres / 180.0;

		AddInfo(FString::Printf(TEXT("Race %d, %.0f cm: collides as %.1f cm, sees from %.1f cm, %.1f in first person."),
			Body.Race, Body.HeightCentimetres, Pawn->GetBodyHeightCentimetres(),
			Boom->GetRelativeLocation().Z, Eyes->GetRelativeLocation().Z));

		TestEqual(*FString::Printf(TEXT("Race %d collides as tall as its body"), Body.Race),
			Pawn->GetBodyHeightCentimetres(), Body.HeightCentimetres, 1e-6);
		TestEqual(*FString::Printf(TEXT("Race %d's third-person pivot is at its own height"), Body.Race),
			Boom->GetRelativeLocation().Z, 160.0 * Scale, 0.01);
		TestEqual(*FString::Printf(TEXT("Race %d's first-person eyes are at its own height"), Body.Race),
			Eyes->GetRelativeLocation().Z, 165.0 * Scale, 0.01);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOATallRaceStopsAtALowBeamTest,
	"SpaceMMO.Character.ATallRaceStopsAtALowBeamAHumanWalksUnder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOATallRaceStopsAtALowBeamTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMOBodyHeightTests;

	// What the taller capsule is for, measured as behaviour: the sweeps the server runs. Each race walks at
	// a beam whose underside is 2 m above the ground under it. A body under 2 m walks beneath; a taller one
	// stops against it, as its head would.
	constexpr double BeamUndersideCentimetres = 200.0;
	constexpr double BeamAheadCentimetres = 150.0;

	FScopedWorld Scoped;
	UWorld* const World = Scoped.World;

	const ASpaceMMOPlanetActor* Planet = nullptr;

	for (TActorIterator<ASpaceMMOPlanetActor> It(World); It; ++It)
	{
		Planet = *It;
	}

	USpaceMMORenderOriginSubsystem* const Origin = World->GetSubsystem<USpaceMMORenderOriginSubsystem>();
	UStaticMesh* const Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));

	if (!TestNotNull(TEXT("The world has its planet"), Planet) || !TestNotNull(TEXT("A render origin"), Origin)
		|| !TestNotNull(TEXT("The engine's cube"), Cube))
	{
		return false;
	}

	const FPlanetConfig& Config = Planet->GetPlanetConfig();
	const FPlanetTerrainConfig& Terrain = Planet->GetTerrainConfig();
	const TArray<FSpaceMMOCharacterBody>& Bodies = GetDefault<ASpaceMMOCharacterPawn>()->GetRaceBodies();

	bool bSomeonePassed = false;
	bool bSomeoneStopped = false;

	for (int32 Index = 0; Index < Bodies.Num(); ++Index)
	{
		const FSpaceMMOCharacterBody& Body = Bodies[Index];

		// Each on its own patch of ground, a few hundred metres apart, so no beam is in another's way.
		const FVector Direction = FVector(0.0, 1.0, 0.02 * Index).GetSafeNormal();
		const FSystemCoordinate Start = FPlanetTerrain::SurfacePosition(Config, Terrain, Direction);

		Origin->SetRenderOrigin(Start);

		ASpaceMMOCharacterPawn* const Walker = Spawn(*World, Body.Race);

		if (!TestNotNull(TEXT("A character spawned"), Walker))
		{
			return false;
		}

		Walker->ResumeAt(Start);

		FWalkInput Input;
		Send(*Walker, Input);

		for (int32 Frame = 0; Frame < 10; ++Frame)
		{
			Walker->Tick(1.0f / 60.0f);
		}

		const FVector Up = Walker->GetSurfaceNormal().GetSafeNormal();
		const FVector Forward = FVector::VectorPlaneProject(Walker->GetActorForwardVector(), Up).GetSafeNormal();
		const FVector From = Origin->ToWorldLocation(Walker->GetSystemPosition());

		// The ground under the beam, from the height field, so a slope cannot lift the beam or sink it.
		const FSystemCoordinate BeamFootSystem = FSystemCoordinate(
			Walker->GetSystemPosition().Kilometres + Forward * (BeamAheadCentimetres / 100000.0));
		const FSystemCoordinate BeamGround = FPlanetTerrain::SurfacePosition(
			Config, Terrain, (BeamFootSystem.Kilometres - Config.Centre.Kilometres).GetSafeNormal());

		// 30 cm deep, 4 m across the path, 60 cm tall; the engine's cube is a metre a side.
		const FVector BeamCentre = Origin->ToWorldLocation(BeamGround) + Up * (BeamUndersideCentimetres + 30.0);
		const FTransform BeamTransform(
			FRotationMatrix::MakeFromZX(Up, Forward).ToQuat(), BeamCentre, FVector(0.3, 4.0, 0.6));

		AStaticMeshActor* const Beam = World->SpawnActorDeferred<AStaticMeshActor>(
			AStaticMeshActor::StaticClass(), BeamTransform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

		if (!TestNotNull(TEXT("The beam spawned"), Beam))
		{
			return false;
		}

		Beam->GetStaticMeshComponent()->SetStaticMesh(Cube);
		Beam->FinishSpawning(BeamTransform);

		Input.Move = FVector2D(1.0, 0.0);

		for (int32 Frame = 0; Frame < 90; ++Frame)
		{
			Send(*Walker, Input);
			Walker->Tick(1.0f / 60.0f);
		}

		const double Travelled =
			FVector::DotProduct(Origin->ToWorldLocation(Walker->GetSystemPosition()) - From, Forward);
		const bool bPassed = Travelled > BeamAheadCentimetres + 100.0;
		const bool bShouldPass = Body.HeightCentimetres < BeamUndersideCentimetres;

		AddInfo(FString::Printf(TEXT("Race %d, %.0f cm: walked %.0f cm toward a beam %.0f cm ahead with its underside %.0f cm up."),
			Body.Race, Body.HeightCentimetres, Travelled, BeamAheadCentimetres, BeamUndersideCentimetres));

		TestEqual(*FString::Printf(TEXT("Race %d, %.0f cm tall, %s the beam"), Body.Race, Body.HeightCentimetres,
				bShouldPass ? TEXT("walks under") : TEXT("stops at")),
			bPassed, bShouldPass);

		// Stopped at it, not before it: a wall of nothing would also stop someone.
		if (!bShouldPass)
		{
			TestTrue(*FString::Printf(TEXT("Race %d stops within half a metre of the beam (%.0f cm short)"), Body.Race,
					BeamAheadCentimetres - Travelled),
				Travelled > BeamAheadCentimetres - 15.0 - 34.0 - 50.0);
		}

		bSomeonePassed |= bPassed;
		bSomeoneStopped |= !bPassed;

		Beam->Destroy();
		Walker->Destroy();
	}

	// Both outcomes, or the arrangement is not testing the capsule at all.
	TestTrue(TEXT("Some race walks under the beam"), bSomeonePassed);
	TestTrue(TEXT("Some race stops at it"), bSomeoneStopped);

	return true;
}

#endif
