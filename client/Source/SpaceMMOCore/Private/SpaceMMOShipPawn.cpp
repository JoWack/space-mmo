#include "SpaceMMOShipPawn.h"

#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/SpringArmComponent.h"
#include "Materials/MaterialInterface.h"
#include "SpaceMMOLog.h"
#include "EngineUtils.h"
#include "SpaceMMOBoarding.h"
#include "SpaceMMOCharacterPawn.h"
#include "SpaceMMOPlanetActor.h"
#include "SpaceMMOPlanetTerrain.h"
#include "SpaceMMORenderOrigin.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

ASpaceMMOShipPawn::ASpaceMMOShipPawn()
{
	PrimaryActorTick.bCanEverTick = true;

	bReplicates = true;

	// Unreal's own movement replication is deliberately off. It replicates the actor's world
	// transform, and world transforms are not comparable between clients here: each one rebases
	// its render origin independently, so the same system coordinate becomes a different world
	// location on every machine. Position is replicated in system space instead, by NetState.
	SetReplicateMovement(false);

	ShipRoot = CreateDefaultSubobject<USceneComponent>(TEXT("ShipRoot"));
	SetRootComponent(ShipRoot);

	Hull = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Hull"));
	Hull->SetupAttachment(ShipRoot);

	// No collision: the ship's position is owned by the flight model and the grid, not by Chaos.
	// Leaving collision on would let the physics solver fight the authoritative position and win
	// intermittently, which is a miserable class of bug to chase.
	Hull->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Engine content so the ship is visible without any authored asset. A placeholder until there
	// is a real hull, but an invisible ship makes every flight change impossible to evaluate.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> HullMesh(
		TEXT("/Engine/BasicShapes/Cone.Cone"));

	if (HullMesh.Succeeded())
	{
		Hull->SetStaticMesh(HullMesh.Object);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> HullMaterial(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	if (HullMaterial.Succeeded())
	{
		Hull->SetMaterial(0, HullMaterial.Object);
	}

	// The cone points up by default; rotate it to point along +X, which is the ship's forward axis
	// and the direction thrust is applied in.
	Hull->SetRelativeRotation(FRotator(-90.0, 0.0, 0.0));
	Hull->SetRelativeScale3D(FVector(1.0, 1.0, 2.0));

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(ShipRoot);
	CameraBoom->TargetArmLength = 1200.0f;
	CameraBoom->bDoCollisionTest = false;

	// The boom follows the ship's own orientation rather than the controller's, because in six
	// degrees of freedom there is no meaningful "up" to keep a camera level against.
	CameraBoom->bUsePawnControlRotation = false;
	CameraBoom->bInheritPitch = true;
	CameraBoom->bInheritYaw = true;
	CameraBoom->bInheritRoll = true;

	ThirdPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("ThirdPersonCamera"));
	ThirdPersonCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);

	FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCamera->SetupAttachment(ShipRoot);
	FirstPersonCamera->SetRelativeLocation(FVector(200.0, 0.0, 60.0));
	FirstPersonCamera->SetActive(false);

	// Deliberately NOT AutoPossessPlayer = Player0.
	//
	// That setting is for a pawn placed in a level in single player, and it is actively destructive
	// on a dedicated server. The engine's guard is GetNetMode() != NM_Client, which a dedicated
	// server passes — and "player 0" there resolves to the FIRST CONNECTED PLAYER's controller. So
	// every ship spawned for a joining player seized the controller of whoever joined first,
	// unpossessing their ship out from under them.
	//
	// The symptom was that the first client froze the instant a second one connected: its flight HUD
	// vanished and it ignored input, while the window still rendered and answered Windows. It looked
	// for all the world like an input or focus fault, and three plausible guesses at that were wrong.
	// A tick heartbeat found it in one run — the ship was still ticking and the world was not paused,
	// but "locally controlled" had flipped to 0, eleven milliseconds before the server logged the
	// second player's ship being spawned.
	//
	// Possession belongs to the game mode, which does it per connection through DefaultPawnClass and
	// RestartPlayer. That already worked in single player; this line was redundant there and wrong
	// everywhere else.
}

