#if WITH_EDITOR
#include "Animation/AnimData/IAnimationDataModel.h"
#endif
#include "Animation/AnimSequence.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "SpaceMMOCharacterPawn.h"
#include "SpaceMMOPlanetActor.h"
#include "SpaceMMOPlanetTerrain.h"
#include "SpaceMMOWalkModel.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * Task 195: stamina, sprint spending it, and the dodge (ADR-0017, amended 19 October).
 *
 * The rules are the walk model's, which the server steps and the owning client predicts, so they are tested where
 * they live: on flat ground, a frame at a time, the way both sides run them.
 */
namespace SpaceMMOStaminaTests
{
	constexpr double Frame = 1.0 / 60.0;

	/** A character on flat ground, and how far they have gone, in centimetres. */
	struct FWalker
	{
		FWalkState State;
		FWalkConfig Config;
		FVector Travelled = FVector::ZeroVector;

		void Run(const FWalkInput& Input, const double Seconds)
		{
			const int32 Steps = FMath::RoundToInt32(Seconds / Frame);

			for (int32 Step = 0; Step < Steps; ++Step)
			{
				State = FCharacterWalkModel::Step(
					State, Input, Config, FVector::UpVector, FVector(0.0, 0.0, -981.0), true, Frame);
				Travelled += State.Velocity * Frame;
			}
		}
	};

