#pragma once

#include "CoreMinimal.h"
#include "SpaceMMOPlanetTerrain.h"
#include "SpaceMMOPlanetPatch.generated.h"

/**
 * One tessellated piece of a planet's surface.
 *
 * The landing-zone approach: rather than meshing a whole globe, mesh the patch a ship is actually
 * approaching. From orbit the planet stays the sphere it already is; on final approach one patch
 * of real ground appears under the landing site. That covers everything a player can reach for a
 * fraction of the work of a full cube-sphere with runtime LOD — and because the heights come from
 * the same pure function either way, a full sphere can be added later without the terrain
 * changing shape underneath anyone.
 */
USTRUCT(BlueprintType)
struct SPACEMMOCORE_API FPlanetPatchConfig
{
	GENERATED_BODY()

	/** Direction from the planet centre to the middle of the patch. Normalised on use. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Terrain")
	FVector CentreDirection = FVector(1.0, 0.0, 0.0);

	/**
	 * Half-width of the patch, in degrees of arc from its centre.
	 *
	 * On a 20 km planet, 10 degrees is roughly 3.5 km across — a comfortable landing zone. The
	 * tangent-plane parameterisation distorts badly past about 45 degrees, which is the practical
	 * limit before this needs to become six proper cube faces.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Terrain")
	double AngularRadiusDegrees = 4.0;

	/** Vertices along each edge. The patch is this squared, so raising it is expensive. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Terrain")
	int32 Resolution = 129;
};

/**
 * A generated patch, in local centimetres ready to hand to a mesh component.
 */
struct SPACEMMOCORE_API FPlanetPatchMesh
{
	/** Positions in centimetres, relative to <see cref="Origin"/>. */
	TArray<FVector> Positions;

	/** Outward vertex normals. */
	TArray<FVector> Normals;

	/** Triangle indices, three per triangle, wound counter-clockwise seen from outside. */
	TArray<int32> Triangles;

	/**
	 * The patch's anchor in system space.
	 *
	 * Positions are relative to this rather than absolute, because a patch on a planet 200 km away
	 * would otherwise be a set of vertices hundreds of kilometres from the origin — precisely the
	 * single-precision problem the whole coordinate model exists to avoid (ADR-0001).
	 */
	FSystemCoordinate Origin;

	bool IsValid() const { return Positions.Num() > 0 && Triangles.Num() > 0; }
};

/**
 * Turns a planet's height function into triangles.
 *
 * Pure, and separate from any actor, for the same reason the terrain function is: this is where
 * the arithmetic lives, so this is where the bugs live, and neither needs a world to be wrong in.
 */
class SPACEMMOCORE_API FPlanetPatch
{
public:
	/**
	 * The direction to a point on the patch.
	 *
	 * @param U,V Patch coordinates in -1..1, with (0,0) the centre.
	 *
	 * Parameterised on a tangent plane built from the centre direction, rather than on latitude
	 * and longitude. A lat-long grid crowds every vertex it has at the poles and cannot be used
	 * near one at all; a tangent frame behaves identically wherever the patch happens to be, which
	 * matters because a player may land anywhere.
	 */
	static FVector DirectionAt(
		const FVector& CentreDirection, double AngularRadiusDegrees, double U, double V);

	/** Tessellates the patch against the planet's terrain. */
	static FPlanetPatchMesh Build(
		const FPlanetConfig& Planet,
		const FPlanetTerrainConfig& Terrain,
		const FPlanetPatchConfig& Patch);

	/**
	 * Whether a patch centred one way still covers a viewer who has moved another.
	 *
	 * Rebuilding every frame would be wasteful and rebuilding never would leave a player walking
	 * off the edge of the world, so the question is how far they may drift before the patch under
	 * them is regenerated around their new position.
	 *
	 * The threshold is a fraction of the patch's own angular radius rather than a fixed angle,
	 * because a wider patch can tolerate more drift by definition. Kept well below 1 so the rebuild
	 * happens while there is still ground ahead, not once the player has reached the edge.
	 *
	 * @param DriftFraction How far, as a fraction of the angular radius, a viewer may move first.
	 */
	static bool ShouldRebuild(
		const FVector& PatchDirection,
		const FVector& ViewerDirection,
		double AngularRadiusDegrees,
		double DriftFraction = 0.4);

	/**
	 * An orthonormal basis whose Z is the given direction.
	 *
	 * Exposed because choosing the reference axis badly is the classic way this breaks: crossing
	 * with a fixed axis produces a zero vector when the direction happens to be that axis, and the
	 * patch collapses at exactly the two poles nobody tests.
	 */
	static void BuildTangentFrame(
		const FVector& Direction, FVector& OutTangent, FVector& OutBitangent);
};
