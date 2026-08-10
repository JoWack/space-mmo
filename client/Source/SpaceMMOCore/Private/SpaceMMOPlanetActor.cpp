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

	/**
	 * Puts the patch's mesh into the globe's own component instead of its own.
	 *
	 * The last bisect available. The patch has now failed to draw as its own actor and as a second
	 * component on this one, so how it is built is not the fault. That leaves the mesh and the
	 * component, and this separates them: the globe's component provably draws, so if the patch's
	 * vertices do not draw inside it either, the fault is in the vertices.
	 *
	 * Destructive on purpose — it replaces the globe while it is on.
	 */
	float GPatchIntoGlobe = 0.0f;

	/**
	 * Which variant of the patch mesh to build.
	 *
	 * The mesh is valid by every measure available without a renderer — no rejected triangle, no
	 * bad normal, no degenerate face, structurally sound, correctly attributed — and it still does
	 * not draw inside a component that draws the globe. So the remaining move is to vary it and
	 * watch, one property at a time, rather than reason about it again.
	 *
	 * 0 as built. 1 with the terrain flattened out, leaving a plain spherical cap. 2 at a much
	 * lower resolution. 3 with radial normals instead of ones accumulated from the faces. None of
	 * those drew.
	 *
	 * 4 is the last structural difference left. The globe's vertices sit twenty kilometres from
	 * its component's origin and it draws; the patch's sit within two, one of them exactly on it,
	 * and it does not. This anchors the patch to the planet's centre the way the globe is, which
	 * is the wrong thing to ship — it throws away the precision the local anchor exists to protect
	 * (ADR-0001) — but it answers whether the anchor is what stops it drawing.
	 */
	float GPatchVariant = 0.0f;

	/**
	 * Rebuilds the globe, at runtime, from a console command.
	 *
	 * Built to test whether handing a registered component new geometry reaches the renderer at
	 * all — the globe's mesh is set once in BeginPlay, the patch's from Tick, and that difference
	 * had been in every experiment and tested by none of them.
	 *
	 * The engine source answered it instead, for free: NotifyMeshUpdated() -> ResetProxy() ->
	 * MarkRenderStateDirty(). A mesh updated after registration does reach the renderer, so a
	 * rebuilt globe would have drawn and the run would have proved nothing about the patch.
	 *
	 * Kept because rebuilding the planet on a keypress is a useful lever in its own right, and
	 * because it restores the globe after SpaceMMO.PatchIntoGlobe borrows its component. It is no
	 * longer evidence about anything.
	 */
	float GRebuildGlobe = 0.0f;

	FAutoConsoleVariableRef CVarRebuildGlobe(
		TEXT("SpaceMMO.RebuildGlobe"),
		GRebuildGlobe,
		TEXT("1 rebuilds the planet's own mesh at runtime. A lever, not a measurement: the engine "
			"already marks the render state dirty on every mesh update."),
		ECVF_Default);

	// SpaceMMO.DirtyAfterMeshUpdate is gone, and the hypothesis above with it, without costing a
	// playtest. UDynamicMeshComponent::NotifyMeshUpdated() calls ResetProxy(), and ResetProxy()
	// calls MarkRenderStateDirty() before it updates the bounds -- so the "candidate fix" was
	// asking the engine to do a thing it had already done on the line above. It could never have
	// changed anything, and had it been run, "no ground" would have read as evidence about
	// registration timing instead of evidence about nothing at all.
	//
	// The same source kills the diagnosis. Handing a registered component new geometry does reach
	// the renderer, by the engine's own code, so "updating a mesh after registration" is not why
	// the patch is missing. Read the engine before building a switch to ask it a question.

	FAutoConsoleVariableRef CVarPatchVariant(
		TEXT("SpaceMMO.PatchVariant"),
		GPatchVariant,
		TEXT("0 normal, 1 flat, 2 low resolution, 3 radial normals, 4 anchored at the planet's "
			"centre like the globe. Whichever one draws names the property at fault."),
		ECVF_Default);

	/**
	 * Reverses the order the patch's triangle indices are appended in.
	 *
	 * Everything the mesh data can be varied by has now been varied — elevations, resolution,
	 * normals and anchoring — and none of it made the ground appear, in a component that draws the
	 * globe. What the playtests keep describing is facing: a white band where the ground is steep
	 * and distant, nothing where it faces the viewer, and nothing at all once the terrain is
	 * flattened. That is what backface culling looks like.
	 *
	 * The awkward part, and the reason this is a switch rather than a fix: the patch's data is
	 * outward-wound, asserted by SurvivesBeingWide at four widths, by the identical formula the
	 * globe's own test uses. So if reversing the order makes the ground appear, the inversion is
	 * happening somewhere after that data, and this says so without pretending to know where.
	 */
	/**
	 * Counts triangles facing the planet's centre, in the mesh about to be handed to a component.
	 *
	 * Every winding check in this project runs against a mesh built inside a test, and they all
	 * pass — including one that reads the winding back out of an FDynamicMesh3 after the append.
	 * Yet the patch renders only with its indices reversed and the globe renders without. Both
	 * cannot be true of the same geometry, so this measures the mesh the running game actually
	 * builds, which is the one thing nobody has looked at.
	 *
	 * CentreOffset carries the anchor: the globe's vertices are already relative to the planet's
	 * centre, the patch's are relative to the ground beneath the viewer.
	 */
	int32 CountInwardTriangles(const FDynamicMesh3& Mesh, const FVector& CentreOffset)
	{
		int32 Inward = 0;

		for (const int32 TriangleId : Mesh.TriangleIndicesItr())
		{
			const FIndex3i Triangle = Mesh.GetTriangle(TriangleId);

			const FVector A = FVector(Mesh.GetVertex(Triangle.A));
			const FVector B = FVector(Mesh.GetVertex(Triangle.B));
			const FVector C = FVector(Mesh.GetVertex(Triangle.C));

			if (FVector::DotProduct(
				FVector::CrossProduct(B - A, C - A),
				CentreOffset + ((A + B + C) / 3.0)) <= 0.0)
			{
				++Inward;
			}
		}

		return Inward;
	}

	/**
	 * The same reversal, applied to the globe.
	 *
	 * The patch draws when its indices are reversed and the globe draws without that, through the
	 * same component, at the same determinant, with both meshes asserted outward-wound by equivalent
	 * tests. One of those statements has to be false, and this is the half that has never been
	 * varied. If the globe also draws reversed, the sphere is hiding its own facing and the two
	 * results are compatible; if the globe disappears, the two meshes genuinely have opposite
	 * handedness and the tests are not measuring the same thing as each other.
	 */
	float GGlobeFlipWinding = 0.0f;

	FAutoConsoleVariableRef CVarGlobeFlipWinding(
		TEXT("SpaceMMO.GlobeFlipWinding"),
		GGlobeFlipWinding,
		TEXT("1 appends the globe's triangles in reverse order, and rebuilds it. The globe "
			"disappearing means it is genuinely wound the opposite way to the patch."),
		ECVF_Default);

	float GPatchFlipWinding = 0.0f;

	FAutoConsoleVariableRef CVarPatchFlipWinding(
		TEXT("SpaceMMO.PatchFlipWinding"),
		GPatchFlipWinding,
		TEXT("1 appends the patch's triangles in reverse order. Ground that appears means the patch "
			"is being rasterised back to front."),
		ECVF_Default);

	FAutoConsoleVariableRef CVarPatchIntoGlobe(
		TEXT("SpaceMMO.PatchIntoGlobe"),
		GPatchIntoGlobe,
		TEXT("1 puts the patch mesh into the globe's component, replacing the globe. Ground that "
			"appears means the component was at fault; ground that does not means the mesh is."),
		ECVF_Default);

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

	bAppliedGlobeFlippedWinding = GGlobeFlipWinding > 0.5f;

	for (int32 Index = 0; Index + 2 < Globe.Triangles.Num(); Index += 3)
	{
		Mesh.AppendTriangle(
			Globe.Triangles[Index],
			Globe.Triangles[Index + (bAppliedGlobeFlippedWinding ? 2 : 1)],
			Globe.Triangles[Index + (bAppliedGlobeFlippedWinding ? 1 : 2)]);
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

	// Measured on the mesh the component is about to receive, not on one a test assembled.
	const int32 GlobeInward = CountInwardTriangles(Mesh, FVector::ZeroVector);

	Surface->SetMesh(MoveTemp(Mesh));
	Surface->NotifyMeshUpdated();

	UE_LOG(LogSpaceMMO, Log,
		TEXT("  globe faces inward: %d of %d."),
		GlobeInward,
		Globe.Triangles.Num() / 3);

	UE_LOG(LogSpaceMMO, Log,
		TEXT("Planet globe: %d triangles, %s winding, a vertex every %.0f m of ground."),
		Globe.Triangles.Num() / 3,
		bAppliedGlobeFlippedWinding ? TEXT("REVERSED") : TEXT("as built"),
		(FMath::DegreesToRadians(90.0 / FMath::Max(GlobeConfig.Resolution - 1, 1))
			* Planet.RadiusKilometres) * 1000.0);
}

