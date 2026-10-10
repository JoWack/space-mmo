#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "SpaceMMOCharacterPawn.h"
#include "SpaceMMOPlanetActor.h"
#include "SpaceMMOPlanetTerrain.h"
#include "SpaceMMOWalkModel.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * Task 145: a new character is stood at their home world's start, facing its outpost.
 *
 * The placement is the height function's, against whatever terrain the planet wears; the facing is laid
 * along the ground; and on a dedicated server the facing has to survive the owning client, which until
 * this task sent back its own facing every frame and so undid any turn the server made after the pawn
 * began.
 */
namespace SpaceMMOHomeStartTests
{
	/** A game world for one test, torn down however the test leaves: the body height tests' arrangement. */
	struct FScopedWorld
	{
		UWorld* World = nullptr;

		FScopedWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("SpaceMMOHomeStartTest"));

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

	/** Spawned and begun by hand: a world with no game mode begins play for nobody. */
	ASpaceMMOCharacterPawn* Spawn(UWorld& World)
	{
		ASpaceMMOCharacterPawn* const Pawn = World.SpawnActorDeferred<ASpaceMMOCharacterPawn>(
			ASpaceMMOCharacterPawn::StaticClass(), FTransform::Identity, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

		if (Pawn != nullptr)
		{
			Pawn->FinishSpawning(FTransform::Identity);
			Pawn->DispatchBeginPlay();
		}

		return Pawn;
	}

	const ASpaceMMOPlanetActor* ThePlanet(UWorld& World)
	{
		for (TActorIterator<ASpaceMMOPlanetActor> It(&World); It; ++It)
		{
			return *It;
		}

		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOHomeStartStandsOnTheGroundFacingTheOutpostTest,
	"SpaceMMO.HomeStart.StandsOnTheGroundFacingTheOutpost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOHomeStartStandsOnTheGroundFacingTheOutpostTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMOHomeStartTests;

	FScopedWorld Scoped;

	const ASpaceMMOPlanetActor* const Planet = ThePlanet(*Scoped.World);
	ASpaceMMOCharacterPawn* const Walker = Spawn(*Scoped.World);

	if (!TestNotNull(TEXT("The world has its planet"), Planet) || !TestNotNull(TEXT("A character"), Walker))
	{
		return false;
	}

	const FPlanetConfig Config = Planet->GetPlanetConfig();
	const FPlanetTerrainConfig Terrain = Planet->GetTerrainConfig();

	// Off every axis, so a start that dropped a component, or faced along world X, cannot pass.
	const FVector Start = FVector(0.31, -0.27, 0.91).GetSafeNormal();

	// An outpost 35 m away, as the authored starts are, in a direction with no axis to it either.
	const FVector Aside = FVector::CrossProduct(Start, FVector(0.2, 0.7, -0.1)).GetSafeNormal();
	const FVector OutpostDirection =
		(Start + Aside * (0.035 / Config.RadiusKilometres)).GetSafeNormal();
	const FVector Outpost = FPlanetTerrain::SurfacePosition(Config, Terrain, OutpostDirection).Kilometres;

	TestTrue(TEXT("It starts"), Walker->StartAt(*Planet, Start, &Outpost));

	const FVector Position = Walker->GetSystemPosition().Kilometres;
	const FVector Up = (Position - Config.Centre.Kilometres).GetSafeNormal();
	const FVector Ground = FPlanetTerrain::SurfacePosition(Config, Terrain, Start).Kilometres;

	// On the start, to the width of a hair, and on the ground there: within the standing height of
	// the height function's answer, not fifty metres above it as the spawn's drop would have been.
	TestTrue(TEXT("Stood over the start"), FVector::DotProduct(Up, Start) > 1.0 - 1e-9);
	TestTrue(
		FString::Printf(TEXT("Stood on the ground (%.2f m from it)"), (Position - Ground).Size() * 1000.0),
		(Position - Ground).Size() < 0.003);

	// Facing the outpost along the ground.
	const FVector Forward = FVector::VectorPlaneProject(Walker->GetActorForwardVector(), Up).GetSafeNormal();
	const FVector Toward = FVector::VectorPlaneProject(Outpost - Position, Up).GetSafeNormal();

	TestTrue(
		FString::Printf(TEXT("Facing the outpost (cosine %.4f)"), FVector::DotProduct(Forward, Toward)),
		FVector::DotProduct(Forward, Toward) > 0.99);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOHomeStartRefusesNoDirectionTest,
	"SpaceMMO.HomeStart.RefusesNoDirection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOHomeStartRefusesNoDirectionTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMOHomeStartTests;

	FScopedWorld Scoped;

	const ASpaceMMOPlanetActor* const Planet = ThePlanet(*Scoped.World);
	ASpaceMMOCharacterPawn* const Walker = Spawn(*Scoped.World);

	if (!TestNotNull(TEXT("The world has its planet"), Planet) || !TestNotNull(TEXT("A character"), Walker))
	{
		return false;
	}

	const FVector Before = Walker->GetSystemPosition().Kilometres;

	// A zero direction normalises to nothing; placing on it would stand the character at the core.
	TestFalse(TEXT("A zero direction is refused"), Walker->StartAt(*Planet, FVector::ZeroVector, nullptr));
	TestTrue(TEXT("...and nothing moved"), Walker->GetSystemPosition().Kilometres.Equals(Before));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOHomeStartFacingTowardTest,
	"SpaceMMO.HomeStart.FacingToward",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOHomeStartFacingTowardTest::RunTest(const FString& Parameters)
{
	const FVector Up = FVector(0.2, -0.4, 0.89).GetSafeNormal();
	const FVector Toward = FVector(1.0, 0.5, 0.3);
	const FQuat Fallback = FQuat(FVector::UpVector, 0.7);

	const FQuat Facing = FCharacterWalkModel::FacingToward(Up, Toward, Fallback);

	TestTrue(TEXT("Stands on Up"), Facing.GetUpVector().Equals(Up, 1e-6));
	TestTrue(
		TEXT("Looks along the ground toward the target"),
		Facing.GetForwardVector().Equals(FVector::VectorPlaneProject(Toward, Up).GetSafeNormal(), 1e-6));

	// Straight overhead names no way to face along the ground.
	TestTrue(
		TEXT("A target overhead keeps the old facing"),
		FCharacterWalkModel::FacingToward(Up, Up * 5.0, Fallback).Equals(Fallback));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOHomeStartServerFacingSurvivesTheClientTest,
	"SpaceMMO.HomeStart.ServerFacingSurvivesTheClient",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOHomeStartServerFacingSurvivesTheClientTest::RunTest(const FString& Parameters)
{
	// The server has turned the character (epoch 1). The client's next input was sent before it saw
	// that, turned from epoch 0: taking it would put the character back the way it faced and publish
	// that, and the client would then take its own old facing as the server's.
	FWalkState Server;
	const FQuat Turned = FQuat(FVector::UpVector, 1.2);
	Server.Rotation = Turned;

	FWalkState ClientState;
	ClientState.Rotation = FQuat(FVector::UpVector, -0.4);

	FWalkInput Stale = FCharacterWalkModel::ForServer(FWalkInput(), ClientState, true, 0).Sanitised();
	FCharacterWalkModel::AdoptClientFacing(Server, Stale, 1);

	TestTrue(TEXT("A facing from before the turn is ignored"), Server.Rotation.Equals(Turned));

	// Once the client has taken the turn and sends epoch 1, its facing is the player's again.
	ClientState.Rotation = FQuat(FVector::UpVector, 1.5);
	FWalkInput Current = FCharacterWalkModel::ForServer(FWalkInput(), ClientState, true, 1).Sanitised();
	FCharacterWalkModel::AdoptClientFacing(Server, Current, 1);

	TestTrue(TEXT("A facing turned from the server's is taken"), Server.Rotation.Equals(ClientState.Rotation));

	return true;
}

#endif
