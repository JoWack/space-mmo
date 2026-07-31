#pragma once

#include "CoreMinimal.h"
#include "SpaceMMOCoordinates.h"
#include "SpaceMMOPhysicsGrid.generated.h"

/**
 * Where a grid sits in system space, resolved through its whole parent chain.
 */
USTRUCT(BlueprintType)
struct SPACEMMOCORE_API FPhysicsGridPose
{
	GENERATED_BODY()

	/** Position in system space, in kilometres. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Grid")
	FSystemCoordinate Origin;

	/** Orientation in system space. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Grid")
	FQuat Rotation = FQuat::Identity;

	FPhysicsGridPose() = default;

	FPhysicsGridPose(const FSystemCoordinate& InOrigin, const FQuat& InRotation)
		: Origin(InOrigin), Rotation(InRotation)
	{
	}
};

/**
 * Nested reference frames, per ADR-0001.
 *
 * Every object belongs to a grid, and grids nest: a crate sits in a ship's interior, the interior
 * sits in the ship, the ship sits near a planet, the planet sits in a star system. Only the
 * player's *active* grid is simulated near the world origin; everything else is transformed in for
 * rendering.
 *
 * This is what makes walking around inside a moving ship an ordinary problem. In a single world
 * space it is a hard one — the floor is travelling at some fraction of light speed and the physics
 * solver has to reconcile that against a character stepping at walking pace. In the ship's own
 * frame the floor is not moving at all.
 *
 * Deliberately plain C++ rather than a UObject or subsystem, so the maths can be tested without
 * spinning up a world. A subsystem wrapper can come later if Blueprint needs one.
 */
class SPACEMMOCORE_API FPhysicsGridRegistry
{
public:
	/** Identifies a grid. INDEX_NONE means none. */
	using FGridId = int32;

	static constexpr FGridId InvalidGrid = INDEX_NONE;

	/**
	 * How deep a chain may go before it is treated as a bug.
	 *
	 * Real chains are four or five deep. A runaway one means a cycle or a construction mistake, and
	 * failing loudly beats spinning forever inside a resolve that runs every frame.
	 */
	static constexpr int32 MaxDepth = 32;

	/**
	 * Adds a root grid, positioned directly in system space.
	 *
	 * Roots are the things a star system contains in its own right: planets, stations, and the
	 * system barycentre itself.
	 */
	FGridId AddRoot(FName DebugName, const FSystemCoordinate& Origin, const FQuat& Rotation = FQuat::Identity);

	/**
	 * Adds a grid nested inside another, offset in centimetres from its parent.
	 *
	 * Centimetres because nested frames are small — a ship is tens of metres, a room is a few. The
	 * kilometre scale only matters between roots, and that is where double precision is spent.
	 */
	FGridId AddChild(
		FName DebugName,
		FGridId ParentId,
		const FVector& OffsetCentimetres,
		const FQuat& Rotation = FQuat::Identity);

	bool IsValidGrid(FGridId Id) const;

	FGridId GetParent(FGridId Id) const;

	FName GetDebugName(FGridId Id) const;

	/** How many links separate a grid from its root. Roots are zero. */
	int32 GetDepth(FGridId Id) const;

	bool IsDescendantOf(FGridId Id, FGridId PossibleAncestor) const;

	/** Moves a child within its parent — a ship drifting relative to a planet, say. */
	void SetLocalOffset(FGridId Id, const FVector& OffsetCentimetres);

	void SetLocalRotation(FGridId Id, const FQuat& Rotation);

	/** Moves a root through system space. */
	void SetRootOrigin(FGridId Id, const FSystemCoordinate& Origin);

	/**
	 * Resolves a grid's absolute pose by walking its parent chain to a root.
	 *
	 * Offsets accumulate in centimetres through the chain and are converted to kilometres only when
	 * added to the root's system position, so nested precision is never spent on the distance to the
	 * star.
	 */
	FPhysicsGridPose ResolveWorldPose(FGridId Id) const;

	/**
	 * The transform that renders one grid while another is the active frame.
	 *
	 * The active grid always comes back as identity — it *is* the origin — which is the property
	 * that keeps Chaos simulating in the range it behaves well in. Everything else is expressed
	 * relative to it.
	 */
	FTransform GetRenderTransform(FGridId Id, FGridId ActiveGrid) const;

	/**
	 * True if a grid is close enough to the active frame to be simulated rather than merely drawn.
	 */
	bool IsWithinSimulationRange(FGridId Id, FGridId ActiveGrid) const;

	int32 Num() const { return Grids.Num(); }

	void Reset() { Grids.Reset(); }

private:
	struct FGrid
	{
		FName DebugName;
		FGridId ParentId = InvalidGrid;

		/** Meaningful only for roots. */
		FSystemCoordinate RootOrigin;

		/** Meaningful only for children. */
		FVector LocalOffsetCentimetres = FVector::ZeroVector;

		FQuat LocalRotation = FQuat::Identity;
	};

	TArray<FGrid> Grids;
};
