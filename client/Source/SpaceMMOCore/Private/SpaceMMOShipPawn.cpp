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
#include "SpaceMMOSolidity.h"
#include "SpaceMMOViewSubsystem.h"
#include "SpaceMMOSurfaces.h"
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
	// Solid to a walking character, and to nothing else (ADR-0013).
	//
	// Query-only, and deliberately not simulated: where a ship is comes from the flight model and
	// the server, and a physics body would be a second opinion about that -- the same reason the
	// planet has no collision body and contact is a function instead.
	//
	// The ship still passes through the ground by its own hull radius rather than by this, because
	// the terrain has no collision geometry for anything to sweep against.
	Hull->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Hull->SetCollisionObjectType(ECC_WorldDynamic);
	Hull->SetCollisionResponseToAllChannels(ECR_Ignore);
	Hull->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);

	// Engine content so the ship is visible without any authored asset. A placeholder until there
	// is a real hull, but an invisible ship makes every flight change impossible to evaluate.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaceholderHull(
		TEXT("/Engine/BasicShapes/Cone.Cone"));

	if (PlaceholderHull.Succeeded())
	{
		Hull->SetStaticMesh(PlaceholderHull.Object);
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
	ApplyHullMesh();

	if (const UGameInstance* const Game = GetGameInstance())
	{
		if (const USpaceMMOViewSubsystem* const Remembered =
			Game->GetSubsystem<USpaceMMOViewSubsystem>())
		{
			View.ArmTargetCentimetres = Remembered->ShipArmCentimetres;
		}
	}

	if (CameraBoom != nullptr && View.ArmTargetCentimetres > 0.0)
	{
		CameraBoom->TargetArmLength = static_cast<float>(View.ArmTargetCentimetres);
	}

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

	// Drawn, never simulated. Before the axes are cleared, because the orbit reads what the mouse
	// did this frame and the clear below is what ends that frame's input.
	if (IsLocallyControlled())
	{
		ApplyView(DeltaSeconds);
	}

	// Axes are cleared each frame because the legacy input path calls the handlers only while a
	// key is held. Without this a tapped key would stay applied forever.
	PendingInput.Thrust = FVector::ZeroVector;
	PendingInput.Torque = FVector::ZeroVector;

	// The three on-screen readouts that used to live here are gone. USpaceMMOFlightReadout says all
	// of it now, in a widget, where the lines appear in the order they are written -- these were
	// keyed 1, 3 and 2 and drew as 2, 3, 1, because debug messages are ordered by slot rather than
	// by key and a zero display time has the engine delete and re-add them every frame.
	//
	// bShowFlightDebug survives and still means the same thing: it now governs the readout's debug
	// line rather than three messages of its own.
}

double ASpaceMMOShipPawn::GetOrbitalSpeedHere() const
{
    // The same nearest-planet rule GetAltitudeKilometres uses, so the two answers are always about
    // the same body rather than quietly about different ones.
    if (const UWorld* World = GetWorld())
    {
        for (TActorIterator<ASpaceMMOPlanetActor> It(const_cast<UWorld*>(World)); It; ++It)
        {
            return FPlanetPhysics::CircularOrbitSpeed(
                It->GetPlanetConfig(),
                FPlanetPhysics::AltitudeKilometres(
                    It->GetPlanetConfig(), Navigation.SystemPosition));
        }
    }

    return 0.0;
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

	const FSystemCoordinate Before = Navigation.SystemPosition;

	Navigation = FShipFlightModel::Advance(Navigation, FlightState, DeltaSeconds);

	// Anything solid in the way, before the ground gets a say. A ship stopped by a wall has not
	// moved, so resolving the ground at the position it was prevented from reaching would settle it
	// onto the wrong piece of terrain for a frame.
	ResolveBlocking(Before);

	// After moving, not before. Resolving first would let the very step that drives the ship into
	// the ground happen unopposed, so it would sink one frame's worth every frame.
	ResolveGroundContact();
}

void ASpaceMMOShipPawn::UnPossessed()
{
	Super::UnPossessed();

	// <strong>A ship nobody is flying must not keep flying.</strong> PendingInput is whatever was
	// last held, and nothing cleared it, so a ship left with thrust on its stick kept thrusting
	// after its pilot stepped out -- which is one candidate for task 135 and a fault on its own
	// terms whether or not it turns out to be that one. Every route out of a ship comes through
	// here, including ones nobody has written yet.
	const bool bWasDriving = !PendingInput.Thrust.IsNearlyZero()
		|| !PendingInput.Torque.IsNearlyZero()
		|| PendingInput.bBoost;

	PendingInput = FShipFlightInput();

	if (bWasDriving)
	{
		UE_LOG(LogSpaceMMO, Log,
			TEXT("Ship left with input still held; the stick is now centred."));
	}
}