void ASpaceMMOShipPawn::BeginPlay()
{
	Super::BeginPlay();

	Navigation = FShipNavigation();
	Navigation.SystemPosition = FSystemCoordinate(StartingSystemPositionKilometres);

	// Dev affordance: start somewhere specific without editing content or flying there.
	//   -ShipStartX=175 -ShipStartY=0 -ShipStartZ=0
	// Useful for anything that only happens near a planet, where the alternative is a two-minute
	// flight before the thing under test even begins.
	//
	// Three scalars rather than one comma-separated vector: FParse::Value treats a comma as a
	// delimiter and returns only the first component, which fails silently and looks exactly like
	// the flag being ignored.
	double StartX = 0.0;
	double StartY = 0.0;
	double StartZ = 0.0;

	if (FParse::Value(FCommandLine::Get(), TEXT("ShipStartX="), StartX)
		| FParse::Value(FCommandLine::Get(), TEXT("ShipStartY="), StartY)
		| FParse::Value(FCommandLine::Get(), TEXT("ShipStartZ="), StartZ))
	{
		// Single pipe, not double: every component must be parsed, and short-circuiting would skip
		// Y and Z whenever X was present.
		Navigation.SystemPosition = FSystemCoordinate(FVector(StartX, StartY, StartZ));

		UE_LOG(LogSpaceMMO, Log, TEXT("Ship start overridden to %s"),
			*Navigation.SystemPosition.ToString());
	}

	// Start with the ship at the render origin, so it begins exactly where physics behaves best.
	Navigation.RenderOrigin = Navigation.SystemPosition;

	FlightState = FShipFlightState();
	FlightState.Rotation = GetActorQuat();

	// Dev affordance: an initial velocity, so a headless run can actually fly somewhere without a
	// hand on the stick. -ShipVelX=200000 is 2 km/s.
	double VelX = 0.0;
	double VelY = 0.0;
	double VelZ = 0.0;

	if (FParse::Value(FCommandLine::Get(), TEXT("ShipVelX="), VelX)
		| FParse::Value(FCommandLine::Get(), TEXT("ShipVelY="), VelY)
		| FParse::Value(FCommandLine::Get(), TEXT("ShipVelZ="), VelZ))
	{
		FlightState.Velocity = FVector(VelX, VelY, VelZ);
	}

	// Flight assist bleeds off any velocity the moment the stick is released, so an injected
	// velocity coasts about five kilometres and stops. -NoFlightAssist turns it off, which is what
	// makes an unattended run able to cross a system.
	if (FParse::Param(FCommandLine::Get(), TEXT("NoFlightAssist")))
	{
		FlightConfig.LinearDamping = 0.0;
	}

	PublishRenderOrigin();
	ApplyWorldTransform();

	UE_LOG(LogSpaceMMO, Log, TEXT("Ship ready at %s"), *Navigation.SystemPosition.ToString());
}

