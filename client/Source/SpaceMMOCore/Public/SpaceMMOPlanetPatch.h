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

	/**
	 * Vertex spacing at the centre, as a fraction of what an evenly spaced grid of the same
	 * resolution would have there. 1 is the even grid.
	 *
	 * <strong>Fine where a player stands, coarse where they only look (task 164).</strong> An even
	 * grid spends as many vertices on a hillside a kilometre away as on the ground under the
	 * character, and a player judges the two very differently: half a metre of disagreement is a
	 * buried pair of boots at your feet and nothing at all on the horizon. A quarter puts a vertex
	 * every 5.5 m under a walker for the same 129 squared the even grid used, where more vertices
	 * would have cost more than the frame -- 58 ms to rebuild at 257, 240 ms at 513, measured.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Terrain")
	double CentreSpacing = 1.0;

	/**
	 * How far out the centre's spacing holds before it starts to widen, as a fraction of the
	 * vertices from the centre to the edge. Past it the spacing grows steadily to the rim, which is
	 * where the vertices saved at the centre are paid back.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Terrain")
	double FineExtent = 0.4;
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

	/**
	 * Surface coordinates for texturing, per vertex.
	 *
	 * The parameterisation was always here and was thrown away: the builder computes a U and a V to
	 * place every vertex and then discarded both. Kept in 0..1 rather than pre-multiplied by a tiling
	 * factor, so the material owns how large a texture reads and this owns only where a point is.
	 */
	TArray<FVector2D> SurfaceUVs;

	/**
	 * What the ground is like at each vertex: X is height, Y is steepness, both 0..1.
	 *
	 * Height is the fraction of the planet's maximum relief, so 0 is the nominal radius and 1 is the
	 * highest ground can go. Steepness is the sine of the slope angle: 0 level, 1 vertical, and 0.5
	 * at thirty degrees. Sine rather than 1 - cos because the latter reads 0.15 on a 32 degree
	 * hillside, which is invisible.
	 *
	 * Computed here rather than in the material because both numbers come from the height function,
	 * which the shader has no access to -- and deriving steepness from a world normal in the shader
	 * would need the planet's centre passed in as a parameter, which is one more thing that can be
	 * set to the wrong value (see task 120, which lost an afternoon to exactly that).
	 */
	TArray<FVector2D> GroundKinds;

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

	/**
	 * Where the grid's Index-th row or column falls, in patch coordinates -1..1.
	 *
	 * Even steps when the patch's CentreSpacing is 1, which is exactly the grid every patch used
	 * before task 164. Otherwise even steps of CentreSpacing out to FineExtent, then steps growing
	 * linearly so the last one still lands on the rim: the patch covers the same ground either way,
	 * and only where its vertices fall changes.
	 */
	static double GridCoordinate(const FPlanetPatchConfig& Patch, int32 Index);

	/**
	 * The drawn ground's distance from the planet's centre along a direction, in kilometres.
	 *
	 * <strong>The mesh between its vertices, which nothing measured before task 164.</strong> Every
	 * vertex is on the height function by construction, so a check of the vertices passes whatever
	 * the faces do -- and the faces are what a character stands beside. This reads the triangle the
	 * direction passes through, from the same four corners Build places, so it is the drawn surface
	 * rather than an estimate of it; SpaceMMO.Patch.DrawnGroundIsTheBuiltMesh holds the two
	 * together.
	 *
	 * @return False when the direction falls outside the patch.
	 */
	static bool DrawnRadiusKilometres(
		const FPlanetConfig& Planet,
		const FPlanetTerrainConfig& Terrain,
		const FPlanetPatchConfig& Patch,
		const FVector& Direction,
		double& OutRadiusKilometres);

	/**
	 * How far out from its centre a patch keeps its finest spacing, in degrees of arc.
	 *
	 * Taken as where the spacing has grown half again past the centre's. The whole patch for an
	 * even grid, which never grows.
	 */
	static double FineRadiusDegrees(const FPlanetPatchConfig& Patch);

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
	 *                      Down to a hundredth: a graded patch keeps its fine ground within a tenth
	 *                      of its radius, and a walker has to be rebuilt around before leaving it.
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
