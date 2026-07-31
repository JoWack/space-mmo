#include "SpaceMMOPlanetActor.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "SpaceMMOLog.h"
#include "SpaceMMORenderOrigin.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** The engine sphere is 100 cm across, so its radius is 50 cm. */
	constexpr double EngineSphereRadiusCentimetres = 50.0;
}

ASpaceMMOPlanetActor::ASpaceMMOPlanetActor()
{
	PrimaryActorTick.bCanEverTick = true;

	Surface = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Surface"));
	SetRootComponent(Surface);

	// Collision is off for the same reason the ship's is: position and contact are decided by the
	// planet physics, not by Chaos. A collision body scaled to twenty kilometres would also be a
	// remarkably bad thing to hand the solver.
	Surface->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(
		TEXT("/Engine/BasicShapes/Sphere.Sphere"));

	if (SphereMesh.Succeeded())
	{
		Surface->SetStaticMesh(SphereMesh.Object);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> SphereMaterial(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	if (SphereMaterial.Succeeded())
	{
		Surface->SetMaterial(0, SphereMaterial.Object);
	}
}

void ASpaceMMOPlanetActor::BeginPlay()
{
	Super::BeginPlay();

	ApplyRenderTransform();

	UE_LOG(LogSpaceMMO, Log,
		TEXT("Planet at %s, radius %.1f km, surface gravity %.0f cm/s^2, atmosphere %.1f km."),
		*Planet.Centre.ToString(),
		Planet.RadiusKilometres,
		Planet.SurfaceGravity,
		Planet.AtmosphereHeightKilometres);
}

void ASpaceMMOPlanetActor::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const UWorld* World = GetWorld();

	const USpaceMMORenderOriginSubsystem* Origin =
		World != nullptr ? World->GetSubsystem<USpaceMMORenderOriginSubsystem>() : nullptr;

	// Only when the origin actually moves. A planet does not orbit yet, so between rebases its
	// Unreal transform is already correct.
	if (Origin == nullptr || Origin->GetRevision() == BuiltAtRevision)
	{
		return;
	}

	ApplyRenderTransform();
}

void ASpaceMMOPlanetActor::SetPlanetConfig(const FPlanetConfig& NewConfig)
{
	Planet = NewConfig;

	ApplyRenderTransform();
}

void ASpaceMMOPlanetActor::ApplyRenderTransform()
{
	const UWorld* World = GetWorld();

	const USpaceMMORenderOriginSubsystem* Origin =
		World != nullptr ? World->GetSubsystem<USpaceMMORenderOriginSubsystem>() : nullptr;

	if (Origin == nullptr || Surface == nullptr)
	{
		return;
	}

	const double RadiusCentimetres =
		Planet.RadiusKilometres * SpaceMMO::Coordinates::CentimetresPerKilometre;

	SetActorLocation(Origin->ToWorldLocation(Planet.Centre));
	Surface->SetWorldScale3D(FVector(RadiusCentimetres / EngineSphereRadiusCentimetres));

	BuiltAtRevision = Origin->GetRevision();
}
