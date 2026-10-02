#include "SpaceMMOStationActor.h"

#include "Components/ChildActorComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "SpaceMMOBackendLog.h"
#include "SpaceMMOBoarding.h"
#include "SpaceMMODepositSettings.h"
#include "SpaceMMOPlanetActor.h"
#include "SpaceMMORenderOrigin.h"
#include "SpaceMMOAirspace.h"
#include "SpaceMMOSettlement.h"
#include "SpaceMMOStationMarkers.h"
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

	// Holds an assembled building when a kind names one. Empty otherwise, and costing nothing.
	Structure = CreateDefaultSubobject<UChildActorComponent>(TEXT("Structure"));
	Structure->SetupAttachment(Hull);

	// Absolute, so the Hull's fitting scale never reaches it. A Blueprint is authored at the size
	// the building actually is, and multiplying that by a number chosen to fit an engine cube into
	// twenty-five metres would be a second opinion about how big a hangar is.
	Structure->SetUsingAbsoluteScale(true);

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
	ApplyConfiguredLook();

	// A Blueprint building is drawn at the size it was built, so none of the fitting below applies
	// to it -- and the cube it would otherwise have been is not drawn at all.
	const bool bUsingBlueprint =
		Structure != nullptr && Structure->GetChildActorClass() != nullptr;

	const USpaceMMOStationSettings* const Settings = GetDefault<USpaceMMOStationSettings>();

	const double SizeMetres = Settings != nullptr
		? FStationAppearance::SizeMetresFor(*Settings, Station.Kind)
		: 25.0;

	const FBoxSphereBounds LocalBounds =
		(!bUsingBlueprint && Hull != nullptr && Hull->GetStaticMesh() != nullptr)
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

	// Altitude above the ground at its own direction, and the lift that was applied to get there.
	//
	// Placement asks FPlanetTerrain for the surface point, so the first number is zero by
	// construction and any other value means something moved the actor afterwards. The second is
	// the only thing that does. Printed together because a station that looks like it is floating
	// has three candidate explanations -- a lift meant for a centred pivot, a mesh whose origin is
	// not where its author thought, and standing on ground that is simply higher than the viewer's
	// -- and they are indistinguishable from a screenshot.
	const double SurfaceRadiusKm = Station.bOnBody
		? FPlanetTerrain::SurfaceRadiusKilometres(Planet, Terrain, Station.Direction)
		: 0.0;

	const double PlacedRadiusKm =
		(SystemPosition.Kilometres - Planet.Centre.Kilometres).Size();

	UE_LOG(LogSpaceMMOBackend, Log,
		TEXT("Station %s: placed %.1f m above the ground at its direction, lifted a further "
			"%.1f m for its pivot."),
		*Station.Key,
		Station.bOnBody ? (PlacedRadiusKm - SurfaceRadiusKm) * 1000.0 : 0.0,
		BaseLiftCentimetres / 100.0);

	UE_LOG(LogSpaceMMOBackend, Log,
		TEXT("Station %s (%s) at %s, docking range %.1f km, drawn as %s%s."),
		*Station.Key,
		*Station.Kind,
		*SystemPosition.ToString(),
		Station.DockingRangeKilometres,
		bUsingBlueprint
			? *GetNameSafe(Structure->GetChildActorClass())
			: ((Hull != nullptr && Hull->GetStaticMesh() != nullptr)
				? *Hull->GetStaticMesh()->GetName()
				: TEXT("<nothing>")),
		bUsingBlueprint
			? TEXT(" at its authored size")
			: *FString::Printf(TEXT(" at %.0f m"), SizeMetres));
}

