#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SpaceMMOWorldSubsystem.generated.h"

/**
 * Builds the world's scenery, on every machine that has one.
 *
 * <strong>Not the game mode.</strong> A game mode exists only on the server, so anything it
 * spawns exists only there — and none of this scenery replicates, so a connected client saw an
 * empty black level with a handful of ship pawns in it and no lights to show them by. That is
 * exactly what happened the first time anyone joined a dedicated server.
 *
 * Replicating it would be the wrong fix. The scene is a deterministic function of its
 * configuration, so every machine can build an identical copy for free, and sending three hundred
 * and fifty marker cubes over the wire to say something both ends already know would be pure
 * cost. This is the same reasoning as ADR-0002: generated content is reproduced, not transmitted.
 *
 * A world subsystem's OnWorldBeginPlay runs on the server and on every client, which is precisely
 * the audience that needs the world to exist.
 */
UCLASS()
class SPACEMMOCORE_API USpaceMMOWorldSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	/**
	 * Applies the lighting console variables when they change.
	 *
	 * Lighting is the one thing here that can only be judged by looking at it, and a rebuild is
	 * minutes. SpaceMMO.KeyLight and SpaceMMO.FillLight are therefore live: type a number in the
	 * console and the scene changes, so finding a value takes seconds rather than a compile each.
	 */
	virtual void Tick(float DeltaTime) override;

	virtual TStatId GetStatId() const override
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(USpaceMMOWorldSubsystem, STATGROUP_Tickables);
	}

	/** The planet every machine agrees on. Spawned here so both sides simulate against it. */
	UFUNCTION(BlueprintCallable, Category = "SpaceMMO|World")
	void BuildScenery();

private:
	UPROPERTY()
	TObjectPtr<class UDirectionalLightComponent> KeyLight;

	UPROPERTY()
	TObjectPtr<class UDirectionalLightComponent> FillLight;

	/** Omnidirectional fill, so no normal on a sphere is ever completely unlit. */
	UPROPERTY()
	TObjectPtr<class USkyLightComponent> AmbientLight;

	/** Owns the manual exposure. Without one the renderer uses a default nobody chose. */
	UPROPERTY()
	TObjectPtr<class APostProcessVolume> Exposure;
};
