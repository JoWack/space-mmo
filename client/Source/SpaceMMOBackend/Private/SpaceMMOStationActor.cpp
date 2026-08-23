#include "SpaceMMOStationActor.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "SpaceMMOBackendLog.h"
#include "SpaceMMODepositSettings.h"
#include "SpaceMMORenderOrigin.h"
#include "SpaceMMOStationSettings.h"
#include "UObject/ConstructorHelpers.h"

namespace SpaceMMOStation
{
	/**
	 * How large a station stands is now per kind, in USpaceMMOStationSettings.
	 *
	 * It was one compiled constant of twenty-five metres for everything, which was judged against
	 * the horizon and is still the default there — this planet has a radius of twenty kilometres,
	 * so from eye height the horizon is about two hundred and sixty metres away, and a sixty-metre
	 * building subtends thirteen degrees at that range and reads as a structure the size of the
	 * visible world. That was exactly how the first one looked.
	 *
	 * What the single value could not express is that a spaceport and somebody's house are not the
	 * same size, which is most of what made every station read as the same building.
	 *
	 * Sizes are stated in metres and converted once, because a radius set in the wrong unit earlier
	 * in this project wrapped a two-metre shape in a twenty-metre body and nothing looked wrong.
	 */
	constexpr double CentimetresPerMetre = 100.0;
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

	// Before the scale is worked out, because the scale is fitted to whatever mesh ends up on the
	// component and the placeholder cube is not the same size as an authored building.
	ApplyConfiguredMesh();

	const USpaceMMOStationSettings* const Settings = GetDefault<USpaceMMOStationSettings>();

	const double SizeMetres = Settings != nullptr
		? FStationAppearance::SizeMetresFor(*Settings, Station.Kind)
		: 25.0;

	const FBoxSphereBounds LocalBounds =
		(Hull != nullptr && Hull->GetStaticMesh() != nullptr)
			? Hull->GetStaticMesh()->GetBounds()
			: FBoxSphereBounds(ForceInit);

	const double Scale = FStationAppearance::UniformScaleForSize(
		LocalBounds.BoxExtent, SizeMetres * SpaceMMOStation::CentimetresPerMetre);

	if (Hull != nullptr)
	{
		Hull->SetWorldScale3D(FVector(Scale));

		// How far to raise it so its base rests on the ground rather than its middle sitting in
		// it. The surface position names a point on the ground, and an actor placed there with a
		// centred pivot is half buried — half of a twenty-five metre cube is twelve metres of
		// station underground, which is most of why the first one did not read as standing on
		// anything.
		//
		// The same helper the deposits use, so both handle either pivot convention without being
		// told which the mesh was authored with.
		BaseLiftCentimetres =
			FDepositPlacement::BaseLift(LocalBounds.Origin, LocalBounds.BoxExtent, Scale);
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
		TEXT("Station %s (%s) at %s, docking range %.1f km, drawn as %s at %.0f m."),
		*Station.Key,
		*Station.Kind,
		*SystemPosition.ToString(),
		Station.DockingRangeKilometres,
		(Hull != nullptr && Hull->GetStaticMesh() != nullptr)
			? *Hull->GetStaticMesh()->GetName()
			: TEXT("<nothing>"),
		SizeMetres);
}

void ASpaceMMOStationActor::ApplyConfiguredMesh()
{
	const USpaceMMOStationSettings* const Settings = GetDefault<USpaceMMOStationSettings>();

	if (Settings == nullptr || Hull == nullptr)
	{
		return;
	}

	const TSoftObjectPtr<UStaticMesh> Configured =
		FStationAppearance::MeshFor(*Settings, Station.Key, Station.Kind);

	if (Configured.IsNull())
	{
		// Keeps the cube the constructor attached. An unmapped kind is still dockable, and a
		// station that rendered as nothing would look exactly like one that was never placed.
		UE_LOG(LogSpaceMMOBackend, Log,
			TEXT("Station kind '%s' has no configured mesh; %s keeps the placeholder cube."),
			*Station.Kind, *Station.Key);

		return;
	}

	// Loaded synchronously, and deliberately, for the same reason deposits are: stations are placed
	// once when the world is built rather than per frame, and a building that popped in a second
	// late would have players flying through the space where it was about to be.
	UStaticMesh* const Mesh = Configured.LoadSynchronous();

	if (Mesh == nullptr)
	{
		UE_LOG(LogSpaceMMOBackend, Warning,
			TEXT("Station mesh for '%s' (%s) is configured but failed to load; using the cube."),
			*Station.Key, *Station.Kind);

		return;
	}

	Hull->SetStaticMesh(Mesh);

	// The mesh brings its own materials. The placeholder material the constructor set is for the
	// engine cube, and leaving it on would repaint an authored building in flat grey.
	Hull->EmptyOverrideMaterials();
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

	// Lifted along local up, which on a sphere is the direction from the planet's centre — not
	// world Z. A station on the far side of the planet lifted along Z would be pushed sideways
	// into the ground.
	//
	// Deep-space stations are not lifted at all: there is no ground under them, and the anchor is
	// already the middle of the structure rather than a point on a surface.
	if (!Station.bOnBody)
	{
		// Nothing to stand on and nothing to be upright with respect to.
		SetActorLocation(Origin->ToWorldLocation(SystemPosition));

		BuiltAtRevision = Origin->GetRevision();

		return;
	}

	const FVector Up = Station.Direction.GetSafeNormal();

	// Turned to face away from the planet's centre, not left pointing along world Z.
	//
	// On a sphere "up" is a different direction at every point, and an unrotated box is upright
	// only at the one place where the local up happens to be world Z. The capital's station sits
	// where up is roughly negative X, so it was lying on its side — which does more to make a
	// building look like it is not on the ground than being the wrong size does.
	SetActorLocationAndRotation(
		Origin->ToWorldLocation(SystemPosition) + (Up * BaseLiftCentimetres),
		FRotationMatrix::MakeFromZ(Up).ToQuat());

	BuiltAtRevision = Origin->GetRevision();
}
