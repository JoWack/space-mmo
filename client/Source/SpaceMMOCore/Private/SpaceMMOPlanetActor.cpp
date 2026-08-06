#include "SpaceMMOPlanetActor.h"

#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Materials/MaterialInterface.h"
#include "SpaceMMOLog.h"
#include "GameFramework/PlayerController.h"
#include "SpaceMMOPlanetGlobe.h"
#include "SpaceMMOPlanetPatch.h"
#include "SpaceMMORenderOrigin.h"
#include "SpaceMMOTerrainPatchActor.h"
#include "UObject/ConstructorHelpers.h"

using namespace UE::Geometry;

namespace
{
	/**
	 * Whether the globe hides while a terrain patch exists.
	 *
	 * <strong>Off, because the patch does not draw and nobody yet knows why.</strong> Its component
	 * reports thirty-two thousand triangles, a real material, unit scale, visible, and bounds whose
	 * sphere contains the camera — which in Unreal means it cannot even be frustum-culled. It is
	 * still not on screen, while the globe built the same way from the same height function through
	 * the same component type is. Hiding the globe under it therefore replaced a planet with
	 * nothing, which is what the black was.
	 *
	 * So the globe carries the ground for now and the patch is a refinement that currently refines
	 * nothing. Set to 1 to reproduce the fault while chasing it.
	 */
	float GHideGlobeUnderPatch = 0.0f;

	FAutoConsoleVariableRef CVarHideGlobeUnderPatch(
		TEXT("SpaceMMO.HideGlobeUnderPatch"),
		GHideGlobeUnderPatch,
		TEXT("1 hides the whole-planet mesh while a terrain patch exists, 0 always draws it. "
			"If ground appears at 0, the patch is what is not drawing."),
		ECVF_Default);

	/**
	 * How much the patch's width may drift from what the altitude asks for before it is rebuilt.
	 *
	 * Without a threshold the patch would regenerate every frame of a descent, since the ideal
	 * width changes continuously with altitude. A quarter is loose enough that a rebuild is an
	 * occasional event and tight enough that the patch never falls far short of the horizon.
	 */
	constexpr double PatchWidthDriftFraction = 0.25;
}

ASpaceMMOPlanetActor::ASpaceMMOPlanetActor()
{
	PrimaryActorTick.bCanEverTick = true;

	Surface = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("Surface"));
	SetRootComponent(Surface);

	// Collision is off for the same reason the ship's is: position and contact are decided by the
	// planet physics, not by Chaos. A collision body scaled to twenty kilometres would also be a
	// remarkably bad thing to hand the solver.
	Surface->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// A sibling of the globe, not a child, so hiding one never hides the other.
	GroundPatch = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("GroundPatch"));
	GroundPatch->SetupAttachment(Surface);
	GroundPatch->SetUsingAbsoluteLocation(true);
	GroundPatch->SetUsingAbsoluteRotation(true);
	GroundPatch->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GroundPatch->SetVisibility(false);

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> SphereMaterial(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	if (SphereMaterial.Succeeded())
	{
		Surface->SetMaterial(0, SphereMaterial.Object);
		GroundPatch->SetMaterial(0, SphereMaterial.Object);
	}
}

void ASpaceMMOPlanetActor::BeginPlay()
{
	Super::BeginPlay();

	BuildGlobe();

	ApplyRenderTransform();

	UE_LOG(LogSpaceMMO, Log,
		TEXT("Planet at %s, radius %.1f km, surface gravity %.0f cm/s^2, atmosphere %.1f km."),
		*Planet.Centre.ToString(),
		Planet.RadiusKilometres,
		Planet.SurfaceGravity,
		Planet.AtmosphereHeightKilometres);

	// Relief as a fraction of the radius, because that is what decides whether a planet reads as a
	// world or as a golf ball, and it is not something anyone can judge from either number alone.
	// Earth's tallest mountain is 0.14% of its radius.
	UE_LOG(LogSpaceMMO, Log,
		TEXT("Terrain rises %.3f km, which is %.2f%% of the radius (Earth is 0.14%%)."),
		TerrainConfig.MaxElevationKilometres,
		Planet.RadiusKilometres > 0.0
			? (TerrainConfig.MaxElevationKilometres / Planet.RadiusKilometres) * 100.0
			: 0.0);
}