void ASpaceMMOStationActor::ApplyConfiguredLook()
{
	const USpaceMMOStationSettings* const Settings = GetDefault<USpaceMMOStationSettings>();

	if (Settings == nullptr || Hull == nullptr)
	{
		return;
	}

	// An assembled building wins over a mesh: where a kind has both, the mesh is the placeholder
	// somebody has now replaced.
	const TSoftClassPtr<AActor> Assembled =
		FStationAppearance::BlueprintFor(*Settings, Station.Key, Station.Kind);

	if (!Assembled.IsNull())
	{
		// Synchronously, and deliberately, for the same reason the meshes are: stations are placed
		// once when the world is built, not per frame, and a building that popped in a second late
		// would have players flying through the space where it was about to be.
		UClass* const Class = Assembled.LoadSynchronous();

		if (Class == nullptr)
		{
			// Almost always a path missing its _C, which names the asset rather than the generated
			// class. Said out loud, because the station still stands there as a cube and nothing
			// else would suggest a building had been configured at all.
			UE_LOG(LogSpaceMMOBackend, Warning,
				TEXT("Station building '%s' for %s (%s) did not load; using the placeholder. "
					"A Blueprint path must end in _C."),
				*Assembled.ToString(), *Station.Key, *Station.Kind);
		}
		else if (Structure != nullptr)
		{
			Structure->SetChildActorClass(Class);

			// The placeholder is not drawn under an assembled building. Hidden rather than
			// stripped, so a Blueprint that fails to load on a later run still has something to
			// fall back to.
			Hull->SetVisibility(false);

			return;
		}
	}

	// Said out loud, on the path that does nothing.
	//
	// A kind with no building configured used to fall through here in silence, so a station drawn
	// as its placeholder looked identical whether nobody had configured a building, the path was
	// wrong, or -- three times now -- the game had been started before the ini was edited and was
	// never going to see it. Only the middle case warned. Now the absence is a line in the log,
	// and a stale session is visible without cross-checking file timestamps.
	if (Assembled.IsNull())
	{
		UE_LOG(LogSpaceMMOBackend, Log,
			TEXT("Station kind '%s' has no building configured; %s falls back to a mesh."),
			*Station.Kind, *Station.Key);
	}

	const TSoftObjectPtr<UStaticMesh> Configured =
		FStationAppearance::MeshFor(*Settings, Station.Key, Station.Kind);

	if (Configured.IsNull())
	{
		// Keeps the cube the constructor attached. An unmapped kind is still dockable, and a
		// station that rendered as nothing would look exactly like one that was never placed.
		//
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

bool ASpaceMMOStationActor::GroundPositionBeside(
	const double OffsetKilometres,
	const double LiftKilometres,
	FSystemCoordinate& OutPosition) const
{
	UWorld* World = GetWorld();

	if (World == nullptr)
	{
		return false;
	}

	// The body this station stands on, which is the one nearest it -- not the first planet the
	// actor iterator returns. This is where a pilot is put down when they leave a station, so the
	// cheap version would step somebody out of Terra Outpost onto the Capital's terrain, two
	// hundred kilometres away, on ground that is not underneath them (task 157).
	const ASpaceMMOPlanetActor* const Standing =
		ASpaceMMOPlanetActor::NearestTo(World, SystemPosition);

	if (Standing == nullptr)
	{
		return false;
	}

	const FPlanetConfig& Body = Standing->GetPlanetConfig();

	const FVector Up = (SystemPosition.Kilometres - Body.Centre.Kilometres).GetSafeNormal();

	if (Up.IsNearlyZero())
	{
		return false;
	}

	// StepOutPosition flattens whatever direction it is given into the tangent plane, so a
	// world axis is a perfectly good thing to hand it -- except where that axis is the up it
	// is being flattened against, which is every station near a pole.
	const FSystemCoordinate Beside = FBoarding::StepOutPosition(
		SystemPosition, Up, FVector::UpVector, OffsetKilometres);

	const FVector BesideDirection =
		(Beside.Kilometres - Body.Centre.Kilometres).GetSafeNormal();

	if (BesideDirection.IsNearlyZero())
	{
		return false;
	}

	OutPosition = FSystemCoordinate(
		FPlanetTerrain::SurfacePosition(
			Body, Standing->GetTerrainConfig(), BesideDirection).Kilometres
		+ (BesideDirection * LiftKilometres));

	return true;
}

const ASpaceMMOSettlementActor* ASpaceMMOStationActor::GetSettlement() const
{
	return Structure != nullptr ? Cast<ASpaceMMOSettlementActor>(Structure->GetChildActor()) : nullptr;
}

int32 ASpaceMMOStationActor::GetBerthCount() const
{
	return BerthPlaces().Num();
}

void ASpaceMMOStationActor::GetDockMarks(TArray<FSpaceMMODockMark>& OutDocks) const
{
	OutDocks.Reset();

	for (const FBerthPlace& Place : BerthPlaces())
	{
		FSpaceMMODockMark Mark;
		Mark.ShortName = Place.ShortName;
		Mark.Pad = Place.Pad;

		// The pad and its rim, which is exactly how far across IsAtBerth reaches -- so a readout that says
		// READY within this of the pad's centre is never followed by a refusal.
		Mark.ReachKilometres = Place.PadRadiusKilometres + FSpaceMMOAirspace::BerthReachKilometres;

		OutDocks.Add(Mark);
	}
}

TArray<ASpaceMMOStationActor::FBerthPlace> ASpaceMMOStationActor::BerthPlaces() const
{
	TArray<FBerthPlace> Places;

	const ASpaceMMOSettlementActor* const Settlement = GetSettlement();
	const UWorld* const World = GetWorld();

	const USpaceMMORenderOriginSubsystem* const Origin =
		World != nullptr ? World->GetSubsystem<USpaceMMORenderOriginSubsystem>() : nullptr;

	if (Settlement == nullptr || Origin == nullptr || !Station.bPlaced || !Station.bOnBody)
	{
		return Places;
	}

	TArray<const USpaceMMOBerthComponent*> Berths;
	Settlement->GetBerths(Berths);

	const FVector Up = Station.Direction.GetSafeNormal();
	const FVector Centre = Settlement->GetActorLocation();

	for (const USpaceMMOBerthComponent* Berth : Berths)
	{
		// Read off the built component rather than recomputed from the station, so a berth is where it
		// is drawn: the same pad a pilot can see is the one they are measured against.
		const FVector Pad = Berth->GetComponentLocation();

		FBerthPlace Place;
		Place.Pad = FSystemCoordinate::FromLocalCentimetres(Pad, Origin->GetRenderOrigin());
		Place.Outward = FVector::VectorPlaneProject(Pad - Centre, Up).GetSafeNormal();
		Place.PadRadiusKilometres = Berth->PadRadiusMetres / 1000.0;
		Place.ShortName = Berth->ShortName;

		Places.Add(Place);
	}

	return Places;
}

int32 ASpaceMMOStationActor::NearestBerth(const TArray<FBerthPlace>& Berths, const FSystemCoordinate& Near)
{
	int32 Best = INDEX_NONE;
	double BestDistance = 0.0;

	for (int32 Index = 0; Index < Berths.Num(); ++Index)
	{
		const double Distance = (Berths[Index].Pad.Kilometres - Near.Kilometres).SizeSquared();

		if (Best == INDEX_NONE || Distance < BestDistance)
		{
			Best = Index;
			BestDistance = Distance;
		}
	}

	return Best;
}

bool ASpaceMMOStationActor::IsAtBerth(const FSystemCoordinate& ShipPosition) const
{
	const FVector Up = Station.Direction.GetSafeNormal();

	for (const FBerthPlace& Place : BerthPlaces())
	{
		if (FSpaceMMOAirspace::IsAtBerth(Place.Pad, Up, Place.PadRadiusKilometres, ShipPosition))
		{
			return true;
		}
	}

	return false;
}

bool ASpaceMMOStationActor::PilotArrivalNear(
	const FSystemCoordinate& Near, const double OffsetKilometres, FSystemCoordinate& OutPosition) const
{
	const TArray<FBerthPlace> Places = BerthPlaces();
	const int32 Index = NearestBerth(Places, Near);

	if (Index == INDEX_NONE)
	{
		return GroundPositionBeside(OffsetKilometres, 0.0, OutPosition);
	}

	// On the dock's floor, which stands above the levelled ground -- not "on the terrain beside the
	// station", which for Borlash is under the platform. The character drops the half metre onto it.
	const FBerthPlace& Place = Places[Index];

	OutPosition = FSpaceMMOAirspace::PilotArrivalBesidePad(
		Place.Pad, Place.Outward, Station.Direction, Place.PadRadiusKilometres);

	return true;
}

bool ASpaceMMOStationActor::ShipPlacementNear(
	const FSystemCoordinate& Near,
	const double OffsetKilometres,
	const double LiftKilometres,
	FSystemCoordinate& OutPosition) const
{
	const TArray<FBerthPlace> Places = BerthPlaces();
	const int32 Index = NearestBerth(Places, Near);

	if (Index == INDEX_NONE)
	{
		return GroundPositionBeside(OffsetKilometres, LiftKilometres, OutPosition);
	}

	// On the pad itself. A ship brought to Borlash comes to a docking station, never into the city.
	OutPosition = FSystemCoordinate(
		Places[Index].Pad.Kilometres + (Station.Direction.GetSafeNormal() * LiftKilometres));

	return true;
}

void ASpaceMMOStationActor::RegisterSettlementAirspace()
{
	const ASpaceMMOSettlementActor* const Settlement = GetSettlement();
	UWorld* const World = GetWorld();

	if (Settlement == nullptr || World == nullptr || !Station.bPlaced || !Station.bOnBody)
	{
		return;
	}

	bSettlementRegistered = true;

	USpaceMMOAirspaceSubsystem* const Airspace = World->GetSubsystem<USpaceMMOAirspaceSubsystem>();

	if (Airspace == nullptr)
	{
		return;
	}

	FSpaceMMOAirspaceZone Zone;
	Zone.Key = Station.Key;
	Zone.Name = Station.Name;
	Zone.Origin = SystemPosition;
	Zone.Rotation = FRotationMatrix::MakeFromZ(Station.Direction.GetSafeNormal()).ToQuat();
	Zone.FloorKilometres = Settlement->FloorMetres / 1000.0;
	Zone.NoFlyRadiusKilometres = Settlement->NoFlyRadiusMetres / 1000.0;
	Zone.NoFlyCeilingKilometres = Settlement->NoFlyCeilingMetres / 1000.0;
	Zone.PlatformHalfWidthKilometres = Settlement->PlatformHalfWidthMetres / 1000.0;

	for (const FBerthPlace& Place : BerthPlaces())
	{
		FSpaceMMOAirspaceBerth Berth;
		Berth.Pad = Place.Pad;
		Berth.PadRadiusKilometres = Place.PadRadiusKilometres;

		Zone.Berths.Add(Berth);
	}

	Airspace->SetZone(Zone);

	// Every berth named with where it stands, so "I cannot dock" has its numbers in the log before
	// anybody asks: how far the ship was from which pad.
	for (const FBerthPlace& Place : BerthPlaces())
	{
		UE_LOG(LogSpaceMMOBackend, Log,
			TEXT("  berth at %s, pad %.0f m across, %.0f m from %s's centre."),
			*Place.Pad.ToString(),
			Place.PadRadiusKilometres * 2000.0,
			(Place.Pad.Kilometres - SystemPosition.Kilometres).Size() * 1000.0,
			*Station.Name);
	}
}

void ASpaceMMOStationActor::BeginPlay()
{
	Super::BeginPlay();

	ApplyRenderTransform();
}

void ASpaceMMOStationActor::EndPlay(const EEndPlayReason::Type Reason)
{
	if (bSettlementRegistered)
	{
		if (UWorld* const World = GetWorld())
		{
			if (USpaceMMOAirspaceSubsystem* const Airspace = World->GetSubsystem<USpaceMMOAirspaceSubsystem>())
			{
				Airspace->RemoveZone(Station.Key);
			}
		}
	}

	Super::EndPlay(Reason);
}

void ASpaceMMOStationActor::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const UWorld* World = GetWorld();

	const USpaceMMORenderOriginSubsystem* Origin =
		World != nullptr ? World->GetSubsystem<USpaceMMORenderOriginSubsystem>() : nullptr;

	// What is actually on the component, said once the renderer has it (task 162).
	//
	// Terra Outpost was placed, positioned, scaled and unhidden by every line of code that could
	// be read, and drew nothing from 43 m away. The terrain patch has carried this measurement since
	// task 84 for exactly that situation: a station that is correct in every input and invisible in
	// the world is distinguished from one that is genuinely not drawn by reading the proxy, the
	// bounds and the material off the built thing.
	if (!bReportedDrawState && Hull != nullptr && Hull->SceneProxy != nullptr)
	{
		bReportedDrawState = true;

		const bool bBuilding =
			Structure != nullptr && Structure->GetChildActorClass() != nullptr;

		const UStaticMesh* const Mesh = Hull->GetStaticMesh();
		const UMaterialInterface* const Material = Hull->GetMaterial(0);

		UE_LOG(LogSpaceMMOBackend, Log,
			TEXT("Station %s draw state: hull visible %d, actor hidden %d, registered %d, has proxy "
				"%d, building %s, mesh %s, material %s, scale %s, world %s, bounds origin %s extent "
				"%s."),
			*Station.Key,
			Hull->IsVisible() ? 1 : 0,
			IsHidden() ? 1 : 0,
			Hull->IsRegistered() ? 1 : 0,
			Hull->SceneProxy != nullptr ? 1 : 0,
			bBuilding ? *GetNameSafe(Structure->GetChildActor()) : TEXT("none"),
			*GetNameSafe(Mesh),
			*GetNameSafe(Material),
			*Hull->GetComponentScale().ToCompactString(),
			*GetActorLocation().ToCompactString(),
			*Hull->Bounds.Origin.ToCompactString(),
			*Hull->Bounds.BoxExtent.ToCompactString());
	}

	// The settlement's airspace, once there is a settlement to read it from (task 169).
	if (!bSettlementRegistered && GetSettlement() != nullptr)
	{
		RegisterSettlementAirspace();
	}

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
