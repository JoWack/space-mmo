#include "SpaceMMOCharacterPawn.h"

#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/SpringArmComponent.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "SpaceMMOLog.h"
#include "SpaceMMOBoarding.h"
#include "SpaceMMOPlanetActor.h"
#include "SpaceMMOShipPawn.h"
#include "SpaceMMOPlanetTerrain.h"
#include "SpaceMMORenderOrigin.h"
#include "UObject/ConstructorHelpers.h"

ASpaceMMOCharacterPawn::ASpaceMMOCharacterPawn()
{
	PrimaryActorTick.bCanEverTick = true;

	bReplicates = true;

	// Off for the same reason as the ship: Unreal replicates world transforms, and world
	// transforms are not comparable between clients that rebase independently.
	SetReplicateMovement(false);

	CharacterRoot = CreateDefaultSubobject<USceneComponent>(TEXT("CharacterRoot"));
	SetRootComponent(CharacterRoot);

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(CharacterRoot);

	// Position is owned by the walk model and ground contact, not by Chaos. Leaving collision on
	// would let the solver fight the authoritative position and win intermittently.
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> BodyMesh(
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));

	if (BodyMesh.Succeeded())
	{
		Body->SetStaticMesh(BodyMesh.Object);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BodyMaterial(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	if (BodyMaterial.Succeeded())
	{
		Body->SetMaterial(0, BodyMaterial.Object);
	}

	// The engine cylinder is 100 cm tall with its origin at the centre, so a person-sized one is
	// scaled to 1.8 and lifted 90 cm: it then spans 0 to 180 cm above the root, standing on its
	// feet with its head at 1.8 m and both cameras -- at 160 and 165 cm -- at eye height.
	//
	// The scale was 0.9, which is what this comment's arithmetic gives if the mesh is 200 cm tall.
	// It is 100, measured: half height 50. So the character was 90 cm tall with its feet 45 cm
	// above its own origin -- floating on the flat, and buried to the knees on anything sloped,
	// which is two symptoms of one number.
	Body->SetRelativeScale3D(FVector(0.4, 0.4, 1.8));
	Body->SetRelativeLocation(FVector(0.0, 0.0, 90.0));

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(CharacterRoot);
	CameraBoom->TargetArmLength = 400.0f;
	CameraBoom->SetRelativeLocation(FVector(0.0, 0.0, 160.0));
	CameraBoom->bDoCollisionTest = false;

	// Follows the character's own orientation rather than the controller's, because the character's
	// up is the ground's normal and the controller has no idea where that points.
	CameraBoom->bUsePawnControlRotation = false;
	CameraBoom->bInheritPitch = true;
	CameraBoom->bInheritYaw = true;
	CameraBoom->bInheritRoll = true;

	ThirdPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("ThirdPersonCamera"));
	ThirdPersonCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);

	FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCamera->SetupAttachment(CharacterRoot);
	FirstPersonCamera->SetRelativeLocation(FVector(20.0, 0.0, 165.0));
	FirstPersonCamera->SetActive(false);
}

void ASpaceMMOCharacterPawn::BeginPlay()
{
	Super::BeginPlay();

	Navigation = FShipNavigation();
	Navigation.SystemPosition = FSystemCoordinate(StartingSystemPositionKilometres);
	Navigation.RenderOrigin = Navigation.SystemPosition;

	double StartX = 0.0;
	double StartY = 0.0;
	double StartZ = 0.0;

	if (FParse::Value(FCommandLine::Get(), TEXT("WalkStartX="), StartX)
		| FParse::Value(FCommandLine::Get(), TEXT("WalkStartY="), StartY)
		| FParse::Value(FCommandLine::Get(), TEXT("WalkStartZ="), StartZ))
	{
		Navigation.SystemPosition = FSystemCoordinate(FVector(StartX, StartY, StartZ));
		Navigation.RenderOrigin = Navigation.SystemPosition;
	}

	// Resolve the ground before the first step so the character starts standing on the surface with
	// its up already correct, rather than upright in world space and snapping on frame one.
	ResolveSurface();

	WalkState.Rotation = FCharacterWalkModel::AlignToSurface(GetActorQuat(), SurfaceNormal);

	PublishRenderOrigin();
	ApplyWorldTransform();

	UE_LOG(LogSpaceMMO, Log, TEXT("Character ready at %s, up %s"),
		*Navigation.SystemPosition.ToString(), *SurfaceNormal.ToCompactString());
}