void ASpaceMMOShipPawn::ApplyHullMesh()
{
	if (Hull == nullptr)
	{
		return;
	}

	// Said on every path, including the one that does nothing. An unset hull and code that never ran
	// produce the same evidence -- a cone -- and only one of them is somebody's mistake.
	UE_LOG(LogSpaceMMO, Log,
		TEXT("Ship hull configured as '%s'."),
		HullMesh.IsNull() ? TEXT("<unset>") : *HullMesh.ToString());

	if (HullMesh.IsNull())
	{
		return;
	}

	UStaticMesh* const Mesh = Cast<UStaticMesh>(HullMesh.TryLoad());

	// Named-but-wrong is worth saying out loud: from the outside a typo and an unset path look
	// identical, and one of them is a mistake somebody wants telling about.
	if (Mesh == nullptr)
	{
		UE_LOG(LogSpaceMMO, Warning,
			TEXT("Ship hull '%s' did not load; the placeholder cone stays."),
			*HullMesh.ToString());

		return;
	}

	// Measured off the mesh, not assumed, and along whichever axis it is longest on. A ship is
	// longer than it is wide or tall, so the longest axis is its length whatever orientation it was
	// authored in -- which means the fit does not depend on the rotation below being right first.
	const FBoxSphereBounds MeshBounds = Mesh->GetBounds();

	const FVector Extent = MeshBounds.BoxExtent;

	const double AuthoredLength = FMath::Max3(Extent.X, Extent.Y, Extent.Z) * 2.0;

	const double TargetCentimetres = HullLengthMetres * 100.0;

	const double Scale = AuthoredLength > UE_DOUBLE_SMALL_NUMBER && TargetCentimetres > 0.0
		? TargetCentimetres / AuthoredLength
		: 1.0;

	Hull->SetStaticMesh(Mesh);

	// The mesh brings its own materials. The placeholder material the constructor set is for the
	// engine cone, and leaving it on would repaint an authored ship in flat grey.
	Hull->EmptyOverrideMaterials();

	// Centred on its own geometry, whatever the asset says its pivot is.
	//
	// <strong>An exporter leaves an object where it was in its scene, and an import can bake that
	// in.</strong> This hull arrived 77 m from its own origin that way, and since the camera boom
	// and the collision sphere both hang off the pawn, the ship was drawn somewhere nobody was
	// looking. Three rounds of import settings went into trying to make the asset agree with the
	// code; measuring the pivot and subtracting it is one line and works for every hull, including
	// ones nobody has exported yet.
	//
	// Rotated, because the bounds origin is in the mesh's own frame and the offset is applied in the
	// parent's: a point at Origin in mesh space ends up at Rotation * (Origin * Scale) once the
	// component has had its say.
	const FVector Centre = HullMeshRotation.RotateVector(MeshBounds.Origin * Scale);

	Hull->SetRelativeRotation(HullMeshRotation);
	Hull->SetRelativeLocation(HullMeshOffset - Centre);
	Hull->SetRelativeScale3D(FVector(Scale));

	// A hull with no simple collision is one a character walks straight through, and a parked ship
	// you can walk through looks exactly like the boarding prompt being broken.
	SpaceMMOSolidity::ReportIfIntangible(Mesh, TEXT("Ship"), GetName());

	UE_LOG(LogSpaceMMO, Log,
		TEXT("Ship drawing as '%s': authored %.1f cm on its longest axis, scaled %.3f to %.1f m; "
			"rotated %s, offset %s. Collision is still a %.1f m sphere."),
		*Mesh->GetName(),
		AuthoredLength,
		Scale,
		HullLengthMetres,
		*HullMeshRotation.ToCompactString(),
		*HullMeshOffset.ToCompactString(),
		HullRadiusKilometres * 1000.0);

	// The pivot, and which way round the hull is, because neither is visible from a length.
	//
	// <strong>A mesh drawn a long way from its own origin is a ship that is not where the ship
	// is.</strong> The camera boom hangs off the pawn, so a hull sitting eighty metres from the
	// pivot is simply not in frame -- which looks like the ship failing to draw rather than like a
	// number in an import setting. The deposit actor prints this for the same reason and it settled
	// a floating-building round in one run.
	//
	// The longest axis is named too: this is scaled along whichever axis is longest, but +X is
	// forward, and a hull whose length runs along Y is one that flies sideways until
	// HullMeshRotation says otherwise.
	UE_LOG(LogSpaceMMO, Log,
		TEXT("  hull bounds: extent %s cm, pivot %s cm off centre (%.1f cm once scaled, and "
			"subtracted); longest axis is %s."),
		*Extent.ToCompactString(),
		*MeshBounds.Origin.ToCompactString(),
		MeshBounds.Origin.Size() * Scale,
		Extent.X >= Extent.Y && Extent.X >= Extent.Z
			? TEXT("X, which is forward")
			: (Extent.Y >= Extent.Z ? TEXT("Y, which is sideways") : TEXT("Z, which is up")));
}

