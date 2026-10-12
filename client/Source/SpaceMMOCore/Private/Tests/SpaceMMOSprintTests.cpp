#include "Animation/AnimInstance.h"
#include "AnimationRuntime.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "SpaceMMOCharacterPawn.h"
#include "SpaceMMOPlanetActor.h"
#include "SpaceMMOPlanetTerrain.h"
#include "SpaceMMOWalkModel.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * Task 191: every race sprints on its own feet, and other players see it.
 *
 * Joe, 11 October: "add a sprinting animation to all of the characters and implement it in the game", one clip
 * per race, each made on its own rig. 176 names the check that matters: in an in-place cycle a planted foot
 * slides back at exactly the speed the clip is played at, and if that is not the speed the character moves,
 * the feet skate. A clip can look right in every preview and still do it.
 *
 * So this measures the pose the game draws, not the clip. Each race's walker sprints on the Capital through the
 * input RPC the server takes; another player's copy is handed the walker's replicated state and race, as their
 * client is. Each body runs the blueprint config gives its race, and the balls of its feet are read off the
 * evaluated pose, frame by frame. Everything a sprint passes through on its way to the screen is in the
 * measurement: the race's blueprint, its blend space, the sample's speed, the clip, its retarget source.
 */
namespace SpaceMMOSprintTests
{
	// Drawn at 120 frames a second, as a client draws (182's rig measured that). Not at 60: the sprints are keyed at
	// 60, and frames landing on keys never see between them, which is where a planted foot sinks if it sinks.
	constexpr float TickSeconds = 1.0f / 120.0f;

	/** A game world for one test, torn down however the test leaves: the settlement tests' arrangement. */
	struct FScopedWorld
	{
		UWorld* World = nullptr;

		FScopedWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("SpaceMMOSprintTest"));

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

	/** A client's input reaching the server: the RPC itself, run as the net driver runs it. */
	void Send(ASpaceMMOCharacterPawn& Walker, const FWalkInput& Input)
	{
		FWalkInput Parameters = Input;
		Walker.ProcessEvent(Walker.FindFunctionChecked(TEXT("ServerSendWalkInput")), &Parameters);
	}

	/** Replication reaching another client's copy: the property written whole, then its notify, as the engine does. */
	void Replicate(const ASpaceMMOCharacterPawn& From, ASpaceMMOCharacterPawn& To, const TCHAR* Property, const TCHAR* Notify)
	{
		FindFProperty<FProperty>(ASpaceMMOCharacterPawn::StaticClass(), Property)->CopyCompleteValue_InContainer(&To, &From);

		if (Notify != nullptr)
		{
			To.ProcessEvent(To.FindFunctionChecked(Notify), nullptr);
		}
	}

	/** One body's feet over a stretch of sprinting, in centimetres in its own space as drawn. */
	struct FFeet
	{
		TArray<FVector> Left;
		TArray<FVector> Right;
		double Scale = 1.0;

		/** How high the balls of the feet are when this body stands in its reference pose, flat on the ground. */
		double Standing = 0.0;

		/** Runs the body's blueprint for a frame and records where the balls of its feet are drawn. */
		void Record(USkeletalMeshComponent& Mesh)
		{
			Mesh.TickAnimation(TickSeconds, false);
			Mesh.RefreshBoneTransforms();

			const int32 BallL = Mesh.GetBoneIndex(TEXT("ball_l"));
			const int32 BallR = Mesh.GetBoneIndex(TEXT("ball_r"));
			const FReferenceSkeleton& Reference = Mesh.GetSkeletalMeshAsset()->GetRefSkeleton();

			Scale = Mesh.GetComponentScale().X;
			Standing = 0.5 * (FAnimationRuntime::GetComponentSpaceTransformRefPose(Reference, BallL).GetLocation().Z
				+ FAnimationRuntime::GetComponentSpaceTransformRefPose(Reference, BallR).GetLocation().Z);
			Left.Add(Mesh.GetBoneTransform(BallL, FTransform::Identity).GetLocation());
			Right.Add(Mesh.GetBoneTransform(BallR, FTransform::Identity).GetLocation());
		}

		/**
		 * How far the balls of the feet sink below their standing height, in centimetres, at worst. A clip keyed at
		 * 30 a second sank the Humanoid man's 4.4 cm between keys: the engine interpolates the joints there, and
		 * the foot swings through an arc under the line it was planted along.
		 */
		double Sag() const
		{
			double Lowest = TNumericLimits<double>::Max();

			for (const TArray<FVector>* Track : { &Left, &Right })
			{
				for (const FVector& At : *Track)
				{
					Lowest = FMath::Min(Lowest, At.Z);
				}
			}

			return (Standing - Lowest) * Scale;
		}