void ASpaceMMOCharacterPawn::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ASpaceMMOCharacterPawn, NetState);
}

void ASpaceMMOCharacterPawn::ResolveSurface()
{
	UWorld* World = GetWorld();

	if (World == nullptr)
	{
		return;
	}

	Gravity = FVector::ZeroVector;
	const bool bWasOnGround = bOnGround;

	bOnGround = false;
	SurfaceNormal = FVector::UpVector;

	for (TActorIterator<ASpaceMMOPlanetActor> It(World); It; ++It)
	{
		const FPlanetConfig& Planet = It->GetPlanetConfig();

		Gravity += FPlanetPhysics::GravityAcceleration(Planet, Navigation.SystemPosition);

		const FGroundContact Contact = FPlanetTerrain::ResolveContact(
			Planet,
			It->GetTerrainConfig(),
			Navigation.SystemPosition,
			WalkState.Velocity,
			StandingHeightKilometres,
			FPlanetTerrain::DefaultContactToleranceKilometres,
			bWasOnGround);

		// The normal is taken from whichever body is underfoot even when not touching it, so a
		// jumping character stays oriented to the ground it left rather than snapping upright.
		SurfaceNormal = Contact.SurfaceNormal;

		if (!Contact.bOnGround)
		{
			continue;
		}

		bOnGround = true;

		Navigation.SystemPosition = Contact.Position;

		// Raw facts, and nothing derived from them.
		//
		// Two attempts at computing "how far are the feet off the ground" were both wrong -- one
		// measured along world Z on a character aligned to a sphere, the other used a bounding
		// sphere radius as a stand-in for a cylinder's half height -- and each produced a confident
		// number that disagreed with the screen. So this prints what is actually known and leaves
		// the arithmetic to whoever is reading, which is the only version that cannot be subtly
		// wrong.
		if (!bReportedStandingGap && Body != nullptr)
		{
			bReportedStandingGap = true;

			const UStaticMesh* const Mesh = Body->GetStaticMesh();

			const FVector LocalExtent = Mesh != nullptr
				? Mesh->GetBounds().BoxExtent
				: FVector::ZeroVector;

			const FVector LocalOrigin = Mesh != nullptr
				? Mesh->GetBounds().Origin
				: FVector::ZeroVector;

			UE_LOG(LogSpaceMMO, Log,
				TEXT("Standing gap: standing height %.1f cm; body relative Z %.1f cm, scale Z %.2f; "
					"mesh local origin Z %.1f, half height %.1f cm -> scaled half height %.1f cm, "
					"so mesh spans %.1f..%.1f cm above the root."),
				StandingHeightKilometres * 100000.0,
				Body->GetRelativeLocation().Z,
				Body->GetRelativeScale3D().Z,
				LocalOrigin.Z,
				LocalExtent.Z,
				LocalExtent.Z * Body->GetRelativeScale3D().Z,
				Body->GetRelativeLocation().Z
					+ ((LocalOrigin.Z - LocalExtent.Z) * Body->GetRelativeScale3D().Z),
				Body->GetRelativeLocation().Z
					+ ((LocalOrigin.Z + LocalExtent.Z) * Body->GetRelativeScale3D().Z));
		}
		WalkState.Velocity = Contact.Velocity;
	}
}

void ASpaceMMOCharacterPawn::SimulateStep(const double DeltaSeconds)
{
	// Ground first: the walk model needs to know which way is up and whether it has anything to
	// push against before it can decide what this step does.
	ResolveSurface();

	WalkState = FCharacterWalkModel::Step(
		WalkState, PendingInput, WalkConfig, SurfaceNormal, Gravity, bOnGround, DeltaSeconds);

	Navigation.SystemPosition = FSystemCoordinate(
		Navigation.SystemPosition.Kilometres
		+ FCharacterWalkModel::PositionDeltaKilometres(WalkState, DeltaSeconds));

	// And again after moving, so the step that would have driven the character into a hill is
	// undone in the same frame rather than being visible for one.
	ResolveSurface();

	if (!Navigation.SystemPosition.IsWithinLocalSpaceOf(Navigation.RenderOrigin))
	{
		Navigation.RenderOrigin = Navigation.SystemPosition;
		++Navigation.RebaseCount;
	}
}

