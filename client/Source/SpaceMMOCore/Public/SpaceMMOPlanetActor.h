#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SpaceMMOPlanet.h"
#include "SpaceMMOPlanetTerrain.h"
#include "SpaceMMOPlanetActor.generated.h"

class ASpaceMMOTerrainPatchActor;
class UStaticMeshComponent;

/**
 * A planet, drawn from its system-space position.
 *
 * Like everything else with a system coordinate, its Unreal transform is recomputed against the
 * render origin rather than stored — so it holds still while the ship flies past and the origin
 * jumps beneath it.
 *
 * <strong>This is a sphere, not terrain.</strong> It is enough to fly toward, orbit, and measure
 * altitude against, which is what proves the approach transition. An actual landable surface needs
 * runtime cube-sphere LOD terrain, and that is a substantially larger problem deliberately left
 * until the transition around it is known to work.
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
	void SetTerrainConfig(const FPlanetTerrainConfig& NewTerrain) { TerrainConfig = NewTerrain; }

	/** Proximity of the local viewer, as the planet last classified it. */
	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Planet")
	EPlanetProximity GetViewerProximity() const { return ViewerProximity; }

protected:
	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Planet")
	FPlanetConfig Planet;

	/** The shape of this planet's surface. Only meaningful once close enough to mesh it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Planet")
	FPlanetTerrainConfig TerrainConfig;

	/**
	 * How much ground to mesh around the viewer, in degrees of arc.
	 *
	 * Four degrees of a 20 km planet is about 1.4 km across. At 129 vertices a side that is roughly
	 * eleven metres between them, which is the difference between ground a walking person can see
	 * changing and a flat plane that never appears to move. Ten degrees looked far more generous
	 * and was, in practice, featureless.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Planet")
	double PatchAngularRadiusDegrees = 4.0;

private:
	void ApplyRenderTransform();

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

	UPROPERTY()
	TObjectPtr<ASpaceMMOTerrainPatchActor> TerrainPatch;

	/** Direction the current patch is centred on, or zero if there is no patch. */
	FVector PatchDirection = FVector::ZeroVector;

	EPlanetProximity ViewerProximity = EPlanetProximity::Orbital;

	UPROPERTY(VisibleAnywhere, Category = "SpaceMMO|Planet")
	TObjectPtr<UStaticMeshComponent> Surface;

	/** Render-origin revision the transform was last built against. */
	int32 BuiltAtRevision = -1;
};
