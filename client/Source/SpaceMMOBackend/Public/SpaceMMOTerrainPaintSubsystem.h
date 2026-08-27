#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "SpaceMMOTerrainPaintSubsystem.generated.h"

/**
 * Paints each planet with the palette its body was authored with.
 *
 * <strong>Here rather than in the planet actor.</strong> A planet's look is content and arrives over
 * HTTP; SpaceMMOCore knows nothing about either, and keeping it that way is why the game mode
 * resolves its player controller by path. So Core owns a material and a setter, and this owns
 * knowing which body a planet is and what the backend said about it.
 *
 * <strong>Driven by the fetch, not by construction.</strong> Bodies land well after the planet is
 * built, so painting at BeginPlay would paint from an empty list. That is the same ordering that put
 * a character 121 km above the ground when a spawn assumed the planet already existed (task 120).
 */
/** Broadcast when the planets have the shape they are going to keep. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSpaceMMOPlanetsPainted);

UCLASS()
class SPACEMMOBACKEND_API USpaceMMOTerrainPaintSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	/**
	 * Fires once the planets have the shape they are going to keep.
	 *
	 * <strong>Anything that places itself on the ground has to wait for this.</strong> Terrain
	 * arrives from content over HTTP, and until it does the planet is wearing the compiled-in
	 * default. Something positioned against that default and never re-derived is left floating or
	 * buried by the difference between two height fields — about a hundred metres, on the capital.
	 */
	UPROPERTY(BlueprintAssignable, Category = "SpaceMMO|Terrain")
	FSpaceMMOPlanetsPainted OnPlanetsPainted;

	/** True once the planets have been shaped from content, or settled without any to apply. */
	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Terrain")
	bool HavePlanetsSettled() const { return bPlanetsSettled; }

private:
	UFUNCTION()
	void HandleBodiesLoaded();

	/** Applies whatever the backend currently knows, to every planet in the world. */
	void PaintPlanets();

	/**
	 * Set once bodies have actually been available to paint from.
	 *
	 * Not set by the speculative call at world begin play, which runs before anything has arrived:
	 * treating "nothing to do yet" as "settled" is how a gate waiting on this would open early and
	 * reintroduce exactly the race it exists to close.
	 */
	bool bPlanetsSettled = false;
};