void ASpaceMMOShipPawn::ResolveBlocking(const FSystemCoordinate& From)
{
	UWorld* const World = GetWorld();

	const USpaceMMORenderOriginSubsystem* const Origin =
		World != nullptr ? World->GetSubsystem<USpaceMMORenderOriginSubsystem>() : nullptr;

	if (World == nullptr || Origin == nullptr || HullRadiusKilometres <= 0.0)
	{
		return;
	}

	// Both ends converted in the same frame, so a rebase between frames cannot skew the sweep.
	FVector Here = Origin->ToWorldLocation(From);
	FVector Want = Origin->ToWorldLocation(Navigation.SystemPosition);

	if (FVector::DistSquared(Here, Want) < UE_DOUBLE_SMALL_NUMBER)
	{
		return;
	}

	const double RadiusCentimetres =
		HullRadiusKilometres * SpaceMMO::Coordinates::CentimetresPerKilometre;

	const FCollisionShape Sphere = FCollisionShape::MakeSphere(
		static_cast<float>(RadiusCentimetres));

	// The pawn channel, which is this project's name for "solid to something moving through the
	// world": it is what stations, deposits and other ships block. Simple collision only, so an
	// asset imported without it is invisible here -- SpaceMMOSolidity warns about exactly that.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SpaceMMOShipBlocking), false, this);

	const AActor* Touched = nullptr;
	FVector LastNormal = FVector::ZeroVector;

	// Three passes, as the character has: one to stop, one to spend the rest of the step along the
	// surface so a glancing approach slides rather than pinning, one for the corner where sliding
	// runs into something else.
	for (int32 Pass = 0; Pass < 3; ++Pass)
	{
		FHitResult Hit;

		const bool bHit = World->SweepSingleByChannel(
			Hit, Here, Want, FQuat::Identity, ECC_Pawn, Sphere, Params);

		if (!bHit || !Hit.bBlockingHit)
		{
			Here = Want;

			break;
		}

		Touched = Hit.GetActor();

		// A sweep that begins inside something reports the shortest way out rather than a surface it
		// crossed, and its impact normal describes nothing. Take the way out and try again.
		if (Hit.bStartPenetrating)
		{
			LastNormal = Hit.Normal.GetSafeNormal();

			const FVector Out =
				LastNormal * SpaceMMO::Surfaces::SeparationCentimetres(Hit.PenetrationDepth);

			Here += Out;
			Want += Out;

			continue;
		}

		LastNormal = Hit.ImpactNormal;

		const FVector Contact =
			Hit.Location + LastNormal * SpaceMMO::Surfaces::SeparationCentimetres(0.0);

		Want = Contact + FShipFlightModel::SlideDeltaCentimetres(Want - Hit.Location, LastNormal);
		Here = Contact;

		FlightState = FShipFlightModel::ResolveBlockingHit(FlightState, LastNormal);
	}

	// Applied as a delta rather than by converting back through the render origin, which has no
	// reverse: a difference between two points in the same frame means the same thing whatever the
	// origin happens to be.
	Navigation.SystemPosition = FSystemCoordinate(
		Navigation.SystemPosition.Kilometres
		+ ((Here - Origin->ToWorldLocation(Navigation.SystemPosition))
			/ SpaceMMO::Coordinates::CentimetresPerKilometre));

	ReportBlocking(Touched, LastNormal, (Want - Origin->ToWorldLocation(From)).Size());
}