	FWalkInput Moving(const bool bSprint = false)
	{
		FWalkInput Input;
		Input.Move = FVector2D(1.0, 0.0);
		Input.bSprint = bSprint;

		return Input;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOSprintSpendsThePoolTest,
	"SpaceMMO.Stamina.SprintSpendsThePoolThenRunsUntilLetGo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOSprintSpendsThePoolTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMOStaminaTests;

	FWalker Walker;
	const FWalkInput Sprint = Moving(true);

	// Ten a second from a full hundred: still going at 9.5 s, spent by 10.
	Walker.Run(Sprint, 9.5);
	TestTrue(FString::Printf(TEXT("Still sprinting at 9.5 s (%.1f left)"), Walker.State.Stamina), Walker.State.Stamina > 0.0);
	TestTrue(TEXT("At sprint speed"), Walker.State.Velocity.Size() > Walker.Config.TopSpeed(false) * 1.5);

	Walker.Run(Sprint, 1.0);
	TestEqual(TEXT("Spent by 10 s"), Walker.State.Stamina, 0.0, 0.01);

	// Held on past it: a run, and still a run once the pool has started filling again.
	Walker.Run(Sprint, 2.5);
	TestEqual(TEXT("Down to a run"), Walker.State.Velocity.Size(), Walker.Config.TopSpeed(false), 1.0);
	TestTrue(FString::Printf(TEXT("Refilling meanwhile (%.1f)"), Walker.State.Stamina), Walker.State.Stamina > 0.0);

	// Let go and pressed again: sprinting once more.
	Walker.Run(Moving(false), 0.1);
	Walker.Run(Sprint, 1.0);
	TestTrue(TEXT("Sprints again after letting go"), Walker.State.Velocity.Size() > Walker.Config.TopSpeed(false) * 1.5);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOStaminaRefillsTest,
	"SpaceMMO.Stamina.RefillWaitsASecondThenComesAtTwentyASecond",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOStaminaRefillsTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMOStaminaTests;

	FWalker Walker;
	FWalkInput Dodge;
	Dodge.DodgePresses = 1;

	Walker.Run(Dodge, Frame);
	TestEqual(TEXT("A dodge spends 25"), Walker.State.Stamina, 75.0, 0.01);

	Walker.Run(Dodge, 0.9);
	TestEqual(TEXT("Nothing back within the second"), Walker.State.Stamina, 75.0, 0.01);

	Walker.Run(Dodge, 0.6);
	TestEqual(TEXT("Twenty a second after it"), Walker.State.Stamina, 75.0 + 20.0 * 0.5, 0.5);

	Walker.Run(Dodge, 3.0);
	TestEqual(TEXT("Full, and no more"), Walker.State.Stamina, Walker.Config.StaminaMax, 0.01);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMODodgeCostsTest,
	"SpaceMMO.Stamina.ADodgeCostsTwentyFiveAndIsRefusedWithout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMODodgeCostsTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMOStaminaTests;

	FWalker Walker;
	Walker.State.Stamina = 24.0;

	FWalkInput Press = Moving();
	Press.DodgePresses = 1;

	Walker.Run(Press, Frame);
	TestEqual(TEXT("At 24, refused"), Walker.State.Dodge, EWalkDodge::None);
	TestEqual(TEXT("Counted as refused, for the bar's flash"), static_cast<int32>(Walker.State.DodgesRefused), 1);
	TestEqual(TEXT("Nothing spent"), Walker.State.Stamina, 24.0, 0.01);

	FWalker Enough;
	Enough.State.Stamina = 25.0;

	Enough.Run(Press, Frame);
	TestEqual(TEXT("At 25, a roll"), Enough.State.Dodge, EWalkDodge::Roll);
	TestEqual(TEXT("All of it spent"), Enough.State.Stamina, 0.0, 0.01);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMORollCarriesTest,
	"SpaceMMO.Stamina.TheRollCarriesAsFarAsItsCaptureAndCannotBeSteered",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMORollCarriesTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMOStaminaTests;

	FWalker Walker;
	const double Duration = FCharacterWalkModel::DodgeDuration(EWalkDodge::Roll, Walker.Config);
	const double Reach = FCharacterWalkModel::RollProfile().Last();

	// About 1.1 s and 2.9 m, as the capture plays (Joe, 19 October).
	TestEqual(TEXT("1.09 s"), Duration, 59.0 / 30.0 / 1.8, 0.001);
	TestEqual(TEXT("2.86 m"), Reach, 286.1, 0.5);

	// Pressed while running forward, then steered hard right throughout: it goes straight on regardless.
	FWalkInput Press = Moving();
	Press.DodgePresses = 1;
	Walker.Run(Press, Frame);

	const FVector Start = Walker.Travelled;

	FWalkInput Steer = Press;
	Steer.Move = FVector2D(0.0, 1.0);
	Walker.Run(Steer, Duration);

	const FVector Went = Walker.Travelled - Start;

	TestEqual(FString::Printf(TEXT("Along the way it began (%.1f cm)"), Went.X), Went.X, Reach, Reach * 0.03);
	TestTrue(FString::Printf(TEXT("Not steered aside (%.1f cm)"), Went.Y), FMath::Abs(Went.Y) < 5.0);
	TestEqual(TEXT("Over once its time is up"), Walker.State.Dodge, EWalkDodge::None);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOHopBackTest,
	"SpaceMMO.Stamina.StandingStillTheDodgeIsAHopBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOHopBackTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMOStaminaTests;

	FWalker Walker;
	FWalkInput Press;
	Press.DodgePresses = 1;

	Walker.Run(Press, Frame);
	TestEqual(TEXT("A hop"), Walker.State.Dodge, EWalkDodge::Hop);

	Walker.Run(Press, Walker.Config.HopSeconds);

	TestEqual(FString::Printf(TEXT("1.5 m back (%.1f cm)"), Walker.Travelled.X), Walker.Travelled.X,
		-Walker.Config.HopDistanceCentimetres, 5.0);
	TestEqual(TEXT("Still facing forward"), Walker.State.Rotation.GetForwardVector().X, 1.0, 0.001);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOUntouchableTest,
	"SpaceMMO.Stamina.UntouchableOnlyForTheFirstQuarterSecond",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOUntouchableTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMOStaminaTests;

	FWalker Walker;
	TestFalse(TEXT("Not before"), FCharacterWalkModel::IsUntouchable(Walker.State, Walker.Config));

	FWalkInput Press = Moving();
	Press.DodgePresses = 1;

	Walker.Run(Press, 0.2);
	TestTrue(TEXT("At 0.2 s"), FCharacterWalkModel::IsUntouchable(Walker.State, Walker.Config));

	Walker.Run(Press, 0.1);
	TestFalse(TEXT("At 0.3 s, still rolling"), FCharacterWalkModel::IsUntouchable(Walker.State, Walker.Config));
	TestEqual(TEXT("Still rolling"), Walker.State.Dodge, EWalkDodge::Roll);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOOnePressOneDodgeTest,
	"SpaceMMO.Stamina.OnePressIsOneDodgeAndNoJumpInOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOOnePressOneDodgeTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMOStaminaTests;

	FWalker Walker;
	FWalkInput Press = Moving();
	Press.DodgePresses = 1;

	// Held at one press for twice the roll: one roll, not two.
	Walker.Run(Press, 2.0 * FCharacterWalkModel::DodgeDuration(EWalkDodge::Roll, Walker.Config));
	TestEqual(TEXT("One dodge"), static_cast<int32>(Walker.State.DodgesStarted), 1);

	// A press during a roll, and jump held through it: neither waits for it to end.
	FWalker During;
	During.Run(Press, 0.2);

	FWalkInput Again = Press;
	Again.DodgePresses = 2;
	Again.bJump = true;
	During.Run(Again, 0.2);

	TestTrue(TEXT("No jump mid-roll"), During.State.Velocity.Z <= 0.0);

	Again.bJump = false;
	During.Run(Again, FCharacterWalkModel::DodgeDuration(EWalkDodge::Roll, During.Config));
	TestEqual(TEXT("The press made mid-roll did not queue another"), static_cast<int32>(During.State.DodgesStarted), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMODeadDoNotDodgeTest,
	"SpaceMMO.Stamina.DodgesPressedWhileDeadNeverHappen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMODeadDoNotDodgeTest::RunTest(const FString& Parameters)
{
	// On a pawn standing on the planet, through the input RPC the server takes: locked by death, a press must do nothing
	// then and nothing on coming back -- an empty input's count of none would read as a press, and presses saved up
	// would fire at once. The same press alive is the control: without it, a pawn that cannot dodge at all would pass.
	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("SpaceMMOStaminaTest"));
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();

	const ASpaceMMOPlanetActor* Planet = nullptr;

	for (TActorIterator<ASpaceMMOPlanetActor> It(World); It; ++It)
	{
		Planet = *It;
	}

	const auto Spawn = [World, Planet]()
	{
		ASpaceMMOCharacterPawn* const Walker = World->SpawnActorDeferred<ASpaceMMOCharacterPawn>(
			ASpaceMMOCharacterPawn::StaticClass(), FTransform::Identity, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

		Walker->FinishSpawning(FTransform::Identity);
		Walker->DispatchBeginPlay();
		Walker->ResumeAt(FPlanetTerrain::SurfacePosition(
			Planet->GetPlanetConfig(), Planet->GetTerrainConfig(), FVector(0.3, 1.0, 0.2).GetSafeNormal()));

		return Walker;
	};

	const auto Run = [World](ASpaceMMOCharacterPawn* Walker, const FWalkInput& Input, const int32 Ticks)
	{
		for (int32 Tick = 0; Tick < Ticks; ++Tick)
		{
			FWalkInput Parameters = Input;
			Walker->ProcessEvent(Walker->FindFunctionChecked(TEXT("ServerSendWalkInput")), &Parameters);
			World->TimeSeconds += 1.0f / 60.0f;
			Walker->Tick(1.0f / 60.0f);
		}
	};

	if (TestNotNull(TEXT("The world has its planet"), Planet))
	{
		FWalkInput Still;
		FWalkInput Pressed;
		Pressed.DodgePresses = 3;

		// The control: alive and standing, the press is a hop, and it costs.
		ASpaceMMOCharacterPawn* const Alive = Spawn();
		Run(Alive, Still, 30);

		TestTrue(TEXT("Standing on the ground"), Alive->IsOnGround());

		Run(Alive, Pressed, 5);
		TestEqual(TEXT("Alive, the press dodges"), Alive->GetStamina(), Alive->GetStaminaMax() - 25.0, 0.5);

		// Dead: the same press, nothing.
		ASpaceMMOCharacterPawn* const Dead = Spawn();
		Run(Dead, Still, 30);
		Dead->SetMovementLocked(true);

		Run(Dead, Pressed, 30);
		TestEqual(TEXT("Nothing spent while dead"), Dead->GetStamina(), Dead->GetStaminaMax(), 0.01);

		Dead->SetMovementLocked(false);
		Run(Dead, Pressed, 30);
		TestEqual(TEXT("Nothing spent on coming back"), Dead->GetStamina(), Dead->GetStaminaMax(), 0.01);
	}

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMODodgeClipsTest,
	"SpaceMMO.Stamina.TheDodgeClipsLoadAndTheRollMovesAsItsCapture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMODodgeClipsTest::RunTest(const FString& Parameters)
{
	const ASpaceMMOCharacterPawn* const Pawn = GetDefault<ASpaceMMOCharacterPawn>();

	const UAnimSequence* const Roll = Cast<UAnimSequence>(Pawn->GetDodgeRollClip().TryLoad());
	const UAnimSequence* const Hop = Cast<UAnimSequence>(Pawn->GetDodgeHopClip().TryLoad());

	if (!TestNotNull(TEXT("The roll loads"), Roll) || !TestNotNull(TEXT("The hop loads"), Hop))
	{
		return false;
	}

	// Locked to their roots: the character travels by the walk model, and a clip that moved itself would carry the
	// body off the character and snap it back when it ended.
	TestTrue(TEXT("The roll is locked to its root"), Roll->bForceRootLock && !Roll->bEnableRootMotion);
	TestTrue(TEXT("The hop is locked to its root"), Hop->bForceRootLock && !Hop->bEnableRootMotion);

	for (const FSpaceMMOCharacterBody& Body : Pawn->GetRaceBodies())
	{
		const USkeletalMesh* const Mesh = Cast<USkeletalMesh>(Body.Mesh.TryLoad());

		if (Mesh != nullptr)
		{
			TestTrue(FString::Printf(TEXT("Both play on race %d's body"), Body.Race),
				Roll->GetSkeleton() == Mesh->GetSkeleton() && Hop->GetSkeleton() == Mesh->GetSkeleton());
		}
	}

#if WITH_EDITOR
	// The walk model carries the character by the roll's own root, frame by frame from its crouch: read here off the
	// clip, so a clip re-captured or re-trimmed cannot leave the walk model moving to the old one. The clip's data model
	// is the editor's; a dedicated server build compiles these tests and has none, and never runs them.
	const IAnimationDataModel* const Model = Roll->GetDataModel();
	const TArray<double>& Profile = FCharacterWalkModel::RollProfile();
	const FName Root(TEXT("root"));
	const double Start = Model->GetBoneTrackTransform(Root, FFrameNumber(FCharacterWalkModel::RollStartFrame)).GetLocation().Y;

	TestEqual(TEXT("The profile spans the clip from its crouch"), Profile.Num(),
		Model->GetNumberOfKeys() - FCharacterWalkModel::RollStartFrame);

	double Worst = 0.0;

	for (int32 Index = 0; Index < Profile.Num(); ++Index)
	{
		const double Clip = Model->GetBoneTrackTransform(
			Root, FFrameNumber(FCharacterWalkModel::RollStartFrame + Index)).GetLocation().Y - Start;

		Worst = FMath::Max(Worst, FMath::Abs(Clip - Profile[Index]));
	}

	TestTrue(FString::Printf(TEXT("The profile is the clip's (worst %.2f cm apart)"), Worst), Worst < 0.1);
#endif

	return true;
}

#endif
