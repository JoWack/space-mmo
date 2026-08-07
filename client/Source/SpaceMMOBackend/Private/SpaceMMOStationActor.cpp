#include "SpaceMMOStationActor.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "SpaceMMOBackendLog.h"
#include "SpaceMMORenderOrigin.h"
#include "UObject/ConstructorHelpers.h"

namespace SpaceMMOStation
{
	/** The engine cube is 100 cm on each edge. */
	constexpr double EngineCubeSizeCentimetres = 100.0;

	/**
	 * How large a station stands, in metres.
	 *
	 * Big enough to be unmistakable from the air on approach, since finding it is the point.
	 * Stated in metres and converted once, because a radius set in the wrong unit earlier in this
	 * project wrapped a two-metre shape in a twenty-metre body and nothing looked wrong.
	 */
	constexpr double SizeMetres = 60.0;
}

ASpaceMMOStationActor::ASpaceMMOStationActor()
{
	PrimaryActorTick.bCanEverTick = true;

	Hull = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Hull"));
	SetRootComponent(Hull);

	// No collision, like the planet, the ship and the deposits. Contact here is decided by
	// FPlanetTerrain rather than by Chaos, and docking is measured rather than collided — a solver
	// body would be a second opinion about where solid things are.
	Hull->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(
		TEXT("/Engine/BasicShapes/Cube.Cube"));

	if (CubeMesh.Succeeded())
	{
		Hull->SetStaticMesh(CubeMesh.Object);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	if (Material.Succeeded())
	{
		Hull->SetMaterial(0, Material.Object);
	}
}

void ASpaceMMOStationActor::Configure(
	const FBackendStation& InStation,
	const FPlanetConfig& InPlanet,
	const FPlanetTerrainConfig& InTerrain)
{
	Station = InStation;
	Planet = InPlanet;
	Terrain = InTerrain;

	const double Scale =
		(SpaceMMOStation::SizeMetres * 100.0) / SpaceMMOStation::EngineCubeSizeCentimetres;

	if (Hull != nullptr)
	{
		Hull->SetWorldScale3D(FVector(Scale));
	}

	if (!Station.bPlaced)
	{
		// Listed but unreachable. Drawing nothing is the honest rendering of "the server has no
		// position for this", and docking will refuse it for the same reason.
		SetActorHiddenInGame(true);

		UE_LOG(LogSpaceMMOBackend, Warning,
			TEXT("Station %s has no position; it will not be drawn or dockable."), *Station.Key);

		return;
	}

	// On a body: stand on the ground by asking the same height function the terrain mesh and the
	// physics ask. A transmitted altitude would be a second answer, free to disagree the moment
	// terrain configuration changed, and the station would end up buried or floating with nothing
	// in the payload looking wrong.
	SystemPosition = Station.bOnBody
		? FPlanetTerrain::SurfacePosition(Planet, Terrain, Station.Direction)
		: Station.Position;

	ApplyRenderTransform();

	UE_LOG(LogSpaceMMOBackend, Log,
		TEXT("Station %s (%s) at %s, docking range %.1f km."),
		*Station.Key,
		*Station.Kind,
		*SystemPosition.ToString(),
		Station.DockingRangeKilometres);
}

bool ASpaceMMOStationActor::IsWithinDockingRange(
	const FBackendStation& Station,
	const FSystemCoordinate& StationPosition,
	const FSystemCoordinate& Position)
{
	// An unplaced station is never dockable. Without this, a station the server could not locate
	// would sit at the system origin and accept anyone who happened to be near (0,0,0) — which is
	// exactly where a ship starts.
	if (!Station.bPlaced)
	{
		return false;
	}

	if (Station.DockingRangeKilometres <= 0.0)
	{
		return false;
	}

	const double Distance =
		(Position.Kilometres - StationPosition.Kilometres).Size();

	return Distance <= Station.DockingRangeKilometres;
}

void ASpaceMMOStationActor::BeginPlay()
{
	Super::BeginPlay();

	ApplyRenderTransform();
}

void ASpaceMMOStationActor::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const UWorld* World = GetWorld();

	const USpaceMMORenderOriginSubsystem* Origin =
		World != nullptr ? World->GetSubsystem<USpaceMMORenderOriginSubsystem>() : nullptr;

	// Only when the origin actually moves. A station does not travel, so between rebases its
	// Unreal transform is already correct.
	if (Origin == nullptr || Origin->GetRevision() == BuiltAtRevision)
	{
		return;
	}

	ApplyRenderTransform();
}

void ASpaceMMOStationActor::ApplyRenderTransform()
{
	const UWorld* World = GetWorld();

	const USpaceMMORenderOriginSubsystem* Origin =
		World != nullptr ? World->GetSubsystem<USpaceMMORenderOriginSubsystem>() : nullptr;

	if (Origin == nullptr || !Station.bPlaced)
	{
		return;
	}

	SetActorLocation(Origin->ToWorldLocation(SystemPosition));

	BuiltAtRevision = Origin->GetRevision();
}
