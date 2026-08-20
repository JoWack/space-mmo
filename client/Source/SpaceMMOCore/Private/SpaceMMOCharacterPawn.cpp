#include "SpaceMMOCharacterPawn.h"

#include "Animation/AnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
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

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaceholderMesh(
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));

	if (PlaceholderMesh.Succeeded())
	{
		Body->SetStaticMesh(PlaceholderMesh.Object);
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

	// The real body, sharing the placeholder's root and its convention: the pawn's origin is the
	// character's feet, so a model whose own origin is at its feet needs no offset at all.
	BodyMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("BodyMesh"));
	BodyMesh->SetupAttachment(CharacterRoot);

	// Collision off for the same reason the placeholder's is: position is owned by the walk model
	// and ground contact, and a solver given an opinion here would fight the authoritative one and
	// win intermittently.
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Animation is drawn, not simulated. Ticking the pose after physics keeps it out of the way of
	// anything that decides where the character actually is.
	BodyMesh->PrimaryComponentTick.TickGroup = TG_PostPhysics;

	// Hidden until a mesh is actually configured, so an unset path shows the tube rather than
	// nothing at all.
	BodyMesh->SetVisibility(false);

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

	// Before the ground is resolved, because it changes what is drawn rather than where anything
	// is, and a warning about a missing model is worth having in the log above the first frame.
	ApplyCharacterMesh();
	ApplyCameraView();

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
		if (!bReportedStandingGap)
		{
			bReportedStandingGap = true;

			// Whichever body is actually being drawn.
			//
			// It measured the placeholder unconditionally, which stopped being the right answer
			// the moment a character model could be configured: the tube is hidden then, and the
			// diagnostic would have gone on confidently describing a cylinder nobody can see while
			// the visible character floated or sank. A measurement that silently measures the
			// wrong thing is worse than none, because it answers anyway.
			const bool bUsingModel =
				BodyMesh != nullptr
				&& BodyMesh->GetSkeletalMeshAsset() != nullptr
				&& BodyMesh->IsVisible();

			const USceneComponent* const Drawn = bUsingModel
				? static_cast<USceneComponent*>(BodyMesh)
				: static_cast<USceneComponent*>(Body);

			if (Drawn != nullptr)
			{
				// Local bounds, so this is the mesh's own shape rather than where it happens to be
				// standing this frame -- the same reason the static version read the asset's
				// bounds rather than the component's world ones.
				const FBoxSphereBounds Local = bUsingModel
					? BodyMesh->GetSkeletalMeshAsset()->GetBounds()
					: (Body->GetStaticMesh() != nullptr
						? Body->GetStaticMesh()->GetBounds()
						: FBoxSphereBounds(ForceInit));

				const FVector Scale = Drawn->GetRelativeScale3D();

				UE_LOG(LogSpaceMMO, Log,
					TEXT("Standing gap: drawing the %s; standing height %.1f cm; relative Z %.1f cm, "
						"scale Z %.2f; mesh local origin Z %.1f, half height %.1f cm -> scaled half "
						"height %.1f cm, so mesh spans %.1f..%.1f cm above the root."),
					bUsingModel ? TEXT("character model") : TEXT("placeholder tube"),
					StandingHeightKilometres * 100000.0,
					Drawn->GetRelativeLocation().Z,
					Scale.Z,
					Local.Origin.Z,
					Local.BoxExtent.Z,
					Local.BoxExtent.Z * Scale.Z,
					Drawn->GetRelativeLocation().Z
						+ ((Local.Origin.Z - Local.BoxExtent.Z) * Scale.Z),
					Drawn->GetRelativeLocation().Z
						+ ((Local.Origin.Z + Local.BoxExtent.Z) * Scale.Z));
			}
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

	ApplyCameraView();
}

void ASpaceMMOCharacterPawn::ApplyCameraView()
{
	if (ThirdPersonCamera != nullptr)
	{
		ThirdPersonCamera->SetActive(!bFirstPerson);
	}

	if (FirstPersonCamera != nullptr)
	{
		FirstPersonCamera->SetActive(bFirstPerson);
	}

	// The body goes away in first person: the camera is inside the character's head, and a model
	// drawn there fills the screen with the underside of a jaw and the inside of a torso.
	//
	// Only ever this pawn's own body, and only on the machine looking through its eyes. bFirstPerson
	// is set by a key press, which only the locally controlled pawn receives, so another player's
	// character is never hidden by their choice of view.
	const bool bBodyVisible = !bFirstPerson;

	if (BodyMesh != nullptr && BodyMesh->GetSkeletalMeshAsset() != nullptr)
	{
		BodyMesh->SetVisibility(bBodyVisible);
	}
	else if (Body != nullptr)
	{
		Body->SetVisibility(bBodyVisible);
	}
}