void ASpaceMMOShipPawn::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Three roles, and they do genuinely different things.
	//
	//   Server            integrates the authoritative simulation and publishes it.
	//   Owning client     predicts locally so flying feels immediate, sends intent, and is pulled
	//                     back toward the server whenever the two disagree.
	//   Everyone else's   is not simulated at all — it is drawn from what the server last said.
	//
	// A listen server's own ship is both authority and locally controlled, and takes the first
	// branch: it is already the truth, so there is nothing to predict or reconcile against.
	if (HasAuthority())
	{
		SimulateStep(DeltaSeconds);
		PublishNetState();
	}
	else if (IsLocallyControlled())
	{
		SimulateStep(DeltaSeconds);
		ServerSendInput(PendingInput);
		ReconcileWithServer(DeltaSeconds);
	}
	else
	{
		FollowServerState(DeltaSeconds);
	}

	// Only the ship you are flying moves the render origin. Letting every ship publish would have
	// them fighting over the one origin the whole client renders through, and the world would jump
	// each time a different ship ticked.
	if (IsLocallyControlled())
	{
		PublishRenderOrigin();
	}

	ApplyWorldTransform();

	// A heartbeat, and the two facts that distinguish the ways it can stop.
	//
	// When a second client joins, the first client's ship stops simulating: the flight HUD vanishes
	// and input does nothing, while the process still renders and answers Windows. Measured with the
	// per-second APPROACH line, which simply stops. That leaves two candidates and they need telling
	// apart, because the fix is completely different for each:
	//
	//   the world is paused        DeltaSeconds keeps arriving, IsPaused() is true
	//   the actor stopped ticking  no heartbeat at all, because Tick is not being called
	//
	// Forcing bPauseOnLossOfFocus=False on the command line did not help, so if IsPaused() is true
	// something else is doing the pausing and this will say so.
	if (FParse::Param(FCommandLine::Get(), TEXT("LogTickHealth")))
	{
		HeartbeatSeconds += DeltaSeconds;

		if (HeartbeatSeconds >= 1.0)
		{
			HeartbeatSeconds = 0.0;

			const UWorld* World = GetWorld();

			UE_LOG(LogSpaceMMO, Log,
				TEXT("TICKHEALTH: delta %.4f s | paused %d | locally controlled %d | authority %d"),
				DeltaSeconds,
				World != nullptr ? World->IsPaused() : -1,
				IsLocallyControlled() ? 1 : 0,
				HasAuthority() ? 1 : 0);
		}
	}

	DiagnosticSeconds += DeltaSeconds;

	if (FParse::Param(FCommandLine::Get(), TEXT("LogApproach")) && DiagnosticSeconds >= 1.0)
	{
		DiagnosticSeconds = 0.0;

		FVector PlanetWorld = FVector::ZeroVector;
		double PlanetSystemDistance = 0.0;

		for (TActorIterator<ASpaceMMOPlanetActor> It(GetWorld()); It; ++It)
		{
			PlanetWorld = It->GetActorLocation();
			PlanetSystemDistance =
				(It->GetPlanetConfig().Centre.Kilometres - Navigation.SystemPosition.Kilometres).Size();

			break;
		}

		// If the planet is stationary, the system distance must fall steadily while the drawn
		// distance tracks it. A drawn distance that grows, or refuses to shrink, is the bug.
		UE_LOG(LogSpaceMMO, Log,
			TEXT("APPROACH: ship sys %.1f km | true gap %.2f km | drawn gap %.2f km | rebases %d"),
			Navigation.SystemPosition.Kilometres.X,
			PlanetSystemDistance,
			(PlanetWorld - GetActorLocation()).Size() / 100000.0,
			Navigation.RebaseCount);
	}

	if (Navigation.RebaseCount != LastLoggedRebaseCount)
	{
		LastLoggedRebaseCount = Navigation.RebaseCount;

		FVector PlanetWorld = FVector::ZeroVector;

		for (TActorIterator<ASpaceMMOPlanetActor> It(GetWorld()); It; ++It)
		{
			PlanetWorld = It->GetActorLocation();

			break;
		}

		UE_LOG(LogSpaceMMO, Log,
			TEXT("REBASE %d: ship sys %s world %s | planet world %s"),
			Navigation.RebaseCount,
			*Navigation.SystemPosition.ToString(),
			*GetActorLocation().ToCompactString(),
			*PlanetWorld.ToCompactString());
	}

	// Classified after moving, and fed its own previous value so the hysteresis in
	// ClassifyProximity has something to work against.
	for (TActorIterator<ASpaceMMOPlanetActor> It(GetWorld()); It; ++It)
	{
		// Height above the ground, not above the nominal sphere. A ship parked on three hundred
		// metres of terrain is landed, and measuring against the sphere called it airborne.
		GroundAltitudeKilometres = FPlanetTerrain::AltitudeAboveGroundKilometres(
			It->GetPlanetConfig(), It->GetTerrainConfig(), Navigation.SystemPosition);

		Proximity = FPlanetPhysics::ClassifyProximityAtAltitude(
			It->GetPlanetConfig(), GroundAltitudeKilometres, Proximity);

		break;
	}

	// Axes are cleared each frame because the legacy input path calls the handlers only while a
	// key is held. Without this a tapped key would stay applied forever.
	PendingInput.Thrust = FVector::ZeroVector;
	PendingInput.Torque = FVector::ZeroVector;

	if (bShowFlightDebug && GEngine != nullptr)
	{
		GEngine->AddOnScreenDebugMessage(
			1, 0.0f, FColor::Cyan,
			FString::Printf(
				TEXT("System %s | %.3f km/s | rebases %d"),
				*Navigation.SystemPosition.ToString(),
				GetSpeedKilometresPerSecond(),
				Navigation.RebaseCount));

		const TCHAR* ProximityName =
			Proximity == EPlanetProximity::Surface ? TEXT("SURFACE")
			: Proximity == EPlanetProximity::Atmospheric ? TEXT("ATMOSPHERE")
			: TEXT("ORBIT");

		// Both altitudes, because they disagree by however tall the ground is and the label keys
		// off the second one. Showing only the first is what made "Altitude 0.34 km | ATMOSPHERE"
		// look like a contradiction while both halves were telling the truth.
		GEngine->AddOnScreenDebugMessage(
			3, 0.0f, FColor::Yellow,
			FString::Printf(
				TEXT("Altitude %.2f km sphere / %.2f km ground | %s"),
				GetAltitudeKilometres(),
				GroundAltitudeKilometres,
				ProximityName));

		GEngine->AddOnScreenDebugMessage(
			2, 0.0f, FColor::Silver,
			FString::Printf(TEXT("World %s"), *GetActorLocation().ToCompactString()));
	}
}