void ASpaceMMOPlanetActor::BuildGlobe()
{
	// A dedicated server has no renderer, and a hundred thousand triangles per planet is a large
	// amount of nothing to hold. It knows the ground through FPlanetTerrain, which is all it needs.
	if (IsRunningDedicatedServer() || Surface == nullptr)
	{
		return;
	}

	// Configuration arrives one setter at a time between SpawnActorDeferred and FinishSpawning, so
	// building on each call would tessellate the planet twice from settings that are still
	// half-applied before BeginPlay tessellates it a third time from the real ones.
	if (!HasActorBegunPlay())
	{
		return;
	}

	const FPlanetGlobeMesh Globe = FPlanetGlobe::Build(Planet, TerrainConfig, GlobeConfig);

	if (!Globe.IsValid())
	{
		UE_LOG(LogSpaceMMO, Warning, TEXT("Planet globe generated nothing."));

		return;
	}

	FDynamicMesh3 Mesh;
	Mesh.EnableAttributes();

	for (const FVector& Position : Globe.Positions)
	{
		Mesh.AppendVertex(FVector3d(Position));
	}

	for (int32 Index = 0; Index + 2 < Globe.Triangles.Num(); Index += 3)
	{
		Mesh.AppendTriangle(
			Globe.Triangles[Index], Globe.Triangles[Index + 1], Globe.Triangles[Index + 2]);
	}

	if (FDynamicMeshNormalOverlay* Normals =
		Mesh.Attributes() != nullptr ? Mesh.Attributes()->PrimaryNormals() : nullptr)
	{
		Normals->ClearElements();

		TArray<int32> Elements;
		Elements.Reserve(Globe.Normals.Num());

		for (const FVector& Normal : Globe.Normals)
		{
			Elements.Add(Normals->AppendElement(FVector3f(Normal)));
		}

		for (const int32 TriangleId : Mesh.TriangleIndicesItr())
		{
			const FIndex3i Triangle = Mesh.GetTriangle(TriangleId);

			Normals->SetTriangle(
				TriangleId,
				FIndex3i(Elements[Triangle.A], Elements[Triangle.B], Elements[Triangle.C]));
		}
	}

	Surface->SetMesh(MoveTemp(Mesh));
	Surface->NotifyMeshUpdated();

	UE_LOG(LogSpaceMMO, Log,
		TEXT("Planet globe: %d triangles, a vertex every %.0f m of ground."),
		Globe.Triangles.Num() / 3,
		(FMath::DegreesToRadians(90.0 / FMath::Max(GlobeConfig.Resolution - 1, 1))
			* Planet.RadiusKilometres) * 1000.0);
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

double ASpaceMMOPlanetActor::PatchDegreesForAltitude(
	const FPlanetConfig& Planet,
	const double AltitudeKilometres,
	const double MinimumDegrees,
	const double MaximumDegrees)
{
	// A margin past the horizon, so the edge of the patch is over it rather than visibly at it.
	constexpr double HorizonMargin = 1.2;

	const double Cap =
		FPlanetGlobe::VisibleCapDegrees(Planet, AltitudeKilometres) * HorizonMargin;

	return FMath::Clamp(Cap, MinimumDegrees, FMath::Max(MinimumDegrees, MaximumDegrees));
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

	// A controller with no pawn has no view worth reading. On the frame a pawn is possessed it is
	// still at the world origin, so the view point reports the render origin — which on a planet
	// forty kilometres away classifies as orbital and throws away the terrain the player is
	// standing on. The log shows it happening on exactly the frames "Ship ready" and
	// "Character ready" are printed.
	if (Controller->GetPawn() == nullptr)
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

	// The globe and the patch are two samplings of one height function, and between samples they
	// differ by however much terrain falls between the coarse mesh's vertices. Drawn together that
	// would be the globe's hills poking through the patch's, so only one is ever visible: the
	// patch owns the view for as long as it exists, and the patch is built wide enough to cover
	// everything the viewer could see.
	if (Surface != nullptr)
	{
		const bool bShowGlobe = !bHasPatch || GHideGlobeUnderPatch <= 0.5f;

		if (Surface->IsVisible() != bShowGlobe)
		{
			UE_LOG(LogSpaceMMO, Log,
				TEXT("Globe %s (terrain patch %s)."),
				bShowGlobe ? TEXT("shown") : TEXT("hidden"),
				bHasPatch ? TEXT("present") : TEXT("absent"));
		}

		Surface->SetVisibility(bShowGlobe);
	}

	if (ViewerProximity == EPlanetProximity::Orbital)
	{
		if (bHasPatch)
		{
			UE_LOG(LogSpaceMMO, Log, TEXT("Left the atmosphere; releasing terrain patch."));

			bHasPatch = false;
			PatchDirection = FVector::ZeroVector;
			PatchAngularRadiusDegrees = 0.0;

			if (GroundPatch != nullptr)
			{
				GroundPatch->SetVisibility(false);
			}

			if (Surface != nullptr)
			{
				Surface->SetVisibility(true);
			}
		}

		return;
	}

	const FVector ViewerDirection =
		(ViewerPosition.Kilometres - Planet.Centre.Kilometres).GetSafeNormal();

	// Height above the ground, not above the sphere the ground sits on. Standing on a half-kilometre
	// mountain is still standing: the horizon is a few hundred metres away and the patch should be
	// narrow and detailed. Measuring against the sphere would call that an altitude of half a
	// kilometre and spread the same vertices over five times the ground for no one's benefit.
	const double DesiredDegrees = PatchDegreesForAltitude(
		Planet,
		FPlanetTerrain::AltitudeAboveGroundKilometres(Planet, TerrainConfig, ViewerPosition));

	// Two reasons to rebuild: the viewer has walked far enough across the patch, or climbed far
	// enough that the patch no longer reaches their horizon.
	const bool bDrifted = bHasPatch
		&& FPlanetPatch::ShouldRebuild(PatchDirection, ViewerDirection, PatchAngularRadiusDegrees);

	const bool bWrongWidth = bHasPatch
		&& PatchAngularRadiusDegrees > 0.0
		&& FMath::Abs(DesiredDegrees - PatchAngularRadiusDegrees)
			> PatchAngularRadiusDegrees * PatchWidthDriftFraction;

	if (bHasPatch && !bDrifted && !bWrongWidth)
	{
		return;
	}

	PatchDirection = ViewerDirection;
	PatchAngularRadiusDegrees = DesiredDegrees;

	BuildPatch(ViewerDirection);
}

void ASpaceMMOPlanetActor::BuildPatch(const FVector& Direction)
{
	if (GroundPatch == nullptr)
	{
		return;
	}

	FPlanetPatchConfig Config;
	Config.CentreDirection = Direction;
	Config.AngularRadiusDegrees = PatchAngularRadiusDegrees;

	const FPlanetPatchMesh Patch = FPlanetPatch::Build(Planet, TerrainConfig, Config);

	if (!Patch.IsValid())
	{
		UE_LOG(LogSpaceMMO, Warning, TEXT("Terrain patch generated nothing."));

		return;
	}

	PatchOrigin = Patch.Origin;

	FDynamicMesh3 Mesh;
	Mesh.EnableAttributes();

	for (const FVector& Position : Patch.Positions)
	{
		Mesh.AppendVertex(FVector3d(Position));
	}

	for (int32 Index = 0; Index + 2 < Patch.Triangles.Num(); Index += 3)
	{
		Mesh.AppendTriangle(
			Patch.Triangles[Index], Patch.Triangles[Index + 1], Patch.Triangles[Index + 2]);
	}

	if (FDynamicMeshNormalOverlay* Normals =
		Mesh.Attributes() != nullptr ? Mesh.Attributes()->PrimaryNormals() : nullptr)
	{
		Normals->ClearElements();

		TArray<int32> Elements;
		Elements.Reserve(Patch.Normals.Num());

		for (const FVector& Normal : Patch.Normals)
		{
			Elements.Add(Normals->AppendElement(FVector3f(Normal)));
		}

		for (const int32 TriangleId : Mesh.TriangleIndicesItr())
		{
			const FIndex3i Triangle = Mesh.GetTriangle(TriangleId);

			Normals->SetTriangle(
				TriangleId,
				FIndex3i(Elements[Triangle.A], Elements[Triangle.B], Elements[Triangle.C]));
		}
	}

	GroundPatch->SetMesh(MoveTemp(Mesh));
	GroundPatch->NotifyMeshUpdated();
	GroundPatch->SetVisibility(true);

	bHasPatch = true;

	ApplyRenderTransform();

	UE_LOG(LogSpaceMMO, Log,
		TEXT("Terrain patch at %s: %d triangles across %.1f degrees, component at %s."),
		*PatchOrigin.ToString(),
		Patch.Triangles.Num() / 3,
		PatchAngularRadiusDegrees,
		*GroundPatch->GetComponentLocation().ToCompactString());
}

void ASpaceMMOPlanetActor::SetPlanetConfig(const FPlanetConfig& NewConfig)
{
	Planet = NewConfig;

	BuildGlobe();

	ApplyRenderTransform();
}

void ASpaceMMOPlanetActor::SetTerrainConfig(const FPlanetTerrainConfig& NewTerrain)
{
	TerrainConfig = NewTerrain;

	// The globe is a tessellation of exactly this, so leaving it alone would leave a planet whose
	// shape and whose ground came from different settings.
	BuildGlobe();
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

	// The globe's vertices are already at planet scale in centimetres from its centre, so placing
	// the actor is the whole transform. The engine sphere needed scaling because it was a 100 cm
	// ball standing in for a 20 km planet, which is also why it was a polyhedron.
	SetActorLocation(Origin->ToWorldLocation(Planet.Centre));

	// The patch keeps its own absolute position: its vertices are relative to the ground beneath
	// the viewer, which is twenty kilometres from the planet's centre and moves independently.
	if (GroundPatch != nullptr && bHasPatch)
	{
		GroundPatch->SetWorldLocation(Origin->ToWorldLocation(PatchOrigin));
	}

	BuiltAtRevision = Origin->GetRevision();
}