		/**
		 * How fast a planted foot slides back, in centimetres a second: the median, across the ground, of the
		 * steps between consecutive frames that both have the ball within 2.5 cm of the lowest it gets. Also
		 * how many such steps.
		 *
		 * The band is not tighter because between two planted keys the joints' rotations are interpolated and
		 * the ball swings through an arc a little below them (Sag). The median, because a frame descending onto
		 * the ground can fall in the band too.
		 */
		TPair<double, int32> PlantedSlide() const
		{
			TArray<double> Speeds;

			for (const TArray<FVector>* Track : { &Left, &Right })
			{
				double Lowest = TNumericLimits<double>::Max();

				for (const FVector& At : *Track)
				{
					Lowest = FMath::Min(Lowest, At.Z);
				}

				for (int32 Index = 1; Index < Track->Num(); ++Index)
				{
					const FVector& Before = (*Track)[Index - 1];
					const FVector& Now = (*Track)[Index];

					if (Before.Z - Lowest < 2.5 && Now.Z - Lowest < 2.5)
					{
						Speeds.Add(FVector2D(Now - Before).Size() * Scale / TickSeconds);
					}
				}
			}

			if (Speeds.IsEmpty())
			{
				return { 0.0, 0 };
			}

			Speeds.Sort();

			return { Speeds[Speeds.Num() / 2], Speeds.Num() };
		}

		/** The left ball's heights and steps, for the log: what the measurement saw, when it finds nothing. */
		FString Trace() const
		{
			FString Out;

			for (int32 Index = 0; Index < FMath::Min(16, Left.Num()); ++Index)
			{
				Out += FString::Printf(TEXT(" (%.1f, %.1f)"), Left[Index].Y, Left[Index].Z);
			}

			return Out;
		}
	};

	/** What one sprint measured: the walker's speed and how fast each body's planted feet slide. */
	struct FSprint
	{
		double GroundCentimetres = 0.0;
		TPair<double, int32> Walker;
		TPair<double, int32> Copy;
		UClass* WalkerPlays = nullptr;
		UClass* CopyPlays = nullptr;
		FString WalkerTrace;
		double WalkerSag = 0.0;
		double CopySag = 0.0;
	};