void ASpaceMMOShipPawn::ReportBlocking(
	const AActor* const Touched, const FVector& Normal, const double WantedCentimetres)
{
	const UWorld* const World = GetWorld();

	if (World == nullptr)
	{
		return;
	}

	const AActor* const Was = BlockedBy.Get();

	if (Touched == Was)
	{
		if (Touched != nullptr)
		{
			BlockedWantedCentimetres += WantedCentimetres;
		}

		return;
	}

	if (Was != nullptr)
	{
		const double Seconds = FMath::Max(0.0, World->GetTimeSeconds() - BlockedSinceSeconds);

		const double GotCentimetres =
			(Navigation.SystemPosition.Kilometres - BlockedFrom.Kilometres).Size()
			* SpaceMMO::Coordinates::CentimetresPerKilometre;

		UE_LOG(LogSpaceMMO, Log,
			TEXT("Ship cleared %s after %.1f s: asked for %.0f cm along it and got %.0f."),
			*GetNameSafe(Was), Seconds, BlockedWantedCentimetres, GotCentimetres);

		// Asked against got, because a contact going nowhere is only a fault if the ship was trying
		// to go somewhere. A ship parked against a hangar wall asks for nothing.
		if (BlockedWantedCentimetres > 100.0 && GotCentimetres < BlockedWantedCentimetres * 0.1)
		{
			UE_LOG(LogSpaceMMO, Warning,
				TEXT("Ship pinned against %s for %.1f s: asked to move %.0f cm and moved %.0f. ")
				TEXT("That is stuck, not sliding."),
				*GetNameSafe(Was), Seconds, BlockedWantedCentimetres, GotCentimetres);
		}
	}

	BlockedBy = Touched;
	BlockedSinceSeconds = World->GetTimeSeconds();
	BlockedFrom = Navigation.SystemPosition;
	BlockedWantedCentimetres = Touched != nullptr ? WantedCentimetres : 0.0;

	if (Touched != nullptr)
	{
		// Named, because a mesh with no simple collision on it is silently intangible and flying
		// through a wall looks exactly like this feature not existing.
		UE_LOG(LogSpaceMMO, Log,
			TEXT("Ship blocked by %s, normal %s."),
			*GetNameSafe(Touched), *Normal.ToCompactString());
	}
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

	// Measured off the hull rather than taken from a constant, so a bigger ship steps you out
	// further and this cannot go stale the next time the drawn ship changes size.
	//
	// The largest horizontal half-extent, not the width specifically: which of a mesh's axes is its
	// width depends on how somebody authored it, and a step-out that lands inside the hull is worse
	// than one that lands a metre further out than it needed to. Extent rather than the component's
	// world bounds, deliberately -- a mesh with a baked-in translation has bounds a long way from
	// the pawn, and this must not inherit that fault (task 133).
	double OffsetKilometres = FBoarding::DefaultStepOutOffsetKilometres;

	if (Hull != nullptr && Hull->GetStaticMesh() != nullptr)
	{
		const FVector Extent =
			Hull->GetStaticMesh()->GetBounds().BoxExtent * Hull->GetRelativeScale3D();

		constexpr double ClearanceCentimetres = 100.0;

		OffsetKilometres =
			(FMath::Max(FMath::Abs(Extent.X), FMath::Abs(Extent.Y)) + ClearanceCentimetres)
			/ SpaceMMO::Coordinates::CentimetresPerKilometre;
	}

	const FSystemCoordinate StepOut = FBoarding::StepOutPosition(
		Navigation.SystemPosition, Up, FlightState.Rotation.GetRightVector(), OffsetKilometres);

	// Facing the way the ship faces. Nothing set this before, so a character stepped out facing
	// whichever way a freshly spawned pawn happens to.
	const FQuat Facing =
		FBoarding::StepOutRotation(Up, FlightState.Rotation.GetForwardVector());

	// Assigned in two statements rather than one conditional: TSubclassOf and UClass* both convert
	// to several common types, so the ternary is ambiguous.
	TSubclassOf<ASpaceMMOCharacterPawn> SpawnClass = CharacterClass;

	if (SpawnClass == nullptr)
	{
		SpawnClass = ASpaceMMOCharacterPawn::StaticClass();
	}

	ASpaceMMOCharacterPawn* Character = World->SpawnActorDeferred<ASpaceMMOCharacterPawn>(
		SpawnClass, FTransform(Facing), nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

	if (Character == nullptr)
	{
		return;
	}

	// Before FinishSpawning. BeginPlay resolves the ground and aligns to it, so a position applied
	// afterwards is a frame too late.
	Character->SetStartingSystemPosition(StepOut.Kilometres);

	// The rotation has to survive FinishSpawning, which is why it is on the transform rather than
	// applied afterwards: BeginPlay reads the actor's quaternion and aligns it to the ground, so a
	// heading set later is a heading set a frame too late.
	Character->FinishSpawning(FTransform(Facing));

	// The ship stays exactly where it is, unpossessed, waiting to be climbed back into.
	OwningController->Possess(Character);

	// The ship's position as well as the character's, because "the ship moved when I got out" and
	// "the ship is where it was and I am looking at it from somewhere new" produce the same
	// impression and different numbers (task 135). One line, both numbers, every time.
	UE_LOG(LogSpaceMMO, Log,
		TEXT("Stepped out of the ship at %s, %.1f m to its side, facing its nose. "
			"The ship is at %s and stays there."),
		*StepOut.ToString(),
		OffsetKilometres * 1000.0,
		*Navigation.SystemPosition.ToString());
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

	PlayerInputComponent->BindAxis(TEXT("ViewZoom"), this, &ASpaceMMOShipPawn::ZoomView);

	PlayerInputComponent->BindAction(
		TEXT("OrbitView"), IE_Pressed, this, &ASpaceMMOShipPawn::StartOrbit);
	PlayerInputComponent->BindAction(
		TEXT("OrbitView"), IE_Released, this, &ASpaceMMOShipPawn::StopOrbit);

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

void ASpaceMMOShipPawn::Pitch(const float Value)
{
	// While the orbit key is held the mouse swings the camera and the ship holds its attitude.
	// Torque is simulated by the server, so the input has to be stopped here rather than undone
	// afterwards -- a pitch sent and then cancelled is a pitch the server flew.
	if (View.bOrbiting)
	{
		View.Swing(0.0, Value * OrbitSensitivityDegrees, OrbitMaxPitchDegrees);

		PendingInput.Torque.Y = 0.0;

		return;
	}

	PendingInput.Torque.Y = Value;
}

void ASpaceMMOShipPawn::Yaw(const float Value)
{
	if (View.bOrbiting)
	{
		View.Swing(Value * OrbitSensitivityDegrees, 0.0, OrbitMaxPitchDegrees);

		PendingInput.Torque.Z = 0.0;

		return;
	}

	PendingInput.Torque.Z = Value;
}

void ASpaceMMOShipPawn::StartOrbit() { View.bOrbiting = true; }
void ASpaceMMOShipPawn::StopOrbit() { View.bOrbiting = false; }

void ASpaceMMOShipPawn::ZoomView(const float Value)
{
	if (FMath::IsNearlyZero(Value))
	{
		return;
	}

	View.Wheel(Value, ZoomNearCentimetres, ZoomFarCentimetres, ZoomStepFraction);

	// A ship is not destroyed when it is left, so this would survive on the pawn -- but it is kept
	// beside the character's for one reason: both are answers to "how does this player like to look
	// at the game", and splitting them across a pawn and a subsystem would leave the next person
	// looking in two places.
	if (const UGameInstance* const Game = GetGameInstance())
	{
		if (USpaceMMOViewSubsystem* const Remembered =
			Game->GetSubsystem<USpaceMMOViewSubsystem>())
		{
			Remembered->ShipArmCentimetres = View.ArmTargetCentimetres;
		}
	}
}

void ASpaceMMOShipPawn::ApplyView(const double DeltaSeconds)
{
	View.Advance(DeltaSeconds, OrbitReturnSeconds);

	if (CameraBoom == nullptr)
	{
		return;
	}

	CameraBoom->TargetArmLength = static_cast<float>(FMath::FInterpTo(
		static_cast<double>(CameraBoom->TargetArmLength),
		View.ArmTargetCentimetres,
		DeltaSeconds,
		ZoomSmoothingSeconds > 0.0 ? 1.0 / ZoomSmoothingSeconds : 0.0));

	CameraBoom->SetRelativeRotation(FRotator(View.Orbit.Pitch, View.Orbit.Yaw, 0.0));

	// In the arm's rotated frame, so the hull stays low in shot through a swing rather than sliding
	// across the screen as the view comes round.
	CameraBoom->SocketOffset = FThirdPersonView::ShoulderAt(
		ShoulderOffset,
		CameraBoom->TargetArmLength,
		ShoulderReferenceArmCentimetres);
}

void ASpaceMMOShipPawn::StartBoost() { PendingInput.bBoost = true; }

void ASpaceMMOShipPawn::StopBoost() { PendingInput.bBoost = false; }
