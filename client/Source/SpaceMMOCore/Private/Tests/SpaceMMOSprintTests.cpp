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

		/**
		 * Each step's reach: how far apart fore and aft the balls of the feet get between one passing of the legs and
		 * the next, in centimetres, for every step the stretch holds whole. Two gaits blended in step reach about as
		 * far as each other every step. Out of step, one gait's forward leg is the other's back one, and the blend's
		 * steps come up short: all of them, matched by length a stride against two (18-30 cm against a walk's 57-76);
		 * a run of them as two unsynced clips drift through each other.
		 */
		TArray<double> Steps() const
		{
			TArray<double> Out;
			int32 Passings = 0;
			double Reach = 0.0;

			for (int32 Index = 1; Index < Left.Num(); ++Index)
			{
				const double Before = Left[Index - 1].Y - Right[Index - 1].Y;
				const double Now = Left[Index].Y - Right[Index].Y;

				if ((Before < 0.0) != (Now < 0.0))
				{
					// The stretch starts and ends mid-step; only what lies between two passings is a whole one.
					if (Passings > 0)
					{
						Out.Add(Reach * Scale);
					}

					++Passings;
					Reach = 0.0;
				}

				Reach = FMath::Max(Reach, FMath::Abs(Now));
			}

			return Out;
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

	/** The joints whose place across the body a sprint must keep from the run (Joe, 13 October). */
	const TCHAR* const ShapeBones[] = { TEXT("lowerarm_l"), TEXT("hand_l"), TEXT("calf_l"), TEXT("foot_l"),
		TEXT("lowerarm_r"), TEXT("hand_r"), TEXT("calf_r"), TEXT("foot_r") };
	constexpr int32 ShapeBoneCount = UE_ARRAY_COUNT(ShapeBones);

	/**
	 * How far across the body each of ShapeBones is drawn over a stretch, centimetres from the midline: the
	 * nearest and furthest. Their middle and width do not depend on how many strides the stretch happens to
	 * hold, which a mean would.
	 */
	struct FShape
	{
		TArray<double> Low;
		TArray<double> High;

		/** Where each joint is, all three ways, when it is furthest in and furthest out: which part of the stride. */
		TArray<FVector> AtLow;
		TArray<FVector> AtHigh;

		/** After FFeet::Record has run the body's blueprint for the frame. */
		void Record(const USkeletalMeshComponent& Mesh)
		{
			if (Low.IsEmpty())
			{
				Low.Init(TNumericLimits<double>::Max(), ShapeBoneCount);
				High.Init(TNumericLimits<double>::Lowest(), ShapeBoneCount);
				AtLow.Init(FVector::ZeroVector, ShapeBoneCount);
				AtHigh.Init(FVector::ZeroVector, ShapeBoneCount);
			}

			for (int32 Index = 0; Index < ShapeBoneCount; ++Index)
			{
				const FVector At = Mesh.GetBoneTransform(Mesh.GetBoneIndex(ShapeBones[Index]), FTransform::Identity)
					.GetLocation() * Mesh.GetComponentScale().X;

				if (At.X < Low[Index])
				{
					Low[Index] = At.X;
					AtLow[Index] = At;
				}

				if (At.X > High[Index])
				{
					High[Index] = At.X;
					AtHigh[Index] = At;
				}
			}
		}

		double Middle(const int32 Index) const { return Low.IsValidIndex(Index) ? 0.5 * (Low[Index] + High[Index]) : 0.0; }
		double Width(const int32 Index) const { return Low.IsValidIndex(Index) ? High[Index] - Low[Index] : 0.0; }
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
		FShape WalkerShape;
		FShape CopyShape;
		TArray<double> WalkerSteps;
		TArray<double> CopySteps;
	};

	/** Steps' reaches for the log, to the centimetre. */
	FString Listed(const TArray<double>& Steps)
	{
		FString Out;

		for (const double Step : Steps)
		{
			Out += FString::Printf(TEXT("%s%.0f"), Out.IsEmpty() ? TEXT("") : TEXT(" "), Step);
		}

		return Out;
	}

	/** The shortest of Steps, or 0 with none: a stretch that holds no whole step reaches nowhere. */
	double Shortest(const TArray<double>& Steps)
	{
		return Steps.IsEmpty() ? 0.0 : FMath::Min(Steps);
	}

	/**
	 * A walker of this race and another player's copy of them, sprinting for two seconds and then measured
	 * for one and a half. PlayOnWalker, when set, replaces the blueprint the walker's race gives it: the control.
	 * bSprint false runs instead; SprintMultiplier, when set, changes the walker's, so it sprints at a speed
	 * between the run's and the sprint's and the blend space plays a mix of both. Throttle is how far forward the
	 * stick is pushed: a third of the way is the walk's 2 m/s, two thirds halfway from it to the run. ChangeAfter,
	 * when set, is a change of pace inside the measured stretch: until that many seconds into it Shift is the
	 * other way from bSprint, and from then on it is bSprint.
	 */
	FSprint Sprint(UWorld& World, const ASpaceMMOPlanetActor& Planet, const int32 Race, UClass* PlayOnWalker = nullptr,
		const bool bSprint = true, const double SprintMultiplier = 0.0, const double Throttle = 1.0,
		const double ChangeAfter = -1.0)
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

		// The walker's own walk config, which the server simulates it by; the copy follows what it sends.
		if (SprintMultiplier > 0.0)
		{
			FindFProperty<FStructProperty>(ASpaceMMOCharacterPawn::StaticClass(), TEXT("WalkConfig"))
				->ContainerPtrToValuePtr<FWalkConfig>(Walker)->SprintMultiplier = SprintMultiplier;
		}

		Walker->ResumeAt(FPlanetTerrain::SurfacePosition(
			Planet.GetPlanetConfig(), Planet.GetTerrainConfig(), FVector(0.3, 1.0, 0.2).GetSafeNormal()));

		FWalkInput Input;
		Input.Move = FVector2D(Throttle, 0.0);
		Input.bSprint = bSprint;

		FFeet WalkerFeet;
		FFeet CopyFeet;

		const int32 Warmup = FMath::RoundToInt32(2.0f / TickSeconds);
		const int32 Window = FMath::RoundToInt32(1.5f / TickSeconds);
		const int32 Change = ChangeAfter >= 0.0 ? Warmup + FMath::RoundToInt32(ChangeAfter / TickSeconds) : 0;

		for (int32 Frame = 0; Frame < Warmup + Window; ++Frame)
		{
			World.TimeSeconds += TickSeconds;
			Input.bSprint = Frame < Change ? !bSprint : bSprint;
			Send(*Walker, Input);
			Walker->Tick(TickSeconds);
			Replicate(*Walker, *Copy, TEXT("NetState"), nullptr);
			Copy->Tick(TickSeconds);

			// The blueprints run every frame, so their state machines and blends are where a player's would be;
			// only the last second and a half is measured, at full speed or through the change of pace.
			if (Frame == Warmup)
			{
				WalkerFeet = FFeet();
				CopyFeet = FFeet();
				Out.WalkerShape = FShape();
				Out.CopyShape = FShape();
				Out.GroundCentimetres = Walker->GetGroundSpeedMetresPerSecond() * 100.0;
			}

			WalkerFeet.Record(*Walker->GetBodyMesh());
			CopyFeet.Record(*Copy->GetBodyMesh());
			Out.WalkerShape.Record(*Walker->GetBodyMesh());
			Out.CopyShape.Record(*Copy->GetBodyMesh());
		}

		Out.Walker = WalkerFeet.PlantedSlide();
		Out.Copy = CopyFeet.PlantedSlide();
		Out.WalkerTrace = WalkerFeet.Trace();
		Out.WalkerSag = WalkerFeet.Sag();
		Out.CopySag = CopyFeet.Sag();
		Out.WalkerSteps = WalkerFeet.Steps();
		Out.CopySteps = CopyFeet.Steps();
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

/*
 * Joe, 13 October: going from the run to the sprint "the arms and legs move much differently outward/inward as
 * compared to the normal movement animation. I'd like it to be more of a seamless transition".
 *
 * Measured, the run every race plays (the library's jog) swings each hand across the chest at the front of its stride
 * and out past the hip at the back, and flares the knees as each leg swings through; the first sprints swung the
 * arms straight and held the knees in line. Each blend between the two dragged the joints sideways: the Martian's
 * hand moved 21 cm at the front of its swing, the orc's 29.
 *
 * So the sprint must keep the run's place across the body, joint by joint: the middle of each joint's sideways
 * sweep, and how wide it is. Fore and aft and up and down it may differ; that is what makes it a sprint. And the
 * blend halfway between them, which a character passes through on every change of pace, must not wander outside
 * them. Measured on the drawn pose of a walker and of another player's copy, at the walk (2 m/s), halfway to the run
 * (4 m/s), the run (6 m/s), halfway to the sprint (8.4 m/s) and the sprint (10.8 m/s), and through the change itself,
 * Shift pressed and released.
 *
 * Halfway took more than the shape. ABP_Human's speed blend space had no sync group, so its samples ran on their own
 * clocks and a sprint could blend in on the opposite foot to the run; by fraction of their length, the run's
 * quarter-stride stance was averaged with the sprint's seventh, a planted leg with a folding one; and once in a
 * group, the walk, one stride to the run's two, was matched to it by length and shuffled. The blend space is in a
 * sync group now, and the walk, the run and every sprint carry footfall markers, so each blends with the next foot by
 * foot: held here by how far each step reaches. The library's own walk-to-run blend is reported sideways beside the
 * sprint's as a yardstick.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOSprintKeepsTheRunsShapeTest,
	"SpaceMMO.Character.SprintKeepsTheRunsShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOSprintKeepsTheRunsShapeTest::RunTest(const FString& Parameters)
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

	const FWalkConfig& Walk = Defaults->GetWalkConfig();
	const double Halfway = 0.5 * (1.0 + Walk.SprintMultiplier);

	for (const FSpaceMMOCharacterBody& Body : Bodies)
	{
		const FSprint Run = Sprint(*Scoped.World, *Planet, Body.Race, nullptr, false);
		const FSprint Half = Sprint(*Scoped.World, *Planet, Body.Race, nullptr, true, Halfway);
		const FSprint Full = Sprint(*Scoped.World, *Planet, Body.Race);

		// The library's own walk-to-run blend, which nobody has found odd sideways: the yardstick. Fore and aft it is held
		// in step like the sprint's, below, since the same sync group holds it.
		const FSprint WalkPace = Sprint(*Scoped.World, *Planet, Body.Race, nullptr, false, 0.0, 1.0 / 3.0);
		const FSprint WalkRun = Sprint(*Scoped.World, *Planet, Body.Race, nullptr, false, 0.0, 2.0 / 3.0);

		for (int32 Bone = 0; Bone < ShapeBoneCount; ++Bone)
		{
			const FShape& W = WalkPace.WalkerShape;
			const FShape& M = WalkRun.WalkerShape;
			const FShape& R = Run.WalkerShape;
			const FShape& H = Half.WalkerShape;
			const FShape& S = Full.WalkerShape;
			const auto Outside = [](const double At, const double A, const double B)
			{
				return FMath::Max3(0.0, At - FMath::Max(A, B), FMath::Min(A, B) - At);
			};

			AddInfo(FString::Printf(TEXT("race %d, %s at its extremes across (x, y forward, z up), run %s %s; halfway %s %s; "
					"sprint %s %s."),
				Body.Race, ShapeBones[Bone], *R.AtLow[Bone].ToCompactString(), *R.AtHigh[Bone].ToCompactString(),
				*H.AtLow[Bone].ToCompactString(), *H.AtHigh[Bone].ToCompactString(), *S.AtLow[Bone].ToCompactString(),
				*S.AtHigh[Bone].ToCompactString()));
			AddInfo(FString::Printf(TEXT("race %d, %s halfway: walk-to-run %.1f cm outside its ends, %.1f wider (walk %.1f, %.1f; "
					"halfway %.1f, %.1f; run %.1f, %.1f); run-to-sprint %.1f outside, %.1f wider."),
				Body.Race, ShapeBones[Bone],
				Outside(M.Middle(Bone), W.Middle(Bone), R.Middle(Bone)), M.Width(Bone) - FMath::Max(W.Width(Bone), R.Width(Bone)),
				W.Middle(Bone), W.Width(Bone), M.Middle(Bone), M.Width(Bone), R.Middle(Bone), R.Width(Bone),
				Outside(H.Middle(Bone), R.Middle(Bone), S.Middle(Bone)), H.Width(Bone) - FMath::Max(R.Width(Bone), S.Width(Bone))));
		}

		// Or every comparison is between three paces that are the same pace.
		TestEqual(*FString::Printf(TEXT("Race %d really runs"), Body.Race), Run.GroundCentimetres, Walk.TopSpeed(false), 5.0);
		TestEqual(*FString::Printf(TEXT("Race %d really goes halfway"), Body.Race), Half.GroundCentimetres,
			Walk.WalkSpeed * Halfway, 5.0);
		TestEqual(*FString::Printf(TEXT("Race %d really sprints"), Body.Race), Full.GroundCentimetres, Walk.TopSpeed(true), 5.0);

		for (const bool bCopy : { false, true })
		{
			const FShape& R = bCopy ? Run.CopyShape : Run.WalkerShape;
			const FShape& H = bCopy ? Half.CopyShape : Half.WalkerShape;
			const FShape& S = bCopy ? Full.CopyShape : Full.WalkerShape;
			const TCHAR* const Who = bCopy ? TEXT("another player's copy of race") : TEXT("race");

			for (int32 Bone = 0; Bone < ShapeBoneCount; ++Bone)
			{
				AddInfo(FString::Printf(TEXT("%s %d, %s across the body (middle, width): run %.1f, %.1f; halfway %.1f, %.1f; "
						"sprint %.1f, %.1f cm."),
					Who, Body.Race, ShapeBones[Bone], R.Middle(Bone), R.Width(Bone), H.Middle(Bone), H.Width(Bone),
					S.Middle(Bone), S.Width(Bone)));

				TestTrue(*FString::Printf(TEXT("%s %d's %s sprints where it runs, across the body: %.1f against %.1f cm"),
						Who, Body.Race, ShapeBones[Bone], S.Middle(Bone), R.Middle(Bone)),
					FMath::Abs(S.Middle(Bone) - R.Middle(Bone)) <= 2.0);
				TestTrue(*FString::Printf(TEXT("%s %d's %s swings as far across: %.1f cm wide against %.1f"),
						Who, Body.Race, ShapeBones[Bone], S.Width(Bone), R.Width(Bone)),
					FMath::Abs(S.Width(Bone) - R.Width(Bone)) <= 3.0);
				TestTrue(*FString::Printf(TEXT("%s %d's %s stays between them halfway: %.1f against %.1f and %.1f cm"),
						Who, Body.Race, ShapeBones[Bone], H.Middle(Bone), R.Middle(Bone), S.Middle(Bone)),
					H.Middle(Bone) >= FMath::Min(R.Middle(Bone), S.Middle(Bone)) - 1.0
						&& H.Middle(Bone) <= FMath::Max(R.Middle(Bone), S.Middle(Bone)) + 1.0);

				// And sweeps no wider: unsynced, the ankles swung 3.5-10.6 cm wider halfway than in either clip, the feet
				// thrown out and in through every change of pace.
				TestTrue(*FString::Printf(TEXT("%s %d's %s swings no wider halfway: %.1f cm against %.1f and %.1f"),
						Who, Body.Race, ShapeBones[Bone], H.Width(Bone), R.Width(Bone), S.Width(Bone)),
					H.Width(Bone) <= FMath::Max(R.Width(Bone), S.Width(Bone)) + 1.5);
			}
		}

		// In step, fore and aft, through both blends a character passes through: walk to run as it sets off, run to
		// sprint on Shift. The sync group that keeps the run and the sprint in step keeps the walk with the run too,
		// and the walk holds one stride to the run's two: matched by length, its left foot comes down on the run's right.
		for (const bool bCopy : { false, true })
		{
			const auto StepsOf = [bCopy](const FSprint& Pace) -> const TArray<double>& { return bCopy ? Pace.CopySteps : Pace.WalkerSteps; };
			const TCHAR* const Who = bCopy ? TEXT("another player's copy of race") : TEXT("race");

			AddInfo(FString::Printf(TEXT("%s %d's steps, how far apart the feet reach fore and aft (cm): walk %s; halfway to the "
					"run %s; run %s; halfway to the sprint %s; sprint %s."),
				Who, Body.Race, *Listed(StepsOf(WalkPace)), *Listed(StepsOf(WalkRun)), *Listed(StepsOf(Run)),
				*Listed(StepsOf(Half)), *Listed(StepsOf(Full))));

			struct FBlend
			{
				const TCHAR* Name;
				const FSprint& From;
				const FSprint& Between;
				const FSprint& To;
			};

			const FBlend Blends[] = { { TEXT("walk to run"), WalkPace, WalkRun, Run }, { TEXT("run to sprint"), Run, Half, Full } };

			for (const FBlend& Blend : Blends)
			{
				const TArray<double>& Steps = StepsOf(Blend.Between);
				const double Ends = FMath::Min(Shortest(StepsOf(Blend.From)), Shortest(StepsOf(Blend.To)));

				TestTrue(*FString::Printf(TEXT("%s %d takes whole steps halfway from %s (%d)"), Who, Body.Race, Blend.Name, Steps.Num()),
					Steps.Num() >= 2);
				TestTrue(*FString::Printf(TEXT("%s %d's legs stay in step halfway from %s: its shortest step reaches %.0f cm, "
						"its ends' %.0f"), Who, Body.Race, Blend.Name, Shortest(Steps), Ends),
					Shortest(Steps) >= 0.75 * Ends);
			}
		}

		// And through the change itself, which is what Joe watched rather than a pace held halfway: Shift pressed half
		// a second into the measured stretch, and released. Every joint stays inside the sweep the run and the sprint
		// make between them, and every step reaches as far as halfway must.
		const FSprint Press = Sprint(*Scoped.World, *Planet, Body.Race, nullptr, true, 0.0, 1.0, 0.5);
		const FSprint Release = Sprint(*Scoped.World, *Planet, Body.Race, nullptr, false, 0.0, 1.0, 0.5);

		for (const bool bCopy : { false, true })
		{
			const FShape& R = bCopy ? Run.CopyShape : Run.WalkerShape;
			const FShape& S = bCopy ? Full.CopyShape : Full.WalkerShape;
			const double Ends = FMath::Min(Shortest(bCopy ? Run.CopySteps : Run.WalkerSteps),
				Shortest(bCopy ? Full.CopySteps : Full.WalkerSteps));
			const TCHAR* const Who = bCopy ? TEXT("another player's copy of race") : TEXT("race");

			for (const FSprint* const Changing : { &Press, &Release })
			{
				const FShape& C = bCopy ? Changing->CopyShape : Changing->WalkerShape;
				const TArray<double>& Steps = bCopy ? Changing->CopySteps : Changing->WalkerSteps;
				const TCHAR* const Shift = Changing == &Press ? TEXT("pressed") : TEXT("released");

				AddInfo(FString::Printf(TEXT("%s %d with Shift %s: steps %s cm."), Who, Body.Race, Shift, *Listed(Steps)));

				for (int32 Bone = 0; Bone < ShapeBoneCount; ++Bone)
				{
					const double Low = FMath::Min(R.Low[Bone], S.Low[Bone]);
					const double High = FMath::Max(R.High[Bone], S.High[Bone]);

					TestTrue(*FString::Printf(TEXT("%s %d's %s stays inside the run's and the sprint's sweep with Shift %s: "
							"%.1f to %.1f cm across, against %.1f to %.1f"),
							Who, Body.Race, ShapeBones[Bone], Shift, C.Low[Bone], C.High[Bone], Low, High),
						C.Low[Bone] >= Low - 1.5 && C.High[Bone] <= High + 1.5);
				}

				TestTrue(*FString::Printf(TEXT("%s %d takes whole steps with Shift %s (%d)"), Who, Body.Race, Shift, Steps.Num()),
					Steps.Num() >= 3);
				TestTrue(*FString::Printf(TEXT("%s %d's legs stay in step with Shift %s: its shortest step reaches %.0f cm, "
						"the run's and sprint's %.0f"), Who, Body.Race, Shift, Shortest(Steps), Ends),
					Shortest(Steps) >= 0.75 * Ends);
			}
		}
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
