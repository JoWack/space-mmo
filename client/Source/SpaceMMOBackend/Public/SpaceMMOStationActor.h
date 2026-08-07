#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SpaceMMOBackendTypes.h"
#include "SpaceMMOPlanet.h"
#include "SpaceMMOPlanetTerrain.h"
#include "SpaceMMOStationActor.generated.h"

class UStaticMeshComponent;

/**
 * A station in the world, wherever the server said it is.
 *
 * <strong>Placed two ways, and told which.</strong> A station on a body is put on the ground by
 * evaluating the same height function the terrain mesh and the physics use, so it stands on the
 * surface rather than at a transmitted altitude that could disagree with it. A station that
 * orbits nothing is placed at its system coordinate directly, because there is no ground to
 * stand on.
 *
 * Like everything with a system coordinate, its Unreal transform is recomputed against the render
 * origin rather than stored, so it holds still while the ship flies past and the origin jumps
 * beneath it.
 */
UCLASS()
class SPACEMMOBACKEND_API ASpaceMMOStationActor : public AActor
{
	GENERATED_BODY()

public:
	ASpaceMMOStationActor();

	virtual void Tick(float DeltaSeconds) override;

	/**
	 * Works out where this station stands and moves it there.
	 *
	 * The planet is passed in rather than looked up so that a station on a body cannot be placed
	 * against a different planet's radius than the one it is standing on.
	 */
	void Configure(
		const FBackendStation& InStation,
		const FPlanetConfig& InPlanet,
		const FPlanetTerrainConfig& InTerrain);

	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Station")
	const FBackendStation& GetStation() const { return Station; }

	/** Where it ended up, in system space. */
	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Station")
	FSystemCoordinate GetSystemPosition() const { return SystemPosition; }

	/**
	 * Whether a point is close enough to dock.
	 *
	 * Pure and static so the same question is answered identically here and anywhere else that
	 * needs it — a client that drew "dock available" on a different rule than the server enforces
	 * would offer a button that refuses.
	 */
	static bool IsWithinDockingRange(
		const FBackendStation& Station,
		const FSystemCoordinate& StationPosition,
		const FSystemCoordinate& Position);

protected:
	virtual void BeginPlay() override;

private:
	void ApplyRenderTransform();

	FBackendStation Station;

	FPlanetConfig Planet;

	FPlanetTerrainConfig Terrain;

	FSystemCoordinate SystemPosition;

	UPROPERTY(VisibleAnywhere, Category = "SpaceMMO|Station")
	TObjectPtr<UStaticMeshComponent> Hull;

	/** Render-origin revision the transform was last built against. */
	int32 BuiltAtRevision = -1;
};
