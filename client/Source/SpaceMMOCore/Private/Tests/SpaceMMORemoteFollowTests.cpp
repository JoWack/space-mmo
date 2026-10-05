#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Math/RandomStream.h"
#include "Misc/AutomationTest.h"
#include "SpaceMMOCharacterPawn.h"
#include "SpaceMMOFlightModel.h"
#include "SpaceMMOPlanetActor.h"
#include "SpaceMMOPlanetTerrain.h"
#include "SpaceMMOWalkModel.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * Task 182: how a client draws another player, from the states the server sends.
 *
 * The stream is the one measured on 5 October with three headless clients on the staged server
 * (scripts/rig-remote-drawing.ps1): states sent 30 times a second and arriving 40-75 ms later, so 21-25
 * reach the client each second, unevenly; a walker running 6 m/s round a 13 m circle and stopping for a
 * second in every eight, which is SpaceMMO.AutoWalk. The client draws at 120 frames a second.
 *
 * A first version of this test sent states perfectly evenly. Each then moved the projection by 0.15 cm, so
 * it passed with FRemoteFollower's continuity removed: uneven arrival is what makes the jumps the
 * follower exists to hide, and a stream without it tests nothing about them.
 */
namespace SpaceMMORemoteFollowTests
{
	constexpr double SendSeconds = 1.0 / 30.0;
	constexpr double FrameSeconds = 1.0 / 120.0;
	constexpr double Speed = 600.0;                     // cm/s
	constexpr double RadiusCentimetres = 1300.0;
	constexpr double CmPerKm = 100000.0;

	struct FSample
	{
		double ServerTime = 0.0;
		double ArrivesAt = 0.0;
		FSystemCoordinate Position;
		FVector Velocity = FVector::ZeroVector;
	};

	/** The walker, sampled at each send, with when each state reaches the client. */
	TArray<FSample> Stream(const double Seconds)
	{
		FRandomStream Jitter(182);
		TArray<FSample> Out;
		double Angle = 0.0;

		for (double T = 0.0; T < Seconds; T += SendSeconds)
		{
			const bool bMoving = FMath::Fmod(T, 8.0) < 7.0;
			const double Omega = bMoving ? Speed / RadiusCentimetres : 0.0;

			FSample S;
			S.ServerTime = T;
			S.ArrivesAt = T + 0.04 + Jitter.FRandRange(0.0, 0.035);
			S.Position = FSystemCoordinate(FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0) * RadiusCentimetres / CmPerKm);
			S.Velocity = bMoving ? FVector(-FMath::Sin(Angle), FMath::Cos(Angle), 0.0) * Speed : FVector::ZeroVector;
			Out.Add(S);

			Angle += Omega * SendSeconds;
		}