void ASpaceMMOCharacterPawn::PublishNetState()
{
	NetState.SystemPosition = Navigation.SystemPosition;
	NetState.Velocity = WalkState.Velocity;
	NetState.Rotation = WalkState.Rotation;
	NetState.bOnGround = bOnGround;

	const UWorld* World = GetWorld();

	NetState.ServerTimeSeconds = World != nullptr ? World->GetTimeSeconds() : 0.0;
}

bool ASpaceMMOCharacterPawn::ServerSendWalkInput_Validate(FWalkInput Input)
{
	// Nothing to reject; Sanitised clamps every axis, so no value here can be hostile.
	return true;
}

void ASpaceMMOCharacterPawn::ServerSendWalkInput_Implementation(FWalkInput Input)
{
	PendingInput = Input.Sanitised();
}

void ASpaceMMOCharacterPawn::ReconcileWithServer(const double DeltaSeconds)
{
	if (NetState.ServerTimeSeconds <= LastAppliedServerTime)
	{
		return;
	}

	LastAppliedServerTime = NetState.ServerTimeSeconds;

	Navigation.SystemPosition = FShipFlightModel::ReconcilePosition(
		Navigation.SystemPosition, NetState.SystemPosition, Reconciliation, DeltaSeconds);

	WalkState.Velocity = NetState.Velocity;
}

void ASpaceMMOCharacterPawn::FollowServerState(const double DeltaSeconds)
{
	const UWorld* World = GetWorld();
	const double Now = World != nullptr ? World->GetTimeSeconds() : 0.0;

	if (NetState.ServerTimeSeconds > LastAppliedServerTime)
	{
		LastAppliedServerTime = NetState.ServerTimeSeconds;
		LastNetStateReceivedAt = Now;
	}

	const FSystemCoordinate Target = FShipFlightModel::Extrapolate(
		NetState.SystemPosition, NetState.Velocity, Now - LastNetStateReceivedAt);

	Navigation.SystemPosition = FShipFlightModel::ReconcilePosition(
		Navigation.SystemPosition, Target, Reconciliation, DeltaSeconds);

	// Drawn, not simulated. Orientation comes from the server, which already aligned it to the
	// ground the character is actually standing on.
	WalkState.Rotation = NetState.Rotation;
	WalkState.Velocity = NetState.Velocity;
	bOnGround = NetState.bOnGround;
}

void ASpaceMMOCharacterPawn::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (HasAuthority())
	{
		SimulateStep(DeltaSeconds);
		PublishNetState();
	}
	else if (IsLocallyControlled())
	{
		SimulateStep(DeltaSeconds);
		ServerSendWalkInput(PendingInput);
		ReconcileWithServer(DeltaSeconds);
	}
	else
	{
		FollowServerState(DeltaSeconds);
	}

	if (IsLocallyControlled())
	{
		PublishRenderOrigin();
	}

	ApplyWorldTransform();

	// Cleared each frame because the legacy input path only calls the handlers while a key is held.
	PendingInput.Move = FVector2D::ZeroVector;
	PendingInput.Turn = 0.0;

	DiagnosticSeconds += DeltaSeconds;

	if (FParse::Param(FCommandLine::Get(), TEXT("LogApproach")) && DiagnosticSeconds >= 1.0)
	{
		DiagnosticSeconds = 0.0;

		const ASpaceMMOShipPawn* Nearest = nullptr;
		double NearestKm = TNumericLimits<double>::Max();

		for (TActorIterator<ASpaceMMOShipPawn> It(GetWorld()); It; ++It)
		{
			const double Distance =
				(Navigation.SystemPosition.Kilometres - It->GetSystemPosition().Kilometres).Size();

			if (Distance < NearestKm)
			{
				NearestKm = Distance;
				Nearest = *It;
			}
		}

		UE_LOG(LogSpaceMMO, Log,
			TEXT("ONFOOT: sys %s | %s | ship %.1f m away, drawn %.1f m | speed %.1f m/s"),
			*Navigation.SystemPosition.ToString(),
			bOnGround ? TEXT("GROUNDED") : TEXT("AIRBORNE"),
			NearestKm * 1000.0,
			Nearest != nullptr
				? (Nearest->GetActorLocation() - GetActorLocation()).Size() / 100.0
				: -1.0,
			GetSpeedMetresPerSecond());
	}

	if (bShowWalkDebug && GEngine != nullptr && IsLocallyControlled())
	{
		GEngine->AddOnScreenDebugMessage(
			10, 0.0f, FColor::Green,
			FString::Printf(
				TEXT("On foot %s | %.1f m/s | %s"),
				*Navigation.SystemPosition.ToString(),
				GetSpeedMetresPerSecond(),
				bOnGround ? TEXT("GROUNDED") : TEXT("AIRBORNE")));

		GEngine->AddOnScreenDebugMessage(
			11, 0.0f, FColor::Emerald,
			FString::Printf(TEXT("Up %s"), *SurfaceNormal.ToCompactString()));
	}
}