double ASpaceMMOShipPawn::GetAltitudeKilometres() const
{
    if (const UWorld* World = GetWorld())
    {
        for (TActorIterator<ASpaceMMOPlanetActor> It(const_cast<UWorld*>(World)); It; ++It)
        {
            return FPlanetPhysics::AltitudeKilometres(
                It->GetPlanetConfig(), Navigation.SystemPosition);
        }
    }

    return 0.0;
}

FVector ASpaceMMOShipPawn::ComputeGravity() const
{
	UWorld* World = GetWorld();

	if (World == nullptr)
	{
		return FVector::ZeroVector;
	}

	// Summed rather than nearest-only, so a ship between two bodies is pulled by both. With one
	// planet it makes no difference; with a planet and its moon it is the difference between
	// orbital mechanics working and not.
	FVector Total = FVector::ZeroVector;

	for (TActorIterator<ASpaceMMOPlanetActor> It(World); It; ++It)
	{
		const FPlanetConfig& Planet = It->GetPlanetConfig();

		Total += FPlanetPhysics::GravityAcceleration(Planet, Navigation.SystemPosition);

		// Air resistance belongs here rather than inside the flight model, because Step already
		// separates pilot intent from the world acting on the ship, and drag is unambiguously the
		// world. It also means flight assist keeps damping only what the pilot is doing.
		//
		// Measured against the ground rather than the sphere: air sits on the terrain, and a ship
		// in a valley is deeper in it than one over a mountain of the same radius.
		const double AltitudeKilometres = FPlanetTerrain::AltitudeAboveGroundKilometres(
			Planet, It->GetTerrainConfig(), Navigation.SystemPosition);

		Total += FPlanetPhysics::AtmosphericDrag(
			Planet,
			AltitudeKilometres,
			FlightState.Velocity,
			FlightConfig.ThrustAcceleration,
			FlightConfig.AtmosphericTerminalSpeed);
	}

	return Total;
}

void ASpaceMMOShipPawn::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ASpaceMMOShipPawn, NetState);
}

void ASpaceMMOShipPawn::SimulateStep(const double DeltaSeconds)
{
	FlightState = FShipFlightModel::Step(
		FlightState, PendingInput, FlightConfig, DeltaSeconds, ComputeGravity());

	Navigation = FShipFlightModel::Advance(Navigation, FlightState, DeltaSeconds);

	// After moving, not before. Resolving first would let the very step that drives the ship into
	// the ground happen unopposed, so it would sink one frame's worth every frame.
	ResolveGroundContact();
}