		return Out;
	}

	struct FResult
	{
		double MeanBehindProjectionCm = 0.0;  // from where the newest state, projected, says they are
		double MeanFrameSpeedErrorMs = 0.0;   // against the newest state's speed
		double WorstFrameSpeedErrorMs = 0.0;
	};

	/** Runs twenty seconds, measuring after the first two; bNew picks FRemoteFollower over the old blend. */
	FResult Run(const bool bNew)
	{
		const TArray<FSample> Sent = Stream(20.0);

		FRemoteFollower Follower;
		FShipReconciliation OldRules;                    // the defaults the pawn used: blend 5 a second
		FSystemCoordinate Drawn;
		bool bDrawnSet = false;
		double NewestApplied = -1.0;
		double NewestSpeed = 0.0;

		FResult R;
		int32 Samples = 0;

		for (double Now = 0.0; Now < 20.0; Now += FrameSeconds)
		{
			const double SpeedBefore = NewestSpeed;

			// Whatever has arrived since the last frame; an older state than one already applied is
			// dropped, as replication leaves the newest value.
			for (const FSample& S : Sent)
			{
				if (S.ArrivesAt <= Now && S.ArrivesAt > Now - FrameSeconds && S.ServerTime > NewestApplied)
				{
					NewestApplied = S.ServerTime;
					NewestSpeed = S.Velocity.Size() / 100.0;
					Follower.Receive(S.Position, S.Velocity, Now, 0.002);
				}
			}

			if (!Follower.bHasState)
			{
				continue;
			}

			const FSystemCoordinate Before = Drawn;
			const FSystemCoordinate Target = Follower.Projected(Now);

			if (bNew)
			{
				Drawn = Follower.Draw(Now, FrameSeconds, 10.0);
			}
			else
			{
				Drawn = bDrawnSet ? FShipFlightModel::ReconcilePosition(Drawn, Target, OldRules, FrameSeconds) : Target;
			}

			const bool bMeasure = bDrawnSet && Now > 2.0;
			bDrawnSet = true;

			if (bMeasure)
			{
				// Against the closer of the speed before this frame's update and after it: in the frame a stop
				// arrives, the drawing rightly moved at the old speed until the stop was known. Measured against
				// the new speed alone, that frame read as 6 m/s off with nothing drawn wrong.
				const double FrameSpeed = (Drawn.Kilometres - Before.Kilometres).Size() * CmPerKm / FrameSeconds / 100.0;
				const double SpeedError = FMath::Min(FMath::Abs(FrameSpeed - NewestSpeed), FMath::Abs(FrameSpeed - SpeedBefore));

				R.MeanBehindProjectionCm += (Drawn.Kilometres - Target.Kilometres).Size() * CmPerKm;
				R.MeanFrameSpeedErrorMs += SpeedError;
				R.WorstFrameSpeedErrorMs = FMath::Max(R.WorstFrameSpeedErrorMs, SpeedError);
				++Samples;
			}
		}

		R.MeanBehindProjectionCm /= FMath::Max(Samples, 1);
		R.MeanFrameSpeedErrorMs /= FMath::Max(Samples, 1);
		return R;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMORemotePlayerDrawnWhereTheyAreTest,
	"SpaceMMO.Walk.RemotePlayerIsDrawnWhereTheyAre",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMORemotePlayerDrawnWhereTheyAreTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMORemoteFollowTests;

	const FResult Old = Run(false);
	const FResult New = Run(true);

	AddInfo(FString::Printf(TEXT("Old blend: %.1f cm behind on average, frame speed off by %.2f m/s on average, %.2f at worst."),
		Old.MeanBehindProjectionCm, Old.MeanFrameSpeedErrorMs, Old.WorstFrameSpeedErrorMs));
	AddInfo(FString::Printf(TEXT("Follower: %.1f cm behind on average, frame speed off by %.2f m/s on average, %.2f at worst."),
		New.MeanBehindProjectionCm, New.MeanFrameSpeedErrorMs, New.WorstFrameSpeedErrorMs));

	// The control: the old way trails a runner by its speed over 5, about a metre once the stops are
	// averaged in. If this fails, the test is no longer the arrangement that showed Joe a player somewhere
	// they were not.
	TestTrue(TEXT("The old blend trails the walker by most of a metre"), Old.MeanBehindProjectionCm > 70.0);

	// Drawn where the newest state says, give or take what is left of an update being eased out.
	TestTrue(TEXT("The follower stays within 5 cm of the projection on average"), New.MeanBehindProjectionCm < 5.0);

	// And never jumps: easing an update's jump costs a frame a metre or two a second. Taking it in one
	// frame costs tens -- 24 m/s at worst and 1.25 on average, measured with the follower's continuity
	// removed -- which is what the follower is for.
	TestTrue(TEXT("No frame of the follower's is more than 8 m/s off the walker's speed"), New.WorstFrameSpeedErrorMs < 8.0);
	TestTrue(TEXT("The follower's speed is within 0.6 m/s of the walker's on average"), New.MeanFrameSpeedErrorMs < 0.6);

	return true;
}

/*
 * Task 182: another player's copy animates as they move, and faces the way they do.
 *
 * Joe's two clients, 5 October: the other player "seem[s] to glide rather than show the full animations",
 * and does not "always face the correct direction". The DRAW lines said why. B ran at 6.00 m/s on his own
 * client, and A's copy of him read 0.92 m/s across the ground and 5.93 m/s vertical: the right velocity,
 * measured against an up 89 degrees wrong. The copy had begun play at the system origin before any state
 * arrived, and kept the up it found there, because only simulating works out the ground.
 *
 * So this is the pawn, not a function: the walker is the server's copy, walked by the RPC its client's
 * input arrives through, and the copy is a simulated proxy spawned as replication spawns one, handed the
 * walker's replicated state each frame. Built by hand, the inputs would have come with an up already in
 * them, and that was the whole fault.
 */
namespace SpaceMMORemoteAnimationTests
{
	/** A game world for one test, torn down however the test leaves: the settlement tests' arrangement. */
	struct FScopedWorld
	{
		UWorld* World = nullptr;

		FScopedWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("SpaceMMORemoteAnimationTest"));

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

	/** Replication reaching another client's copy: the property, written whole, as the server set it. */
	void Replicate(const ASpaceMMOCharacterPawn& From, ASpaceMMOCharacterPawn& To)
	{
		FindFProperty<FProperty>(ASpaceMMOCharacterPawn::StaticClass(), TEXT("NetState"))
			->CopyCompleteValue_InContainer(&To, &From);
	}

	/** How far apart two bodies are drawn facing, in degrees: the model's own orientation, as drawn. */
	double BodiesApartDegrees(const ASpaceMMOCharacterPawn& A, const ASpaceMMOCharacterPawn& B)
	{
		return FMath::RadiansToDegrees(
			A.GetBodyMesh()->GetComponentQuat().AngularDistance(B.GetBodyMesh()->GetComponentQuat()));
	}

	struct FLeg
	{
		const TCHAR* Name;
		FVector2D Move;
		bool bSprint;
		bool bJump;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMORemotePlayerRunsAndFacesAsTheyDoTest,
	"SpaceMMO.Walk.RemotePlayerRunsAndFacesAsTheyDo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMORemotePlayerRunsAndFacesAsTheyDoTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMORemoteAnimationTests;

	FScopedWorld Scoped;
	UWorld* const World = Scoped.World;

	// The world's own planet, which is the Capital: (60, 0, 0) km and 20 km across, as Joe's clients built it.
	const ASpaceMMOPlanetActor* Planet = nullptr;

	for (TActorIterator<ASpaceMMOPlanetActor> It(World); It; ++It)
	{
		Planet = *It;
	}

	if (!TestNotNull(TEXT("The world has its planet"), Planet))
	{
		return false;
	}

