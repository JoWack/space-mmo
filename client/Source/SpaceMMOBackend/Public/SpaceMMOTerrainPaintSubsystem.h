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
UCLASS()
class SPACEMMOBACKEND_API USpaceMMOTerrainPaintSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

private:
	UFUNCTION()
	void HandleBodiesLoaded();

	/** Applies whatever the backend currently knows, to every planet in the world. */
	void PaintPlanets();
};