void ASpaceMMOPlanetActor::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const UWorld* World = GetWorld();

	const USpaceMMORenderOriginSubsystem* Origin =
		World != nullptr ? World->GetSubsystem<USpaceMMORenderOriginSubsystem>() : nullptr;

	// Rebuild the planet's own mesh on request, so the runtime update path can be tested with
	// geometry that is known to draw.
	if (GRebuildGlobe > 0.5f)
	{
		GRebuildGlobe = 0.0f;

		UE_LOG(LogSpaceMMO, Log, TEXT("Rebuilding the globe at runtime."));

		BuildGlobe();
	}

	// The globe is otherwise built once, at BeginPlay. Without this the winding switch would set a
	// value that nothing ever read, and come back "no change" having never been applied -- which is
	// precisely how three earlier experiments produced results from runs that did not happen.
	if ((GGlobeFlipWinding > 0.5f) != bAppliedGlobeFlippedWinding)
	{
		UE_LOG(LogSpaceMMO, Log,
			TEXT("Globe winding changed to %s; rebuilding."),
			GGlobeFlipWinding > 0.5f ? TEXT("REVERSED") : TEXT("as built"));

		BuildGlobe();
	}

	// Terrain is checked every frame regardless of the origin, because it follows the viewer
	// rather than the render window — a player can walk a long way without a single rebase.
	UpdateTerrainPatch();

	ReportPatchIfPending();

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

	// Height above the ground, not above the sphere it sits on. Standing on half a kilometre of
	// terrain put a landed ship two and a half surface bands up and had it classified as flying,
	// which is the same mistake the patch width made and for the same reason.
	const double ViewerAltitude =
		FPlanetTerrain::AltitudeAboveGroundKilometres(Planet, TerrainConfig, ViewerPosition);

	// Fed its own previous value, so the hysteresis in ClassifyProximity has something to work
	// against — without it a viewer hovering on the atmosphere boundary would build and destroy
	// the same patch every frame.
	ViewerProximity = FPlanetPhysics::ClassifyProximityAtAltitude(
		Planet, ViewerAltitude, ViewerProximity);

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

	// The same height above the ground the classification used. Standing on a half-kilometre
	// mountain is still standing: the horizon is a few hundred metres away and the patch should be
	// narrow and detailed.
	const double DesiredDegrees = PatchDegreesForAltitude(Planet, ViewerAltitude);

	// Two reasons to rebuild: the viewer has walked far enough across the patch, or climbed far
	// enough that the patch no longer reaches their horizon.
	const bool bDrifted = bHasPatch
		&& FPlanetPatch::ShouldRebuild(PatchDirection, ViewerDirection, PatchAngularRadiusDegrees);

	const bool bWrongWidth = bHasPatch
		&& PatchAngularRadiusDegrees > 0.0
		&& FMath::Abs(DesiredDegrees - PatchAngularRadiusDegrees)
			> PatchAngularRadiusDegrees * PatchWidthDriftFraction;

	// A third reason: somebody changed what the patch should be made of.
	//
	// Without this the diagnostic switches did nothing visible and looked like answers. A wide
	// patch tolerates eighteen degrees of drift before it rebuilds — kilometres of walking — so
	// setting a variant and moving a little produced no rebuild at all, and three variants were
	// reported as "did not draw" when they had never been built.
	const int32 WantedVariant = FMath::RoundToInt(GPatchVariant);
	const bool bWantsGlobeComponent = GPatchIntoGlobe > 0.5f;
	const bool bWantsFlippedWinding = GPatchFlipWinding > 0.5f;

	const bool bRecipeChanged = bHasPatch
		&& (WantedVariant != AppliedPatchVariant
			|| bWantsGlobeComponent != bPatchInGlobeComponent
			|| bWantsFlippedWinding != bAppliedFlippedWinding);

	if (bHasPatch && !bDrifted && !bWrongWidth && !bRecipeChanged)
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

	const int32 Variant = FMath::RoundToInt(GPatchVariant);

	FPlanetPatchConfig Config;
	Config.CentreDirection = Direction;
	Config.AngularRadiusDegrees = PatchAngularRadiusDegrees;

	// Two thousand triangles rather than thirty-two, in case the fault scales with the mesh.
	if (Variant == 2)
	{
		Config.Resolution = 33;
	}

	// A plain spherical cap: same vertices, same winding, no height variation at all. If this
	// draws and the real one does not, the fault is in the elevations rather than in the grid.
	FPlanetTerrainConfig Terrain = TerrainConfig;

	if (Variant == 1)
	{
		Terrain.MaxElevationKilometres = 0.0;
	}

	FPlanetPatchMesh Patch = FPlanetPatch::Build(Planet, Terrain, Config);

	// Radial normals: smooth and provably outward, ignoring the accumulated ones entirely.
	if (Variant == 3)
	{
		const FVector CentreOffset =
			(Patch.Origin.Kilometres - Planet.Centre.Kilometres)
			* SpaceMMO::Coordinates::CentimetresPerKilometre;

		for (int32 Index = 0; Index < Patch.Normals.Num(); ++Index)
		{
			Patch.Normals[Index] = (CentreOffset + Patch.Positions[Index]).GetSafeNormal();
		}
	}

	// Re-anchored to the planet's centre: every vertex pushed out by the offset that used to be in
	// the component's transform, so the numbers look like the globe's.
	if (Variant == 4)
	{
		const FVector CentreOffset =
			(Patch.Origin.Kilometres - Planet.Centre.Kilometres)
			* SpaceMMO::Coordinates::CentimetresPerKilometre;

		for (FVector& Position : Patch.Positions)
		{
			Position += CentreOffset;
		}

		Patch.Origin = Planet.Centre;
	}

	AppliedPatchVariant = Variant;

	UE_LOG(LogSpaceMMO, Log,
		TEXT("Patch variant %d: %s."),
		Variant,
		Variant == 1 ? TEXT("flat, no terrain")
			: Variant == 2 ? TEXT("low resolution")
			: Variant == 3 ? TEXT("radial normals")
			: Variant == 4 ? TEXT("anchored at the planet's centre")
			: TEXT("as built"));

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

	// Swapping the second and third index reverses a triangle's winding without touching a single
	// position, so nothing about where the surface sits can change with it.
	bAppliedFlippedWinding = GPatchFlipWinding > 0.5f;

	for (int32 Index = 0; Index + 2 < Patch.Triangles.Num(); Index += 3)
	{
		Mesh.AppendTriangle(
			Patch.Triangles[Index],
			Patch.Triangles[Index + (bAppliedFlippedWinding ? 2 : 1)],
			Patch.Triangles[Index + (bAppliedFlippedWinding ? 1 : 2)]);
	}

	if (bAppliedFlippedWinding)
	{
		UE_LOG(LogSpaceMMO, Log, TEXT("Patch winding: reversed."));
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

	const bool bWantsGlobeComponent = GPatchIntoGlobe > 0.5f;

	// Turning the experiment off has to give the planet back. The globe's component is holding the
	// patch's mesh by then, and the globe is otherwise only built once at startup — so switching
	// the flag back left a permanently black sky, which is a diagnostic that breaks the thing it
	// was measuring.
	if (bPatchInGlobeComponent && !bWantsGlobeComponent)
	{
		bPatchInGlobeComponent = false;

		BuildGlobe();
	}

	bPatchInGlobeComponent = bWantsGlobeComponent;

	UDynamicMeshComponent* const Target = bPatchInGlobeComponent ? Surface : GroundPatch;

	// The same count as the globe's, against the same reference, so the two can be compared
	// directly in one log rather than argued about across two.
	const int32 PatchInward = CountInwardTriangles(
		Mesh,
		(PatchOrigin.Kilometres - Planet.Centre.Kilometres)
			* SpaceMMO::Coordinates::CentimetresPerKilometre);

	UE_LOG(LogSpaceMMO, Log,
		TEXT("  patch faces inward: %d of %d."),
		PatchInward,
		Patch.Triangles.Num() / 3);

	// NotifyMeshUpdated() already marks the render state dirty, via ResetProxy(). Nothing else is
	// needed here, and anything added would be a second request for the same work.
	Target->SetMesh(MoveTemp(Mesh));
	Target->NotifyMeshUpdated();

	Target->SetVisibility(true);

	bHasPatch = true;

	ApplyRenderTransform();

	UE_LOG(LogSpaceMMO, Log,
		TEXT("Terrain patch at %s: %d triangles across %.1f degrees, in %s at %s."),
		*PatchOrigin.ToString(),
		Patch.Triangles.Num() / 3,
		PatchAngularRadiusDegrees,
		bPatchInGlobeComponent ? TEXT("the globe's component") : TEXT("its own component"),
		*Target->GetComponentLocation().ToCompactString());

	// Read next frame rather than here. MarkRenderStateDirty() destroys the scene proxy and queues
	// a new one for the end of the frame, so asking now reports whatever was true before the mesh
	// changed — which would be a diagnostic that answers confidently about the wrong frame.
	bPatchReportPending = true;
}