double ASpaceMMOCharacterPawn::UniformScaleForHeight(
	const double AuthoredHeightCentimetres, const double TargetCentimetres)
{
	// Either being unusable leaves the model exactly as authored. Dividing by a zero height, or
	// scaling to a zero target, both end with a character that is not there at all -- which reads
	// as a failure to spawn rather than as a number nobody set.
	if (AuthoredHeightCentimetres <= UE_DOUBLE_SMALL_NUMBER || TargetCentimetres <= 0.0)
	{
		return 1.0;
	}

	return TargetCentimetres / AuthoredHeightCentimetres;
}

void ASpaceMMOCharacterPawn::ApplyCharacterMesh()
{
	if (BodyMesh == nullptr)
	{
		return;
	}

	// Said on every path, including the one that does nothing. An unset model and code that never
	// ran produce the same evidence -- a tube -- and only one of them is somebody's mistake.
	UE_LOG(LogSpaceMMO, Log,
		TEXT("Character model configured as '%s', animation as '%s'."),
		CharacterMesh.IsNull() ? TEXT("<unset>") : *CharacterMesh.ToString(),
		CharacterAnimClass.IsNull() ? TEXT("<unset>") : *CharacterAnimClass.ToString());

	if (CharacterMesh.IsNull())
	{
		return;
	}

	USkeletalMesh* const Mesh = Cast<USkeletalMesh>(CharacterMesh.TryLoad());

	// Named-but-wrong is worth saying out loud: from the outside a typo and an unset path look
	// identical, and one of them is a mistake somebody wants telling about.
	if (Mesh == nullptr)
	{
		UE_LOG(LogSpaceMMO, Warning,
			TEXT("Character model '%s' did not load; the placeholder stays."),
			*CharacterMesh.ToString());

		return;
	}

	// Measured off the model, not assumed. The bounds are the reference pose's, which is the only
	// thing that knows what scale somebody exported at.
	const double AuthoredHeight = Mesh->GetBounds().BoxExtent.Z * 2.0;
	const double Scale = UniformScaleForHeight(AuthoredHeight, CharacterHeightCentimetres);

	BodyMesh->SetSkeletalMeshAsset(Mesh);
	BodyMesh->SetRelativeRotation(CharacterMeshRotation);
	BodyMesh->SetRelativeLocation(CharacterMeshOffset);
	BodyMesh->SetRelativeScale3D(FVector(Scale));
	BodyMesh->SetVisibility(true);

	// The tube has done its job. Hidden rather than destroyed, so a model that fails to load on a
	// later run still has something to fall back to.
	if (Body != nullptr)
	{
		Body->SetVisibility(false);
	}

	if (!CharacterAnimClass.IsNull())
	{
		UClass* const AnimClass = CharacterAnimClass.TryLoadClass<UAnimInstance>();

		if (AnimClass == nullptr)
		{
			// A model in its bind pose is a T-posed statue sliding around the planet, which is a
			// working state on the way to an animated one -- but not one to arrive at silently.
			UE_LOG(LogSpaceMMO, Warning,
				TEXT("Animation blueprint '%s' did not load; '%s' stands in its bind pose."),
				*CharacterAnimClass.ToString(), *Mesh->GetName());
		}
		else
		{
			BodyMesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
			BodyMesh->SetAnimInstanceClass(AnimClass);
		}
	}

	// The authored height is named as well as the applied scale, because a model that needs a large
	// multiplier is a model exported at the wrong scale, and that is worth being told rather than
	// silently corrected forever.
	UE_LOG(LogSpaceMMO, Log,
		TEXT("Character drawing as '%s': authored %.1f cm, scaled %.3f to stand %.1f cm; "
			"rotated %s, offset %s."),
		*Mesh->GetName(),
		AuthoredHeight,
		Scale,
		AuthoredHeight * Scale,
		*CharacterMeshRotation.ToCompactString(),
		*CharacterMeshOffset.ToCompactString());
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