void ASpaceMMOCharacterPawn::PublishRenderOrigin()
{
	const UWorld* World = GetWorld();

	if (!IsLocallyControlled())
	{
		return;
	}

	// Rebase from wherever the character actually is, before publishing.
	//
	// The rebase test used to live only in the locally-simulated movement path, so a position that
	// arrived any other way — a spawn, a teleport off a ship, replication from the server — left
	// the render origin wherever it had been. On disembarking, this pawn begins play at the system
	// origin, publishes that, and the whole world is then drawn relative to a point forty
	// kilometres from the player. Static meshes survive it; a kilometres-wide generated mesh does
	// not, which is what "there is no ground under me" turned out to be.
	//
	// Asking the question here means it is asked once per frame against the position that is
	// actually being rendered, whatever produced it.
	if (!Navigation.SystemPosition.IsWithinLocalSpaceOf(Navigation.RenderOrigin))
	{
		Navigation.RenderOrigin = Navigation.SystemPosition;
		++Navigation.RebaseCount;

		UE_LOG(LogSpaceMMO, Log,
			TEXT("REBASE %d: character sys %s"),
			Navigation.RebaseCount,
			*Navigation.SystemPosition.ToString());
	}

	if (USpaceMMORenderOriginSubsystem* Origin =
		World != nullptr ? World->GetSubsystem<USpaceMMORenderOriginSubsystem>() : nullptr)
	{
		Origin->SetRenderOrigin(Navigation.RenderOrigin);
	}
}

void ASpaceMMOCharacterPawn::ApplyWorldTransform()
{
	const UWorld* World = GetWorld();

	const USpaceMMORenderOriginSubsystem* Origin =
		World != nullptr ? World->GetSubsystem<USpaceMMORenderOriginSubsystem>() : nullptr;

	// Against the subsystem's origin, not this pawn's own, so remote characters are drawn in the
	// frame of reference this client is actually rendering in.
	const FVector Location = Origin != nullptr
		? Origin->ToWorldLocation(Navigation.SystemPosition)
		: Navigation.RenderLocationCentimetres();

	SetActorLocationAndRotation(Location, WalkState.Rotation);
}

void ASpaceMMOCharacterPawn::SetSystemPosition(const FSystemCoordinate& NewPosition)
{
	Navigation.SystemPosition = NewPosition;
	Navigation.RenderOrigin = NewPosition;
	++Navigation.RebaseCount;

	ResolveSurface();

	WalkState.Rotation = FCharacterWalkModel::AlignToSurface(WalkState.Rotation, SurfaceNormal);

	PublishRenderOrigin();
	ApplyWorldTransform();
}

void ASpaceMMOCharacterPawn::RequestEmbark()
{
	ServerEmbark();
}

void ASpaceMMOCharacterPawn::ServerEmbark_Implementation()
{
	AController* OwningController = GetController();
	UWorld* World = GetWorld();

	if (OwningController == nullptr || World == nullptr)
	{
		return;
	}

	// Nearest in range, chosen here rather than named by the client. Nearest rather than first,
	// so parking two ships side by side does not board whichever happened to spawn earlier.
	ASpaceMMOShipPawn* Best = nullptr;
	double BestDistance = TNumericLimits<double>::Max();

	for (TActorIterator<ASpaceMMOShipPawn> It(World); It; ++It)
	{
		ASpaceMMOShipPawn* Ship = *It;

		// Somebody else is flying it.
		if (Ship == nullptr || Ship->GetController() != nullptr)
		{
			continue;
		}

		const double Distance =
			(Navigation.SystemPosition.Kilometres - Ship->GetSystemPosition().Kilometres).Size();

		if (Distance < BestDistance
			&& FBoarding::CanEmbark(Navigation.SystemPosition, Ship->GetSystemPosition()))
		{
			Best = Ship;
			BestDistance = Distance;
		}
	}

	if (Best == nullptr)
	{
		UE_LOG(LogSpaceMMO, Log, TEXT("No ship within boarding range."));

		return;
	}

	UE_LOG(LogSpaceMMO, Log,
		TEXT("Boarded a ship from %.3f km away."), BestDistance);

	OwningController->Possess(Best);

	// Destroyed only after possession has moved on. Destroying first would leave the controller
	// briefly possessing nothing, and anything that runs in that window has no pawn to ask.
	Destroy();
}