void ASpaceMMOShipPawn::ResolveGroundContact()
{
	UWorld* World = GetWorld();

	if (World == nullptr)
	{
		return;
	}

	const bool bWasOnGround = bOnGround;

	bOnGround = false;

	for (TActorIterator<ASpaceMMOPlanetActor> It(World); It; ++It)
	{
		const FGroundContact Contact = FPlanetTerrain::ResolveContact(
			It->GetPlanetConfig(),
			It->GetTerrainConfig(),
			Navigation.SystemPosition,
			FlightState.Velocity,
			HullRadiusKilometres,
			FPlanetTerrain::DefaultContactToleranceKilometres,
			bWasOnGround);

		if (!Contact.bOnGround)
		{
			continue;
		}

		bOnGround = true;

		Navigation.SystemPosition = Contact.Position;
		FlightState.Velocity = Contact.Velocity;

		// Friction, but only when the pilot is not driving. Damping a held input would stop a ship
		// taxiing or lifting off, and the whole point is that a parked ship stays parked.
		if (PendingInput.Thrust.IsNearlyZero() && FlightConfig.GroundFriction > 0.0)
		{
			const FVector Into =
				Contact.SurfaceNormal * FVector::DotProduct(FlightState.Velocity, Contact.SurfaceNormal);

			const FVector Along = FlightState.Velocity - Into;

			FlightState.Velocity =
				Into + (Along * FMath::Exp(-FlightConfig.GroundFriction * GetWorld()->GetDeltaSeconds()));
		}

		if (Contact.ImpactSpeed > 50000.0)
		{
			// Half a kilometre per second into the ground. Nothing happens to the ship yet — the
			// death and insurance rules exist server-side and are not wired to flight — but a
			// landing this hard is worth seeing in a log while tuning.
			UE_LOG(LogSpaceMMO, Warning,
				TEXT("Hard contact at %.1f km/s."), Contact.ImpactSpeed / 100000.0);
		}
	}

	// Logged on the transition rather than every frame, because touching down is the event and
	// resting on the ground is not.
	if (bOnGround != bWasOnGround)
	{
		// Speed, and what it would take to orbit, because those two numbers decide whether this
		// line is a bug or physics. On a twenty-kilometre planet orbital velocity is only about
		// 443 m/s, so a ship skimming at 800 is thrown off the ground by its own speed and contact
		// is genuinely intermittent. Without the comparison, honest skipping and a broken threshold
		// produce the same log and the wrong one gets fixed.
		const double SpeedMetresPerSecond = FlightState.Velocity.Size() / 100.0;

		double OrbitalMetresPerSecond = 0.0;

		for (TActorIterator<ASpaceMMOPlanetActor> It(World); It; ++It)
		{
			const FPlanetConfig& Planet = It->GetPlanetConfig();

			OrbitalMetresPerSecond = FMath::Sqrt(
				(Planet.SurfaceGravity / 100.0) * (Planet.RadiusKilometres * 1000.0));

			break;
		}

		UE_LOG(LogSpaceMMO, Log, TEXT("%s at %s, %.0f m/s (orbital %.0f m/s)"),
			bOnGround ? TEXT("Touched down") : TEXT("Lifted off"),
			*Navigation.SystemPosition.ToString(),
			SpeedMetresPerSecond,
			OrbitalMetresPerSecond);

		// Dev affordance: -AutoDisembark steps out the moment the ship settles, so the whole
		// descend-land-disembark sequence can be checked without a human holding a key.
		if (bOnGround && !bAutoDisembarked
			&& FParse::Param(FCommandLine::Get(), TEXT("AutoDisembark")))
		{
			bAutoDisembarked = true;

			RequestDisembark();
		}
	}
}

void ASpaceMMOShipPawn::PublishNetState()
{
	NetState.SystemPosition = Navigation.SystemPosition;
	NetState.Velocity = FlightState.Velocity;
	NetState.Rotation = FlightState.Rotation;
	NetState.AngularVelocity = FlightState.AngularVelocity;

	const UWorld* World = GetWorld();

	NetState.ServerTimeSeconds = World != nullptr ? World->GetTimeSeconds() : 0.0;
}

bool ASpaceMMOShipPawn::ServerSendInput_Validate(FShipFlightInput Input)
{
	// Nothing to reject. Sanitised() clamps every axis into range, so a client sending a hundred
	// on the thrust axis flies exactly as fast as one sending one — there is no value here that
	// can be hostile, only values that get clamped. Rejecting the packet outright would punish
	// ordinary float noise for no gain.
	return true;
}

void ASpaceMMOShipPawn::ServerSendInput_Implementation(FShipFlightInput Input)
{
	// Sanitised on arrival, not on send. What the client chose to send is a claim; this is where
	// it stops being one.
	PendingInput = Input.Sanitised();
}

void ASpaceMMOShipPawn::ReconcileWithServer(const double DeltaSeconds)
{
	// Nothing new to reconcile against. Applying the same state twice would drag the prediction
	// backwards every frame between updates, which is exactly the rubber-banding this is meant to
	// avoid.
	if (NetState.ServerTimeSeconds <= LastAppliedServerTime)
	{
		return;
	}

	LastAppliedServerTime = NetState.ServerTimeSeconds;

	const FSystemCoordinate Corrected = FShipFlightModel::ReconcilePosition(
		Navigation.SystemPosition, NetState.SystemPosition, Reconciliation, DeltaSeconds);

	LastCorrectionKilometres =
		(Corrected.Kilometres - Navigation.SystemPosition.Kilometres).Size();

	Navigation.SystemPosition = Corrected;

	// Velocity is taken from the server outright rather than blended. It is not drawn, so easing
	// it buys nothing visible, and a stale velocity is what makes the next frame's prediction
	// wrong again — correcting position while leaving the cause in place guarantees a correction
	// every frame.
	FlightState.Velocity = NetState.Velocity;
}