void ASpaceMMOPlanetActor::ReportPatchIfPending()
{
	if (!bPatchReportPending)
	{
		return;
	}

	bPatchReportPending = false;

	// Not const: UDynamicMeshComponent::GetDynamicMesh() is a non-const accessor, so reading the
	// triangle count off a const pointer does not compile.
	UDynamicMeshComponent* const Target = bPatchInGlobeComponent ? Surface : GroundPatch;

	if (Target == nullptr)
	{
		UE_LOG(LogSpaceMMO, Warning, TEXT("  patch report: no component to report on."));

		return;
	}

	// What the component is holding, as opposed to what was handed to it. This block used to live
	// on ASpaceMMOTerrainPatchActor, which stopped being spawned when the patch moved onto this
	// actor -- so the run that produced "visible, registered, camera inside its bounds, never on
	// screen" has not been repeatable since, and every playtest since has had one line to go on.
	const UMaterialInterface* const Material = Target->GetMaterial(0);
	const FBoxSphereBounds Bounds = Target->Bounds;

	UE_LOG(LogSpaceMMO, Log,
		TEXT("  patch holds %d triangles, material %s, visible %d, scale %s."),
		Target->GetDynamicMesh() != nullptr ? Target->GetDynamicMesh()->GetTriangleCount() : -1,
		Material != nullptr ? *Material->GetName() : TEXT("NONE"),
		Target->IsVisible() ? 1 : 0,
		*Target->GetComponentScale().ToCompactString());

	// A component can report itself visible, hold a mesh and a material, and still never reach the
	// renderer if its owner is hidden, if it never registered, or if no proxy was ever created for
	// it. Having a proxy a frame after the update is the one that says the geometry got there.
	UE_LOG(LogSpaceMMO, Log,
		TEXT("  actor hidden %d, registered %d, has proxy %d, render in main pass %d."),
		IsHidden() ? 1 : 0,
		Target->IsRegistered() ? 1 : 0,
		Target->SceneProxy != nullptr ? 1 : 0,
		Target->bRenderInMainPass ? 1 : 0);

	UE_LOG(LogSpaceMMO, Log,
		TEXT("  bounds origin %s, box extent %s, sphere radius %.0f cm."),
		*Bounds.Origin.ToCompactString(),
		*Bounds.BoxExtent.ToCompactString(),
		Bounds.SphereRadius);

	// Bounds that do not contain the camera can be frustum-culled; bounds that do cannot, which
	// turns "it is off screen" from a guess into a ruled-out answer.
	if (const APlayerController* const Controller =
		GetWorld() != nullptr ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		FVector ViewLocation = FVector::ZeroVector;
		FRotator ViewRotation = FRotator::ZeroRotator;

		const_cast<APlayerController*>(Controller)->GetPlayerViewPoint(ViewLocation, ViewRotation);

		const double ToCentre = FVector::Dist(ViewLocation, Bounds.Origin);

		UE_LOG(LogSpaceMMO, Log,
			TEXT("  camera at %s, %.0f cm from the bounds centre, inside them %d."),
			*ViewLocation.ToCompactString(),
			ToCentre,
			ToCentre <= Bounds.SphereRadius ? 1 : 0);
	}
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
	// While the patch is borrowing the globe's component, the actor has to stand at the patch's
	// anchor instead of the planet's centre — the component is the root, so its transform is the
	// actor's.
	SetActorLocation(Origin->ToWorldLocation(
		bPatchInGlobeComponent && bHasPatch ? PatchOrigin : Planet.Centre));

	// The patch keeps its own absolute position: its vertices are relative to the ground beneath
	// the viewer, which is twenty kilometres from the planet's centre and moves independently.
	if (GroundPatch != nullptr && bHasPatch && !bPatchInGlobeComponent)
	{
		GroundPatch->SetWorldLocation(Origin->ToWorldLocation(PatchOrigin));
	}

	BuiltAtRevision = Origin->GetRevision();
}