void ASpaceMMOCharacterPawn::ToggleCameraView()
{
	bFirstPerson = !bFirstPerson;

	ThirdPersonCamera->SetActive(!bFirstPerson);
	FirstPersonCamera->SetActive(bFirstPerson);
}

void ASpaceMMOCharacterPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (PlayerInputComponent == nullptr)
	{
		return;
	}

	PlayerInputComponent->BindAxis(
		TEXT("WalkForward"), this, &ASpaceMMOCharacterPawn::MoveForward);
	PlayerInputComponent->BindAxis(
		TEXT("WalkRight"), this, &ASpaceMMOCharacterPawn::MoveRight);
	PlayerInputComponent->BindAxis(
		TEXT("WalkTurn"), this, &ASpaceMMOCharacterPawn::TurnRight);
	PlayerInputComponent->BindAxis(
		TEXT("WalkLook"), this, &ASpaceMMOCharacterPawn::LookUp);

	PlayerInputComponent->BindAction(
		TEXT("WalkJump"), IE_Pressed, this, &ASpaceMMOCharacterPawn::StartJump);
	PlayerInputComponent->BindAction(
		TEXT("WalkJump"), IE_Released, this, &ASpaceMMOCharacterPawn::StopJump);
	PlayerInputComponent->BindAction(
		TEXT("ToggleCamera"), IE_Pressed, this, &ASpaceMMOCharacterPawn::ToggleCameraView);

	PlayerInputComponent->BindAction(
		TEXT("Board"), IE_Pressed, this, &ASpaceMMOCharacterPawn::RequestEmbark);
}

void ASpaceMMOCharacterPawn::MoveForward(const float Value)
{
	PendingInput.Move.X = Value;
}

void ASpaceMMOCharacterPawn::MoveRight(const float Value)
{
	PendingInput.Move.Y = Value;
}

void ASpaceMMOCharacterPawn::TurnRight(const float Value)
{
	PendingInput.Turn = Value;
}

void ASpaceMMOCharacterPawn::LookUp(const float Value)
{
	if (FMath::IsNearlyZero(Value))
	{
		return;
	}

	// Clamped rather than wrapped. Past vertical the view rolls over and every subsequent movement
	// reads inverted, which is indistinguishable from broken controls and has no recovery that is
	// not itself surprising.
	ViewPitchDegrees = FMath::Clamp(
		ViewPitchDegrees + (Value * LookSensitivityDegrees),
		-MaxLookPitchDegrees,
		MaxLookPitchDegrees);

	// On the boom, which already inherits the character's own orientation -- and the character's up
	// is the ground's normal, not the world's. Adding pitch to a controller rotation instead would
	// be pitching about an axis that means nothing on a sphere.
	if (CameraBoom != nullptr)
	{
		CameraBoom->SetRelativeRotation(FRotator(ViewPitchDegrees, 0.0, 0.0));
	}

	// The first-person camera is on the root rather than the boom, so it has to be tilted too or
	// looking up works in one view and silently does nothing in the other.
	if (FirstPersonCamera != nullptr)
	{
		FirstPersonCamera->SetRelativeRotation(FRotator(ViewPitchDegrees, 0.0, 0.0));
	}
}

void ASpaceMMOCharacterPawn::StartJump()
{
	PendingInput.bJump = true;
}

void ASpaceMMOCharacterPawn::StopJump()
{
	// Held rather than edge-triggered, so the jump survives until a frame actually consumes it —
	// pressing jump between two ticks would otherwise be silently dropped.
	PendingInput.bJump = false;
}