	/**
	 * A walker of this race and another player's copy of them, sprinting for two seconds and then measured
	 * for one and a half. PlayOnWalker, when set, replaces the blueprint the walker's race gives it: the control.
	 */
	FSprint Sprint(UWorld& World, const ASpaceMMOPlanetActor& Planet, const int32 Race, UClass* PlayOnWalker = nullptr)
	{
		ASpaceMMOCharacterPawn* const Walker = World.SpawnActorDeferred<ASpaceMMOCharacterPawn>(
			ASpaceMMOCharacterPawn::StaticClass(), FTransform::Identity, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		ASpaceMMOCharacterPawn* const Copy = World.SpawnActorDeferred<ASpaceMMOCharacterPawn>(
			ASpaceMMOCharacterPawn::StaticClass(), FTransform::Identity, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

		FSprint Out;

		if (Walker == nullptr || Copy == nullptr)
		{
			return Out;
		}

		Walker->FinishSpawning(FTransform::Identity);
		Copy->SetRole(ROLE_SimulatedProxy);
		Copy->FinishSpawning(FTransform::Identity);

		// Begun by hand: a world with no game mode begins play for nobody.
		Walker->DispatchBeginPlay();
		Copy->DispatchBeginPlay();

		// The server takes the owning client's word for the race (182); the copy gets it by replication.
		Walker->SetBodyRace(Race);
		Replicate(*Walker, *Copy, TEXT("BodyRace"), TEXT("OnRep_BodyRace"));

		if (PlayOnWalker != nullptr)
		{
			Walker->GetBodyMesh()->SetAnimInstanceClass(PlayOnWalker);
		}

		Walker->ResumeAt(FPlanetTerrain::SurfacePosition(
			Planet.GetPlanetConfig(), Planet.GetTerrainConfig(), FVector(0.3, 1.0, 0.2).GetSafeNormal()));

		FWalkInput Input;
		Input.Move = FVector2D(1.0, 0.0);
		Input.bSprint = true;

		FFeet WalkerFeet;
		FFeet CopyFeet;

		const int32 Warmup = FMath::RoundToInt32(2.0f / TickSeconds);
		const int32 Window = FMath::RoundToInt32(1.5f / TickSeconds);

		for (int32 Frame = 0; Frame < Warmup + Window; ++Frame)
		{
			World.TimeSeconds += TickSeconds;
			Send(*Walker, Input);
			Walker->Tick(TickSeconds);
			Replicate(*Walker, *Copy, TEXT("NetState"), nullptr);
			Copy->Tick(TickSeconds);

			// The blueprints run every frame, so their state machines and blends are where a player's would be;
			// only the last second and a half is measured, at full speed.
			if (Frame == Warmup)
			{
				WalkerFeet = FFeet();
				CopyFeet = FFeet();
				Out.GroundCentimetres = Walker->GetGroundSpeedMetresPerSecond() * 100.0;
			}

			WalkerFeet.Record(*Walker->GetBodyMesh());
			CopyFeet.Record(*Copy->GetBodyMesh());
		}

		Out.Walker = WalkerFeet.PlantedSlide();
		Out.Copy = CopyFeet.PlantedSlide();
		Out.WalkerTrace = WalkerFeet.Trace();
		Out.WalkerSag = WalkerFeet.Sag();
		Out.CopySag = CopyFeet.Sag();
		Out.WalkerPlays = Walker->GetBodyMesh()->GetAnimClass();
		Out.CopyPlays = Copy->GetBodyMesh()->GetAnimClass();

		Walker->Destroy();
		Copy->Destroy();

		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOEachRaceSprintsOnItsOwnFeetTest,
	"SpaceMMO.Character.EachRaceSprintsOnItsOwnFeet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOEachRaceSprintsOnItsOwnFeetTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMOSprintTests;

	const ASpaceMMOCharacterPawn* const Defaults = GetDefault<ASpaceMMOCharacterPawn>();
	const TArray<FSpaceMMOCharacterBody>& Bodies = Defaults->GetRaceBodies();

	if (!TestTrue(TEXT("DefaultGame.ini names race bodies"), Bodies.Num() > 0))
	{
		return false;
	}

	FScopedWorld Scoped;
	const ASpaceMMOPlanetActor* Planet = nullptr;

	for (TActorIterator<ASpaceMMOPlanetActor> It(Scoped.World); It; ++It)
	{
		Planet = *It;
	}

	if (!TestNotNull(TEXT("The world has its planet"), Planet))
	{
		return false;
	}

	// The speed the model sprints at, which each race's sprint sample is placed at.
	const double SprintCentimetres = Defaults->GetWalkConfig().TopSpeed(true);

	for (const FSpaceMMOCharacterBody& Body : Bodies)
	{
		UClass* const Expected = ASpaceMMOCharacterPawn::AnimClassFor(&Body, Defaults->GetDefaultAnimClass())
			.TryLoadClass<UAnimInstance>();

		if (!TestNotNull(*FString::Printf(TEXT("Race %d's animation blueprint loads"), Body.Race), Expected))
		{
			continue;
		}

		const FSprint Measured = Sprint(*Scoped.World, *Planet, Body.Race);

		AddInfo(FString::Printf(TEXT("Race %d sprints at %.1f cm/s playing %s. Planted, the walker's feet slide at %.1f cm/s "
				"(%d steps) and the copy's at %.1f (%d); they sink %.2f and %.2f cm below standing at worst. The walker's left "
				"ball, forward and up, from the first measured frame:%s"),
			Body.Race, Measured.GroundCentimetres, *GetNameSafe(Measured.WalkerPlays), Measured.Walker.Key,
			Measured.Walker.Value, Measured.Copy.Key, Measured.Copy.Value, Measured.WalkerSag, Measured.CopySag,
			*Measured.WalkerTrace));

		// Not into the ground: a centimetre and a half is where it starts to read on a flat floor.
		TestTrue(*FString::Printf(TEXT("Race %d's feet stay out of the ground (%.2f cm below standing at worst)"),
				Body.Race, Measured.WalkerSag),
			Measured.WalkerSag <= 1.5);
		TestTrue(*FString::Printf(TEXT("And on another player's screen (%.2f cm)"), Measured.CopySag), Measured.CopySag <= 1.5);

		// Or every comparison below is a standing character's feet agreeing with a standing character.
		TestEqual(*FString::Printf(TEXT("Race %d really sprints"), Body.Race), Measured.GroundCentimetres, SprintCentimetres, 5.0);

		TestTrue(*FString::Printf(TEXT("Race %d's walker plays its race's blueprint"), Body.Race), Measured.WalkerPlays == Expected);
		TestTrue(*FString::Printf(TEXT("Another player's copy of race %d plays it too"), Body.Race), Measured.CopyPlays == Expected);

		// A stance of two thirtieths of a second, drawn at 120, is eight frames a foot a stride.
		TestTrue(*FString::Printf(TEXT("Race %d's walker plants its feet (%d steps)"), Body.Race, Measured.Walker.Value),
			Measured.Walker.Value >= 6);
		TestTrue(*FString::Printf(TEXT("Race %d's copy plants its feet (%d steps)"), Body.Race, Measured.Copy.Value),
			Measured.Copy.Value >= 6);

		// 176's check, within its 5%: the planted foot slides at the speed the body moves.
		TestTrue(*FString::Printf(TEXT("Race %d's planted feet slide at its sprint, %.1f cm/s against %.1f"),
				Body.Race, Measured.Walker.Key, Measured.GroundCentimetres),
			FMath::Abs(Measured.Walker.Key - Measured.GroundCentimetres) <= 0.05 * Measured.GroundCentimetres);
		TestTrue(*FString::Printf(TEXT("On another player's screen too, %.1f cm/s against %.1f"),
				Measured.Copy.Key, Measured.GroundCentimetres),
			FMath::Abs(Measured.Copy.Key - Measured.GroundCentimetres) <= 0.05 * Measured.GroundCentimetres);
	}

	// The control: the Humanoid man's sprint on the Martian, which is what one clip for every race would have
	// been. It has to miss, or this measurement cannot tell one body's clip from another's and proves nothing.
	const FSpaceMMOCharacterBody* const Martian = ASpaceMMOCharacterPawn::FindBody(Bodies, 1);
	UClass* const Shared = Defaults->GetDefaultAnimClass().TryLoadClass<UAnimInstance>();

	if (TestNotNull(TEXT("The Martian has a body"), Martian) && TestNotNull(TEXT("CharacterAnimClass loads"), Shared))
	{
		const FSprint Control = Sprint(*Scoped.World, *Planet, Martian->Race, Shared);

		AddInfo(FString::Printf(TEXT("Control: the Martian playing %s slides his planted feet at %.1f cm/s (%d steps), sprinting at %.1f."),
			*GetNameSafe(Control.WalkerPlays), Control.Walker.Key, Control.Walker.Value, Control.GroundCentimetres));

		TestTrue(TEXT("The control plays the shared blueprint"), Control.WalkerPlays == Shared);
		TestTrue(*FString::Printf(TEXT("Another body's sprint on the Martian slides more than 10%% off, at %.1f cm/s against %.1f"),
				Control.Walker.Key, Control.GroundCentimetres),
			FMath::Abs(Control.Walker.Key - Control.GroundCentimetres) > 0.10 * Control.GroundCentimetres);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOSprintIsTheTopSpeedTest,
	"SpaceMMO.Walk.SprintIsWalkSpeedTimesTheMultiplier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOSprintIsTheTopSpeedTest::RunTest(const FString& Parameters)
{
	// The number each race's sprint sample is placed at (191) and the one the model accelerates toward (136).
	FWalkConfig Config;
	Config.WalkSpeed = 600.0;
	Config.SprintMultiplier = 1.8;

	TestEqual(TEXT("Walking tops out at WalkSpeed"), Config.TopSpeed(false), 600.0, 1e-9);
	TestEqual(TEXT("Sprinting at WalkSpeed times SprintMultiplier"), Config.TopSpeed(true), 1080.0, 1e-9);

	// A multiplier under one would make the sprint key a brake.
	Config.SprintMultiplier = 0.5;
	TestEqual(TEXT("A sprint is never slower than a walk"), Config.TopSpeed(true), 600.0, 1e-9);

	// And it is the speed the model really settles at, not a number kept beside it.
	Config.SprintMultiplier = 1.8;
	FWalkState State;
	FWalkInput Input;
	Input.Move = FVector2D(1.0, 0.0);
	Input.bSprint = true;

	for (int32 Frame = 0; Frame < 240; ++Frame)
	{
		State = FCharacterWalkModel::Step(State, Input, Config, FVector::UpVector, FVector::ZeroVector, true, 1.0 / 60.0);
	}

	TestEqual(TEXT("The model settles at TopSpeed(true)"),
		FCharacterWalkModel::GroundSpeed(State, FVector::UpVector), Config.TopSpeed(true), 1.0);

	return true;
}

#endif