void ASpaceMMOShipPawn::FollowServerState(const double DeltaSeconds)
{
	const UWorld* World = GetWorld();
	const double Now = World != nullptr ? World->GetTimeSeconds() : 0.0;

	if (NetState.ServerTimeSeconds > LastAppliedServerTime)
	{
		LastAppliedServerTime = NetState.ServerTimeSeconds;
		LastNetStateReceivedAt = Now;
	}

	// Carried forward along its last known velocity, because replication arrives far slower than
	// the frame rate and a remote ship pinned to its last received position visibly stutters.
	const FSystemCoordinate Target = FShipFlightModel::Extrapolate(
		NetState.SystemPosition, NetState.Velocity, Now - LastNetStateReceivedAt);

	// Eased toward rather than snapped to, so each arriving update does not produce a visible step.
	Navigation.SystemPosition = FShipFlightModel::ReconcilePosition(
		Navigation.SystemPosition, Target, Reconciliation, DeltaSeconds);

	// A remote ship is drawn, not simulated: its orientation and velocity are whatever the server
	// said, and nothing here integrates them.
	FlightState.Rotation = NetState.Rotation;
	FlightState.Velocity = NetState.Velocity;
}

void ASpaceMMOShipPawn::PublishRenderOrigin()
{
	// The piloted ship owns the render origin: it is what the camera is attached to, so it is the
	// thing that must stay near Unreal's origin for physics and rendering to behave.
	if (UWorld* World = GetWorld())
	{
		if (USpaceMMORenderOriginSubsystem* Origin =
			World->GetSubsystem<USpaceMMORenderOriginSubsystem>())
		{
			Origin->SetRenderOrigin(Navigation.RenderOrigin);
		}
	}
}

void ASpaceMMOShipPawn::ApplyWorldTransform()
{
	// Drawn against the subsystem's origin rather than this ship's own.
	//
	// For the ship being flown here the two agree — it published the origin a moment ago. For
	// every other ship they do not: each carries its own Navigation.RenderOrigin, and resolving
	// against that would place remote ships in a frame of reference this client is not rendering
	// in. They would be drawn at plausible-looking coordinates nowhere near where they are.
	const UWorld* World = GetWorld();

	const USpaceMMORenderOriginSubsystem* Origin =
		World != nullptr ? World->GetSubsystem<USpaceMMORenderOriginSubsystem>() : nullptr;

	const FVector Location = Origin != nullptr
		? Origin->ToWorldLocation(Navigation.SystemPosition)
		: Navigation.RenderLocationCentimetres();

	SetActorLocationAndRotation(Location, FlightState.Rotation);
}

void ASpaceMMOShipPawn::SetSystemPosition(const FSystemCoordinate& NewPosition)
{
	Navigation.SystemPosition = NewPosition;
	Navigation.RenderOrigin = NewPosition;
	++Navigation.RebaseCount;

	PublishRenderOrigin();
	ApplyWorldTransform();
}

void ASpaceMMOShipPawn::RequestDisembark()
{
	ServerDisembark();
}

