#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SpaceMMOPlanetPatch.h"
#include "SpaceMMOTerrainPatchActor.generated.h"

class UDynamicMeshComponent;

/**
 * A patch of walkable ground, built at runtime from the planet's height function.
 *
 * The landing-zone half of the terrain plan. The planet stays a smooth sphere from orbit — which
 * is all it ever needs to be at that distance — and one of these appears where a ship is actually
 * coming down. Since the heights come from {@link FPlanetTerrain}, the same function the server
 * uses to decide where the ground is, the patch is a view of the authoritative surface rather
 * than a second opinion about it.
 *
 * Nothing here is replicated. Two clients landing at the same spot build the same mesh from the
 * same seed, so sending it would be sending something both ends already know.
 */
UCLASS()
class SPACEMMOCORE_API ASpaceMMOTerrainPatchActor : public AActor
{
	GENERATED_BODY()

public:
	ASpaceMMOTerrainPatchActor();

	virtual void Tick(float DeltaSeconds) override;

	/**
	 * Generates the patch and hands it to the mesh component.
	 *
	 * @param Direction Where on the planet to build. Normalised on use.
	 */
	UFUNCTION(BlueprintCallable, Category = "SpaceMMO|Terrain")
	void BuildPatch(
		const FPlanetConfig& InPlanet,
		const FPlanetTerrainConfig& InTerrain,
		const FVector& Direction);

	/** The patch's anchor in system space. */
	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Terrain")
	FSystemCoordinate GetPatchOrigin() const { return PatchOrigin; }

	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Terrain")
	int32 GetTriangleCount() const { return TriangleCount; }

protected:
	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Terrain")
	FPlanetPatchConfig PatchConfig;

private:
	void ApplyRenderTransform();

	UPROPERTY(VisibleAnywhere, Category = "SpaceMMO|Terrain")
	TObjectPtr<UDynamicMeshComponent> Ground;

	FPlanetConfig Planet;

	FPlanetTerrainConfig Terrain;

	FSystemCoordinate PatchOrigin;

	int32 TriangleCount = 0;

	/** Render-origin revision the transform was last built against. */
	int32 BuiltAtRevision = -1;
};
