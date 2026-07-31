#include "SpaceMMOShipPawn.h"

#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "GameFramework/SpringArmComponent.h"

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

	ApplyWorldTransform();
}

void ASpaceMMOShipPawn::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	FlightState = FShipFlightModel::Step(FlightState, PendingInput, FlightConfig, DeltaSeconds);

	Navigation = FShipFlightModel::Advance(Navigation, FlightState, DeltaSeconds);

	ApplyWorldTransform();

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

		GEngine->AddOnScreenDebugMessage(
			2, 0.0f, FColor::Silver,
			FString::Printf(TEXT("World %s"), *GetActorLocation().ToCompactString()));
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
