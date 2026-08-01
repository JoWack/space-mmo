#include "SpaceMMOPlanetActor.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "SpaceMMOLog.h"
#include "GameFramework/PlayerController.h"
#include "SpaceMMOPlanetPatch.h"
#include "SpaceMMORenderOrigin.h"
#include "SpaceMMOTerrainPatchActor.h"
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

	// Terrain is checked every frame regardless of the origin, because it follows the viewer
	// rather than the render window — a player can walk a long way without a single rebase.
	UpdateTerrainPatch();

	// Only when the origin actually moves. A planet does not orbit yet, so between rebases its
	// Unreal transform is already correct.
	if (Origin == nullptr || Origin->GetRevision() == BuiltAtRevision)
	{
		return;
	}

	ApplyRenderTransform();
}

bool ASpaceMMOPlanetActor::TryGetViewerPosition(FSystemCoordinate& OutPosition) const
{
	const UWorld* World = GetWorld();

	if (World == nullptr)
	{
		return false;
	}

	const USpaceMMORenderOriginSubsystem* Origin =
		World->GetSubsystem<USpaceMMORenderOriginSubsystem>();

	const APlayerController* Controller = World->GetFirstPlayerController();

	if (Origin == nullptr || Controller == nullptr)
	{
		return false;
	}

	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;

	const_cast<APlayerController*>(Controller)->GetPlayerViewPoint(ViewLocation, ViewRotation);

	// World location is by construction relative to the render origin, so this reconstructs the
	// viewer's system position exactly — and works for any viewer, without the planet needing to
	// know that ships exist.
	OutPosition = FSystemCoordinate(
		Origin->GetRenderOrigin().Kilometres
		+ (ViewLocation / SpaceMMO::Coordinates::CentimetresPerKilometre));

	return true;
}

void ASpaceMMOPlanetActor::UpdateTerrainPatch()
{
	// A dedicated server has no viewer and no renderer. It knows the ground through
	// FPlanetTerrain, which is all it needs to decide where a player may stand.
	if (IsRunningDedicatedServer())
	{
		return;
	}

	FSystemCoordinate ViewerPosition;

	if (!TryGetViewerPosition(ViewerPosition))
	{
		return;
	}

	// Fed its own previous value, so the hysteresis in ClassifyProximity has something to work
	// against — without it a viewer hovering on the atmosphere boundary would build and destroy
	// the same patch every frame.
	ViewerProximity = FPlanetPhysics::ClassifyProximity(Planet, ViewerPosition, ViewerProximity);

	if (ViewerProximity == EPlanetProximity::Orbital)
	{
		if (TerrainPatch != nullptr)
		{
			UE_LOG(LogSpaceMMO, Log, TEXT("Left the atmosphere; releasing terrain patch."));

			TerrainPatch->Destroy();
			TerrainPatch = nullptr;
			PatchDirection = FVector::ZeroVector;
		}

		return;
	}

	const FVector ViewerDirection =
		(ViewerPosition.Kilometres - Planet.Centre.Kilometres).GetSafeNormal();

	if (TerrainPatch != nullptr
		&& !FPlanetPatch::ShouldRebuild(PatchDirection, ViewerDirection, PatchAngularRadiusDegrees))
	{
		return;
	}

	UWorld* World = GetWorld();

	if (World == nullptr)
	{
		return;
	}

	if (TerrainPatch == nullptr)
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		TerrainPatch = World->SpawnActor<ASpaceMMOTerrainPatchActor>(
			ASpaceMMOTerrainPatchActor::StaticClass(), FTransform::Identity, SpawnParameters);
	}

	if (TerrainPatch == nullptr)
	{
		return;
	}

	PatchDirection = ViewerDirection;

	TerrainPatch->SetAngularRadiusDegrees(PatchAngularRadiusDegrees);
	TerrainPatch->BuildPatch(Planet, TerrainConfig, ViewerDirection);
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
