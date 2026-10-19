#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "SpaceMMOCharacterPawn.h"
#include "SpaceMMOFlightModel.h"
#include "SpaceMMOPlanetActor.h"
#include "SpaceMMOPlanetTerrain.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * Task 200: somewhere the server puts a character on purpose is jumped to, not slid to.
 *
 * Joe, 18 October: after dying and respawning, the character slid across the planet to the station. The owning
 * client blends toward the server's position unless the two are a kilometre apart -- a threshold inherited from
 * ships -- and a station is usually nearer than that.
 */
namespace SpaceMMOPlacementTests
{
	/** The home start tests' world: begun by hand, torn down however the test leaves. */
	struct FScopedWorld
	{
		UWorld* World = nullptr;

		FScopedWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("SpaceMMOPlacementTest"));

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
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOPlacedOnPurposeIsJumpedToTest,
	"SpaceMMO.Placement.SomewherePutOnPurposeIsJumpedTo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOPlacedOnPurposeIsJumpedToTest::RunTest(const FString& Parameters)
{
	// The respawn in Joe's playtest: a few hundred metres, well inside the kilometre that would have snapped.
	const FSystemCoordinate DiedAt(FVector(60.0, 0.0, 20.5));
	const FSystemCoordinate Station(FVector(60.31, 0.12, 20.47));
	const FShipReconciliation Rules;

	// One frame at 60 a second, as the owning client takes it.
	const double Frame = 1.0 / 60.0;

	const FSystemCoordinate Jumped = ASpaceMMOCharacterPawn::FollowOwnPosition(DiedAt, Station, Rules, Frame, true);

	TestTrue(TEXT("Put there on purpose: there at once"), (Jumped.Kilometres - Station.Kilometres).Size() < 1e-12);

	// The blend it replaces, kept honest: a frame in, a slide has covered under a tenth of the way.
	const FSystemCoordinate Slid = ASpaceMMOCharacterPawn::FollowOwnPosition(DiedAt, Station, Rules, Frame, false);
	const double Left = (Slid.Kilometres - Station.Kilometres).Size() / (DiedAt.Kilometres - Station.Kilometres).Size();

	TestTrue(FString::Printf(TEXT("Disagreement still blends (%.0f%% of the way left)"), Left * 100.0), Left > 0.9);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOServerPlacementsAreMarkedTest,
	"SpaceMMO.Placement.EveryPlacementTheServerMakesIsMarked",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOServerPlacementsAreMarkedTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMOPlacementTests;

	FScopedWorld Scoped;

	ASpaceMMOCharacterPawn* const Walker = Spawn(*Scoped.World);
	const ASpaceMMOPlanetActor* Planet = nullptr;

	for (TActorIterator<ASpaceMMOPlanetActor> It(Scoped.World); It; ++It)
	{
		Planet = *It;
	}

	if (!TestNotNull(TEXT("A character"), Walker) || !TestNotNull(TEXT("The world has its planet"), Planet))
	{
		return false;
	}

	const uint8 Before = Walker->GetPlacementEpoch();

	// A respawn and a restore both come through here.
	Walker->ResumeAt(FSystemCoordinate(Planet->GetPlanetConfig().Centre.Kilometres + FVector(0.0, 0.0, 20.5)));

	TestEqual(TEXT("Resumed: marked"), static_cast<int32>(Walker->GetPlacementEpoch()), Before + 1);

	// A new character's home start.
	Walker->StartAt(*Planet, FVector(0.31, -0.27, 0.91).GetSafeNormal(), nullptr);

	TestEqual(TEXT("Started at home: marked"), static_cast<int32>(Walker->GetPlacementEpoch()), Before + 2);

	return true;
}

#endif
