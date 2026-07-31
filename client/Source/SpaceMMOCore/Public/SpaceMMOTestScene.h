#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SpaceMMOCoordinates.h"
#include "SpaceMMOTestScene.generated.h"

class UInstancedStaticMeshComponent;

/**
 * A field of reference markers, so flight is visible.
 *
 * Empty space renders identically at any speed — without fixed objects to pass, there is nothing
 * to see moving and no way to tell whether flight works at all. This scatters marker cubes at
 * known positions in <em>system</em> space, which makes it a live demonstration of the coordinate
 * model rather than merely scenery: the markers stay exactly where they are while the ship moves
 * and the render origin jumps beneath them.
 *
 * Placeholder for real content, and the first thing to delete once there are planets and stations
 * to navigate by.
 */
UCLASS()
class SPACEMMOCORE_API ASpaceMMOTestScene : public AActor
{
	GENERATED_BODY()

public:
	ASpaceMMOTestScene();

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

	/** Markers per axis. The lattice is this cubed, so it grows quickly. */
	UPROPERTY(EditAnywhere, Category = "SpaceMMO|TestScene")
	int32 MarkersPerAxis = 7;

	/** Spacing between markers, in kilometres. */
	UPROPERTY(EditAnywhere, Category = "SpaceMMO|TestScene")
	double SpacingKilometres = 3.0;

	/** Edge length of a marker cube, in metres. Large, because they are seen from kilometres away. */
	UPROPERTY(EditAnywhere, Category = "SpaceMMO|TestScene")
	double MarkerSizeMetres = 40.0;

private:
	void BuildMarkerPositions();

	void RefreshInstances();

	UPROPERTY(VisibleAnywhere, Category = "SpaceMMO|TestScene")
	TObjectPtr<UInstancedStaticMeshComponent> Markers;

	/** Authoritative marker positions, in system space. These never change. */
	TArray<FSystemCoordinate> MarkerPositions;

	/**
	 * Render-origin revision the instances were last built against.
	 *
	 * Instances are rebuilt only when the origin actually moves. Between rebases the origin is
	 * fixed, so the markers' Unreal transforms are still correct and rewriting them every frame
	 * would be pure cost — for a lattice of a few hundred, on every tick, that adds up.
	 */
	int32 BuiltAtRevision = -1;
};
