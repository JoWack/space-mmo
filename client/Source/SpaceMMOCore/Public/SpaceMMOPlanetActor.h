#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SpaceMMOPlanet.h"
#include "SpaceMMOPlanetGlobe.h"
#include "SpaceMMOPlanetTerrain.h"
#include "SpaceMMOPlanetActor.generated.h"

class UDynamicMeshComponent;

/**
 * A planet, drawn from its system-space position.
 *
 * Like everything else with a system coordinate, its Unreal transform is recomputed against the
 * render origin rather than stored — so it holds still while the ship flies past and the origin
 * jumps beneath it.
 *
 * <strong>Two meshes of one surface, and never both at once.</strong> The globe is the whole
 * planet at a coarse sampling; the patch is the ground under the viewer at a fine one. Both are
 * tessellations of the same height function, so neither can drift away from what the physics
 * thinks the ground is — but a coarse mesh and a fine mesh of the same hills still disagree by
 * tens of metres between samples, which drawn together would be hills poking through hills. So
 * the patch widens with altitude until it covers everything in view, and the globe is hidden for
 * exactly as long as the patch exists.
 */
UCLASS()
class SPACEMMOCORE_API ASpaceMMOPlanetActor : public AActor
{
	GENERATED_BODY()

public:
	ASpaceMMOPlanetActor();

	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Planet")
	FPlanetConfig GetPlanetConfig() const { return Planet; }

	UFUNCTION(BlueprintCallable, Category = "SpaceMMO|Planet")
	void SetPlanetConfig(const FPlanetConfig& NewConfig);

	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Planet")
	FPlanetTerrainConfig GetTerrainConfig() const { return TerrainConfig; }

	UFUNCTION(BlueprintCallable, Category = "SpaceMMO|Planet")
	void SetTerrainConfig(const FPlanetTerrainConfig& NewTerrain);

	/** Proximity of the local viewer, as the planet last classified it. */
	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Planet")
	EPlanetProximity GetViewerProximity() const { return ViewerProximity; }

	/**
	 * How wide the ground patch needs to be, in degrees of arc, for a viewer at a given altitude.
	 *
	 * Grows with altitude to cover the horizon, so the patch is always the only surface worth
	 * drawing. The floor keeps a walking player on fine ground rather than on a patch stretched
	 * thin to cover a horizon a few hundred metres away; the ceiling is where the patch's
	 * tangent-plane parameterisation stretches too badly to be worth widening further, and is
	 * reached just short of the top of the atmosphere.
	 */
	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Planet")
	static double PatchDegreesForAltitude(
		const FPlanetConfig& Planet,
		double AltitudeKilometres,
		double MinimumDegrees = 4.0,
		double MaximumDegrees = 60.0);

protected:
	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Planet")
	FPlanetConfig Planet;

	/** The shape of this planet's surface. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Planet")
	FPlanetTerrainConfig TerrainConfig;

	/** How finely the whole-planet mesh is tessellated. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Planet")
	FPlanetGlobeConfig GlobeConfig;

private:
	void ApplyRenderTransform();

	/** Tessellates the whole planet. Once, unless the planet or its terrain is reconfigured. */
	void BuildGlobe();

	/** Tessellates the ground around a direction into <see cref="GroundPatch"/>. */
	void BuildPatch(const FVector& Direction);

	/**
	 * Streams the landing zone in and out as the viewer approaches and leaves.
	 *
	 * Lives on the planet rather than on the ship, because the planet is what owns the surface and
	 * a ship has no business knowing how terrain is built. It also means several planets each
	 * manage their own ground without anything coordinating them.
	 */
	void UpdateTerrainPatch();

	/** Where the local viewer is, in system space, or false if there is nobody to render for. */
	bool TryGetViewerPosition(FSystemCoordinate& OutPosition) const;

	/** Direction the current patch is centred on, or zero if there is no patch. */
	FVector PatchDirection = FVector::ZeroVector;

	/** Arc the current patch spans, so a change in altitude can be noticed. */
	double PatchAngularRadiusDegrees = 0.0;

	EPlanetProximity ViewerProximity = EPlanetProximity::Orbital;

	UPROPERTY(VisibleAnywhere, Category = "SpaceMMO|Planet")
	TObjectPtr<UDynamicMeshComponent> Surface;

	/**
	 * The detailed ground under the viewer.
	 *
	 * A component on this actor rather than an actor of its own. The separate actor version never
	 * drew — visible, registered, holding thirty-two thousand triangles and a material, with the
	 * camera inside its bounds, and never on screen — while this actor's globe, built by nearly
	 * identical code into the same component type, always did. The one thing that differed was how
	 * it came into being: spawned mid-Tick, given a mesh, then moved. So the patch now comes into
	 * being the same way the globe does.
	 */
	UPROPERTY(VisibleAnywhere, Category = "SpaceMMO|Planet")
	TObjectPtr<UDynamicMeshComponent> GroundPatch;

	/** Anchor the patch's vertices are relative to. */
	FSystemCoordinate PatchOrigin;

	/** True once a patch mesh has been built and is worth drawing. */
	bool bHasPatch = false;

	/** True while the patch is borrowing the globe's component, for SpaceMMO.PatchIntoGlobe. */
	bool bPatchInGlobeComponent = false;

	/** Which SpaceMMO.PatchVariant the current mesh was built with, so a change forces a rebuild. */
	int32 AppliedPatchVariant = 0;

	/** Whether the current mesh was built with SpaceMMO.PatchFlipWinding, for the same reason. */
	bool bAppliedFlippedWinding = false;

	/** Whether the globe was built with SpaceMMO.GlobeFlipWinding, so a change forces a rebuild. */
	bool bAppliedGlobeFlippedWinding = false;

	/** True while the globe's mesh is in the patch's component, for SpaceMMO.GlobeIntoPatch. */
	bool bGlobeInPatchComponent = false;

	/** Arc the globe was last cropped to, so changing SpaceMMO.GlobeCrop forces a rebuild. */
	float AppliedGlobeCropDegrees = 0.0f;

	/** Render-origin revision the transform was last built against. */
	int32 BuiltAtRevision = -1;

	/**
	 * Logs what the patch's component is actually holding, one frame after it was given a mesh.
	 *
	 * Deferred on purpose. Marking the render state dirty destroys the scene proxy and queues a
	 * replacement for the end of the frame, so reading it during the build reports the frame
	 * before the one being asked about.
	 */
	void ReportPatchIfPending();

	/** Set when a patch is built, cleared by the report on the following tick. */
	bool bPatchReportPending = false;
};