	ASpaceMMOCharacterPawn* const Walker = World->SpawnActorDeferred<ASpaceMMOCharacterPawn>(
		ASpaceMMOCharacterPawn::StaticClass(), FTransform::Identity, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

	ASpaceMMOCharacterPawn* const Copy = World->SpawnActorDeferred<ASpaceMMOCharacterPawn>(
		ASpaceMMOCharacterPawn::StaticClass(), FTransform::Identity, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

	if (!TestNotNull(TEXT("The walker spawned"), Walker) || !TestNotNull(TEXT("The copy spawned"), Copy))
	{
		return false;
	}

	Walker->FinishSpawning(FTransform::Identity);

	// A simulated proxy from BeginPlay on, with nowhere to begin but the origin -- as Joe's began: "Character
	// ready at (0.000, 0.000, 0.000) km, up V(X=-1.00, Y=-0.01, Z=-0.00)" (ClientA.log).
	Copy->SetRole(ROLE_SimulatedProxy);
	Copy->FinishSpawning(FTransform::Identity);

	// Begun by hand. A world with no game mode begins play for nobody, and BeginPlay is where Joe's copy found
	// the up it then kept: without this, the copy started from the class default instead and the test was a
	// different arrangement from the one that showed him the fault.
	Walker->DispatchBeginPlay();
	Copy->DispatchBeginPlay();

	AddInfo(FString::Printf(TEXT("The copy began play with up %s."), *Copy->GetSurfaceNormal().ToCompactString()));

	constexpr float FrameSeconds = 1.0f / 60.0f;
	FWalkInput Input;

	// One frame of a dedicated server and one client drawing the walker, in the order they happen.
	auto Frame = [&]()
	{
		World->TimeSeconds += FrameSeconds;
		Send(*Walker, Input);
		Walker->Tick(FrameSeconds);
		Replicate(*Walker, *Copy);
		Copy->Tick(FrameSeconds);
	};

	const FLeg Legs[] = {
		{ TEXT("running forward"), FVector2D(1.0, 0.0), false, false },
		{ TEXT("running right"), FVector2D(0.0, 1.0), false, false },
		{ TEXT("running back"), FVector2D(-1.0, 0.0), false, false },
		{ TEXT("sprinting forward"), FVector2D(1.0, 0.0), true, false },
		{ TEXT("jumping on the spot"), FVector2D::ZeroVector, false, true },
		{ TEXT("standing"), FVector2D::ZeroVector, false, false },
	};

	// Where Joe's two characters stood, at Borlash; and a quarter of the way round, where a respawn could put
	// somebody, so the copy's up has to follow them there rather than be right about one place.
	const FVector Centre = Planet->GetPlanetConfig().Centre.Kilometres;
	const TPair<const TCHAR*, FVector> Places[] = {
		{ TEXT("at Borlash"), FVector(60.491, -0.004, 20.158) - Centre },
		{ TEXT("a quarter of the way round"), FVector(0.0, 1.0, 0.0) },
	};

	for (const TPair<const TCHAR*, FVector>& Place : Places)
	{
		Walker->ResumeAt(FPlanetTerrain::SurfacePosition(
			Planet->GetPlanetConfig(), Planet->GetTerrainConfig(), Place.Value.GetSafeNormal()));

		// Standing a second, so the copy has caught the walker up and eased onto their facing.
		Input = FWalkInput();

		for (int32 F = 0; F < 60; ++F)
		{
			Frame();
		}

		for (const FLeg& Leg : Legs)
		{
			double WorstSpeedOff = 0.0;
			double WorstVerticalOff = 0.0;
			double WorstFacingOff = 0.0;
			double FastestWalker = 0.0;
			double HighestWalker = 0.0;

			for (int32 F = 0; F < 90; ++F)
			{
				Input.Move = Leg.Move;
				Input.bSprint = Leg.bSprint;
				Input.bJump = Leg.bJump && F == 0;

				Frame();

				FastestWalker = FMath::Max(FastestWalker, Walker->GetGroundSpeedMetresPerSecond());
				HighestWalker = FMath::Max(HighestWalker, Walker->GetVerticalSpeedMetresPerSecond());

				// From half a second in: the body takes a quarter of one to swing round at 720 degrees a second,
				// and is turning on both copies alike while it does.
				if (F >= 30)
				{
					WorstSpeedOff = FMath::Max(WorstSpeedOff,
						FMath::Abs(Copy->GetGroundSpeedMetresPerSecond() - Walker->GetGroundSpeedMetresPerSecond()));
					WorstVerticalOff = FMath::Max(WorstVerticalOff,
						FMath::Abs(Copy->GetVerticalSpeedMetresPerSecond() - Walker->GetVerticalSpeedMetresPerSecond()));
					WorstFacingOff = FMath::Max(WorstFacingOff, BodiesApartDegrees(*Walker, *Copy));
				}
			}

			const FString What = FString::Printf(TEXT("%s, %s"), Place.Key, Leg.Name);

			AddInfo(FString::Printf(
				TEXT("%s: the walker reads %.2f m/s across the ground, %.2f vertical, moving %.1f deg; the copy %.2f, %.2f, "
					"%.1f deg, up %s. Worst from half a second in: %.3f m/s, %.3f m/s, bodies %.2f deg apart."),
				*What,
				Walker->GetGroundSpeedMetresPerSecond(), Walker->GetVerticalSpeedMetresPerSecond(), Walker->GetMoveDirectionDegrees(),
				Copy->GetGroundSpeedMetresPerSecond(), Copy->GetVerticalSpeedMetresPerSecond(), Copy->GetMoveDirectionDegrees(),
				*Copy->GetSurfaceNormal().ToCompactString(),
				WorstSpeedOff, WorstVerticalOff, WorstFacingOff));

			// Or every comparison below is two people standing still agreeing about it.
			if (!Leg.Move.IsZero())
			{
				TestTrue(*FString::Printf(TEXT("%s: the walker really moves (%.2f m/s)"), *What, FastestWalker), FastestWalker > 3.0);
			}

			if (Leg.bJump)
			{
				TestTrue(*FString::Printf(TEXT("%s: the walker really leaves the ground (%.2f m/s up)"), *What, HighestWalker),
					HighestWalker > 2.0);
			}

			// The blend space's speed: 6 m/s read as 0.92 is a walk played under a run, which is the glide.
			TestTrue(*FString::Printf(TEXT("%s: the copy's ground speed is the walker's"), *What), WorstSpeedOff < 0.05);
			TestTrue(*FString::Printf(TEXT("%s: the copy's vertical speed is the walker's"), *What), WorstVerticalOff < 0.05);

			// The body as drawn, which is what Joe was looking at.
			TestTrue(*FString::Printf(TEXT("%s: the copy's body faces the way the walker's does"), *What), WorstFacingOff < 2.0);
		}
	}

	return true;
}

#endif
