#pragma once

#include "CoreMinimal.h"
#include "SpaceMMOCoordinates.h"
#include "Subsystems/WorldSubsystem.h"
#include "SpaceMMORenderOrigin.generated.h"

/**
 * The single source of truth for which system-space position currently maps to Unreal's world
 * origin.
 *
 * Everything drawn from a system-space position needs the same answer, or objects disagree about
 * where they are relative to each other. Holding it on the ship would work for exactly one ship
 * and nothing else in the world, so it lives here instead.
 *
 * The piloted ship writes it; anything rendering from system space reads it (ADR-0001).
 */
UCLASS()
class SPACEMMOCORE_API USpaceMMORenderOriginSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/**
	 * The system position that currently maps to Unreal's world origin.
	 *
	 * The single per-client answer to "what is world zero?". Anything drawn from a system
	 * coordinate must resolve against <em>this</em> rather than its own idea of an origin —
	 * otherwise two ships carrying different origins are drawn in two different frames of
	 * reference and appear nowhere near each other.
	 */
	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Coordinates")
	FSystemCoordinate GetRenderOrigin() const { return RenderOrigin; }

	/**
	 * Moves the render origin.
	 *
	 * Called by whatever the camera is attached to, once per rebase.
	 */
	UFUNCTION(BlueprintCallable, Category = "SpaceMMO|Coordinates")
	void SetRenderOrigin(const FSystemCoordinate& NewOrigin);

	/**
	 * Increments every time the origin moves.
	 *
	 * Cheaper for observers to compare than the coordinate itself, and it makes "has this changed
	 * since I last looked?" a single integer test rather than a floating-point comparison with a
	 * tolerance nobody agrees on.
	 */
	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Coordinates")
	int32 GetRevision() const { return Revision; }

	/** Where to draw something whose authoritative position is a system coordinate. */
	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Coordinates")
	FVector ToWorldLocation(const FSystemCoordinate& Position) const
	{
		return Position.ToLocalCentimetres(RenderOrigin);
	}

private:
	FSystemCoordinate RenderOrigin;

	int32 Revision = 0;
};
