#include "SpaceMMODepositActor.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "SpaceMMOBackendLog.h"
#include "SpaceMMORenderOrigin.h"
#include "UObject/ConstructorHelpers.h"

namespace SpaceMMODeposit
{
	/** The engine cylinder is 100 cm tall and 100 cm across. */
	constexpr double EngineCylinderSizeCentimetres = 100.0;

	/**
	 * How big a deposit stands, in metres.
	 *
	 * A character is about two metres, so this is a shape you walk up to rather than one you have
	 * to hunt for. Worth stating in metres and converting once: a hull radius was set to 0.02
	 * kilometres earlier in this project on the assumption it was metres, and wrapped a two-metre
	 * cone in a twenty-metre collision sphere.
	 */
	constexpr double WidthMetres = 2.0;
	constexpr double HeightMetres = 3.0;
}

ASpaceMMODepositActor::ASpaceMMODepositActor()
{
	PrimaryActorTick.bCanEverTick = true;

	Marker = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Marker"));
	SetRootComponent(Marker);

	// No collision, for the same reason the planet and the ship have none: contact here is decided
	// by FPlanetTerrain, not by Chaos, and a solver body would be a second opinion about where
	// solid things are. Gathering range is measured, not collided.
	Marker->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));

	if (CylinderMesh.Succeeded())
	{
		Marker->SetStaticMesh(CylinderMesh.Object);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	if (Material.Succeeded())
	{
		Marker->SetMaterial(0, Material.Object);
	}
}

void ASpaceMMODepositActor::Configure(
	const FBackendResourceNode& InNode,
	const FPlanetConfig& InPlanet,
	const FPlanetTerrainConfig& InTerrain)
{
	Node = InNode;
	Planet = InPlanet;
	Terrain = InTerrain;

	// The one place a direction becomes a position. Both machines run this same call on the same
	// inputs, which is why neither has to be told the answer.
	SurfacePosition = FPlanetTerrain::SurfacePosition(Planet, Terrain, Node.Direction);

	ApplyRenderTransform();
}

void ASpaceMMODepositActor::BeginPlay()
{
	Super::BeginPlay();

	ApplyRenderTransform();

	UE_LOG(LogSpaceMMOBackend, Log,
		TEXT("Deposit %s (%s x%d) at %s, %.4f km above the nominal radius."),
		*Node.Key,
		*Node.ItemKey,
		Node.QuantityMax,
		*SurfacePosition.ToString(),
		(SurfacePosition.Kilometres - Planet.Centre.Kilometres).Size() - Planet.RadiusKilometres);
}

void ASpaceMMODepositActor::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const UWorld* World = GetWorld();

	const USpaceMMORenderOriginSubsystem* Origin =
		World != nullptr ? World->GetSubsystem<USpaceMMORenderOriginSubsystem>() : nullptr;

	// A deposit does not move, so between rebases its transform is already right. Only a change of
	// what "world zero" means can invalidate it.
	if (Origin == nullptr || Origin->GetRevision() == BuiltAtRevision)
	{
		return;
	}

	ApplyRenderTransform();
}

void ASpaceMMODepositActor::ApplyRenderTransform()
{
	const UWorld* World = GetWorld();

	const USpaceMMORenderOriginSubsystem* Origin =
		World != nullptr ? World->GetSubsystem<USpaceMMORenderOriginSubsystem>() : nullptr;

	if (Origin == nullptr || Marker == nullptr)
	{
		return;
	}

	// Stand along the ground's normal rather than along the radius. On a slope those differ, and it
	// is the difference between a deposit growing out of a hillside and one skewered through it at
	// whatever angle the planet's centre happens to be.
	const FVector Up = FPlanetTerrain::SurfaceNormal(Planet, Terrain, Node.Direction);

	// The cylinder's axis is Z, so aligning Z to the surface normal stands it up. Any rotation
	// about that axis is as good as any other for a rock.
	const FQuat Rotation = FQuat::FindBetweenNormals(FVector::UpVector, Up);

	const double Width =
		(SpaceMMODeposit::WidthMetres * 100.0) / SpaceMMODeposit::EngineCylinderSizeCentimetres;

	const double Height =
		(SpaceMMODeposit::HeightMetres * 100.0) / SpaceMMODeposit::EngineCylinderSizeCentimetres;

	// The engine cylinder is centred on its origin, so half of it would be underground. Lifting by
	// half its height puts its base on the surface point rather than its middle.
	const FVector BaseOffset = Up * (SpaceMMODeposit::HeightMetres * 100.0 * 0.5);

	SetActorLocation(Origin->ToWorldLocation(SurfacePosition) + BaseOffset);
	SetActorRotation(Rotation);

	Marker->SetWorldScale3D(FVector(Width, Width, Height));

	BuiltAtRevision = Origin->GetRevision();
}