void ASpaceMMOShipPawn::ServerDisembark_Implementation()
{
	// Checked here rather than on the client, because this is where it counts.
	if (!FBoarding::CanDisembark(bOnGround))
	{
		UE_LOG(LogSpaceMMO, Log, TEXT("Cannot step out: the ship is not on the ground."));

		return;
	}

	AController* OwningController = GetController();
	UWorld* World = GetWorld();

	if (OwningController == nullptr || World == nullptr)
	{
		return;
	}

	// Up is whichever planet the ship is resting on. Without it the character would step out along
	// an arbitrary axis and end up inside the ground or hanging above it.
	FVector Up = FVector::UpVector;

	for (TActorIterator<ASpaceMMOPlanetActor> It(World); It; ++It)
	{
		const FGroundContact Contact = FPlanetTerrain::ResolveContact(
			It->GetPlanetConfig(),
			It->GetTerrainConfig(),
			Navigation.SystemPosition,
			FVector::ZeroVector,
			HullRadiusKilometres);

		if (Contact.bOnGround)
		{
			Up = Contact.SurfaceNormal;

			break;
		}
	}

	const FSystemCoordinate StepOut = FBoarding::StepOutPosition(
		Navigation.SystemPosition, Up, FlightState.Rotation.GetRightVector());

	// Assigned in two statements rather than one conditional: TSubclassOf and UClass* both convert
	// to several common types, so the ternary is ambiguous.
	TSubclassOf<ASpaceMMOCharacterPawn> SpawnClass = CharacterClass;

	if (SpawnClass == nullptr)
	{
		SpawnClass = ASpaceMMOCharacterPawn::StaticClass();
	}

	ASpaceMMOCharacterPawn* Character = World->SpawnActorDeferred<ASpaceMMOCharacterPawn>(
		SpawnClass, FTransform::Identity, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

	if (Character == nullptr)
	{
		return;
	}

	// Before FinishSpawning. BeginPlay resolves the ground and aligns to it, so a position applied
	// afterwards is a frame too late.
	Character->SetStartingSystemPosition(StepOut.Kilometres);
	Character->FinishSpawning(FTransform::Identity);

	// The ship stays exactly where it is, unpossessed, waiting to be climbed back into.
	OwningController->Possess(Character);

	UE_LOG(LogSpaceMMO, Log, TEXT("Stepped out of the ship at %s"), *StepOut.ToString());
}

void ASpaceMMOShipPawn::ToggleCameraView()
{
	bFirstPerson = !bFirstPerson;

	ThirdPersonCamera->SetActive(!bFirstPerson);
	FirstPersonCamera->SetActive(bFirstPerson);
}

void ASpaceMMOShipPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (PlayerInputComponent == nullptr)
	{
		return;
	}

	// Axis and action names match Config/DefaultInput.ini.
	PlayerInputComponent->BindAxis(TEXT("ShipThrustForward"), this, &ASpaceMMOShipPawn::ThrustForward);
	PlayerInputComponent->BindAxis(TEXT("ShipThrustRight"), this, &ASpaceMMOShipPawn::ThrustRight);
	PlayerInputComponent->BindAxis(TEXT("ShipThrustUp"), this, &ASpaceMMOShipPawn::ThrustUp);
	PlayerInputComponent->BindAxis(TEXT("ShipPitch"), this, &ASpaceMMOShipPawn::Pitch);
	PlayerInputComponent->BindAxis(TEXT("ShipYaw"), this, &ASpaceMMOShipPawn::Yaw);
	PlayerInputComponent->BindAxis(TEXT("ShipRoll"), this, &ASpaceMMOShipPawn::Roll);

	PlayerInputComponent->BindAction(
		TEXT("ShipBoost"), IE_Pressed, this, &ASpaceMMOShipPawn::StartBoost);
	PlayerInputComponent->BindAction(
		TEXT("ShipBoost"), IE_Released, this, &ASpaceMMOShipPawn::StopBoost);
	PlayerInputComponent->BindAction(
		TEXT("ToggleCamera"), IE_Pressed, this, &ASpaceMMOShipPawn::ToggleCameraView);

	PlayerInputComponent->BindAction(
		TEXT("Board"), IE_Pressed, this, &ASpaceMMOShipPawn::RequestDisembark);
}

void ASpaceMMOShipPawn::ThrustForward(const float Value) { PendingInput.Thrust.X = Value; }

void ASpaceMMOShipPawn::ThrustRight(const float Value) { PendingInput.Thrust.Y = Value; }

void ASpaceMMOShipPawn::ThrustUp(const float Value) { PendingInput.Thrust.Z = Value; }

void ASpaceMMOShipPawn::Roll(const float Value) { PendingInput.Torque.X = Value; }

void ASpaceMMOShipPawn::Pitch(const float Value) { PendingInput.Torque.Y = Value; }

void ASpaceMMOShipPawn::Yaw(const float Value) { PendingInput.Torque.Z = Value; }

void ASpaceMMOShipPawn::StartBoost() { PendingInput.bBoost = true; }

void ASpaceMMOShipPawn::StopBoost() { PendingInput.bBoost = false; }
