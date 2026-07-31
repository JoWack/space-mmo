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
#include "SpaceMMOPlanetActor.h"
#include "SpaceMMORenderOrigin.h"
#include "UObject/ConstructorHelpers.h"

ASpaceMMOShipPawn::ASpaceMMOShipPawn()
{
	PrimaryActorTick.bCanEverTick = true;

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

	AutoPossessPlayer = EAutoReceiveInput::Player0;
}

void ASpaceMMOShipPawn::BeginPlay()
{
	Super::BeginPlay();

	Navigation = FShipNavigation();
	Navigation.SystemPosition = FSystemCoordinate(StartingSystemPositionKilometres);

	// Start with the ship at the render origin, so it begins exactly where physics behaves best.
	Navigation.RenderOrigin = Navigation.SystemPosition;

	FlightState = FShipFlightState();
	FlightState.Rotation = GetActorQuat();

	PublishRenderOrigin();
	ApplyWorldTransform();

	UE_LOG(LogSpaceMMO, Log, TEXT("Ship ready at %s"), *Navigation.SystemPosition.ToString());
}

void ASpaceMMOShipPawn::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	FlightState = FShipFlightModel::Step(
		FlightState, PendingInput, FlightConfig, DeltaSeconds, ComputeGravity());

	Navigation = FShipFlightModel::Advance(Navigation, FlightState, DeltaSeconds);

	PublishRenderOrigin();
	ApplyWorldTransform();

	// Classified after moving, and fed its own previous value so the hysteresis in
	// ClassifyProximity has something to work against.
	for (TActorIterator<ASpaceMMOPlanetActor> It(GetWorld()); It; ++It)
	{
		Proximity = FPlanetPhysics::ClassifyProximity(
			It->GetPlanetConfig(), Navigation.SystemPosition, Proximity);

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

		GEngine->AddOnScreenDebugMessage(
			3, 0.0f, FColor::Yellow,
			FString::Printf(
				TEXT("Altitude %.2f km | %s"), GetAltitudeKilometres(), ProximityName));

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
		Total += FPlanetPhysics::GravityAcceleration(
			It->GetPlanetConfig(), Navigation.SystemPosition);
	}

	return Total;
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
	SetActorLocationAndRotation(Navigation.RenderLocationCentimetres(), FlightState.Rotation);
}

void ASpaceMMOShipPawn::SetSystemPosition(const FSystemCoordinate& NewPosition)
{
	Navigation.SystemPosition = NewPosition;
	Navigation.RenderOrigin = NewPosition;
	++Navigation.RebaseCount;

	PublishRenderOrigin();
	ApplyWorldTransform();
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
}

void ASpaceMMOShipPawn::ThrustForward(const float Value) { PendingInput.Thrust.X = Value; }

void ASpaceMMOShipPawn::ThrustRight(const float Value) { PendingInput.Thrust.Y = Value; }

void ASpaceMMOShipPawn::ThrustUp(const float Value) { PendingInput.Thrust.Z = Value; }

void ASpaceMMOShipPawn::Roll(const float Value) { PendingInput.Torque.X = Value; }

void ASpaceMMOShipPawn::Pitch(const float Value) { PendingInput.Torque.Y = Value; }

void ASpaceMMOShipPawn::Yaw(const float Value) { PendingInput.Torque.Z = Value; }

void ASpaceMMOShipPawn::StartBoost() { PendingInput.bBoost = true; }

void ASpaceMMOShipPawn::StopBoost() { PendingInput.bBoost = false; }
