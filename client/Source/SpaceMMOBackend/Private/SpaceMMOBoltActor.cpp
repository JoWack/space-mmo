#include "SpaceMMOBoltActor.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "SpaceMMORenderOrigin.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** Laser red-orange, bright enough to read against a lit desert at range. Linear, and over 1 to glow. */
	const FLinearColor BoltColour(6.0f, 1.2f, 0.25f, 1.0f);

	/** The engine's cylinder is 100 units tall and 100 across. */
	constexpr double MeshUnits = 100.0;

	/** A streak about four centimetres across. */
	constexpr double StreakWidthCentimetres = 4.0;

	/** The flash, at its largest, across. */
	constexpr double FlashCentimetres = 35.0;

	UStaticMeshComponent* MakePart(AActor* Owner, const TCHAR* Name, UStaticMesh* Mesh)
	{
		UStaticMeshComponent* Part = Owner->CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Part->SetStaticMesh(Mesh);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetCastShadow(false);
		Part->SetGenerateOverlapEvents(false);
		Part->SetMobility(EComponentMobility::Movable);

		return Part;
	}
}

ASpaceMMOBoltActor::ASpaceMMOBoltActor()
{
	PrimaryActorTick.bCanEverTick = true;

	// Each client draws its own from the server's multicast; there is nothing to replicate.
	bReplicates = false;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	Streak = MakePart(this, TEXT("Streak"), Cylinder.Object);
	Streak->SetupAttachment(Root);

	Flash = MakePart(this, TEXT("Flash"), Sphere.Object);
	Flash->SetupAttachment(Root);
	Flash->SetVisibility(false);
}

void ASpaceMMOBoltActor::Launch(const FSystemCoordinate& Start, const FSystemCoordinate& End, const bool bHitSomebody)
{
	StartPosition = Start;
	EndPosition = End;
	LengthMetres = FVector::Distance(Start.Kilometres, End.Kilometres) * 1000.0;
	bHit = bHitSomebody;
	bLaunched = true;
	Elapsed = 0.0;

	// The prototyping kit's glow: unlit, translucent, with a colour to set. One instance a bolt, so each can fade.
	if (UMaterialInterface* Glow = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Game/LevelPrototyping/Interactable/JumpPad/Assets/Materials/M_SimpleGlow.M_SimpleGlow")))
	{
		for (UStaticMeshComponent* Part : {Streak.Get(), Flash.Get()})
		{
			UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Glow, this);
			Instance->SetVectorParameterValue(TEXT("Color"), BoltColour);
			Part->SetMaterial(0, Instance);
		}
	}

	Place();
}

void ASpaceMMOBoltActor::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bLaunched)
	{
		return;
	}

	Elapsed += DeltaSeconds;

	const double Travel = LengthMetres / SpeedMetresPerSecond;

	if (Elapsed > Travel + (bHit ? FlashSeconds : 0.0))
	{
		Destroy();

		return;
	}

	Place();
}

void ASpaceMMOBoltActor::Place()
{
	const UWorld* World = GetWorld();

	const USpaceMMORenderOriginSubsystem* Origin =
		World != nullptr ? World->GetSubsystem<USpaceMMORenderOriginSubsystem>() : nullptr;

	if (Origin == nullptr || LengthMetres <= 0.0)
	{
		return;
	}

	const FVector Start = Origin->ToWorldLocation(StartPosition);
	const FVector End = Origin->ToWorldLocation(EndPosition);
	const FVector Direction = (End - Start).GetSafeNormal();

	// The head runs from the muzzle to the end; the tail follows a streak's length behind, never before the muzzle.
	const double Travelled = FMath::Min(LengthMetres, Elapsed * SpeedMetresPerSecond);
	const double Tail = FMath::Max(0.0, Travelled - StreakMetres);
	const double Shown = Travelled - Tail;

	SetActorLocation(Start);

	const bool bInFlight = Travelled < LengthMetres;

	Streak->SetVisibility(bInFlight && Shown > 0.0);

	if (Shown > 0.0)
	{
		// The cylinder stands on its middle along Z; laid along the shot and stretched to the streak.
		const FVector Middle = Direction * (Tail + Shown * 0.5) * 100.0;

		Streak->SetRelativeLocation(Middle);
		Streak->SetWorldRotation(FRotationMatrix::MakeFromZ(Direction).Rotator());
		Streak->SetRelativeScale3D(FVector(
			StreakWidthCentimetres / MeshUnits, StreakWidthCentimetres / MeshUnits, Shown * 100.0 / MeshUnits));
	}

	// Where it landed, if it landed on somebody: a quick flash that swells and is gone.
	const bool bFlashing = bHit && !bInFlight;

	Flash->SetVisibility(bFlashing);

	if (bFlashing)
	{
		const double Since = Elapsed - LengthMetres / SpeedMetresPerSecond;
		const double Size = FlashCentimetres * (0.4 + 0.6 * FMath::Clamp(Since / FlashSeconds, 0.0, 1.0));

		Flash->SetWorldLocation(End);
		Flash->SetRelativeScale3D(FVector(Size / MeshUnits));
	}
}
