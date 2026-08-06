#pragma once

#include "CoreMinimal.h"
#include "SpaceMMOPlanetTerrain.h"
#include "SpaceMMOPlanetGlobe.generated.h"

/**
 * How finely to tessellate a whole planet.
 */
USTRUCT(BlueprintType)
struct SPACEMMOCORE_API FPlanetGlobeConfig
{
	GENERATED_BODY()

	/**
	 * Vertices along each edge of each of the six cube faces.
	 *
	 * The globe costs six times this squared, so it grows fast: 96 is about 55,000 vertices and
	 * 109,000 triangles, built once when the planet appears. What it buys is angular resolution —
	 * each face spans 90 degrees, so 96 puts a vertex every 0.95 degrees, which on a 20 km planet
	 * is roughly 330 metres of ground.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Terrain")
	int32 Resolution = 96;
};

/**
 * A whole planet's surface, in local centimetres relative to the planet's centre.
 */
struct SPACEMMOCORE_API FPlanetGlobeMesh
{
	TArray<FVector> Positions;

	TArray<FVector> Normals;

	/** Three indices per triangle, wound counter-clockwise seen from outside. */
	TArray<int32> Triangles;

	bool IsValid() const { return Positions.Num() > 0 && Triangles.Num() > 0; }
};

/**
 * Tessellates an entire planet from its height function.
 *
 * <strong>This is the same surface the terrain patch draws, at a coarser sampling.</strong> Both
 * call <see cref="FPlanetTerrain::SurfaceRadiusKilometres"/>, so the globe cannot drift away from
 * the ground a player walks on the way a separately-authored low-detail mesh would. What replaced
 * it — <c>/Engine/BasicShapes/Sphere</c> — had around thirty segments, which is a sphere at a metre
 * across and a polyhedron at twenty kilometres, with flat faces cutting kilometres through the real
 * surface between their vertices.
 *
 * Pure and actor-free, like the patch builder and the terrain function, so it can be checked
 * without a world.
 */
class SPACEMMOCORE_API FPlanetGlobe
{
public:
	/** The six cube faces, as an outward axis and the two axes spanning the face. */
	struct FFace
	{
		FVector Normal;

		/** Across the face. Chosen so that <c>Across x Down</c> is the outward normal. */
		FVector Across;

		FVector Down;
	};

	/**
	 * The six faces in a fixed order.
	 *
	 * Each is right-handed with the outward direction third, which is what lets every face use the
	 * same triangle winding instead of six special cases — and getting one of them backwards would
	 * give a planet with a patch of surface lit from the inside.
	 */
	static const TArray<FFace>& Faces();

	/**
	 * Tessellates the planet.
	 *
	 * Vertices are relative to the planet's centre rather than absolute, for the reason every other
	 * mesh here is: a planet 200 km out would otherwise be a set of coordinates far enough from the
	 * origin to lose precision in single-precision rendering (ADR-0001).
	 */
	static FPlanetGlobeMesh Build(
		const FPlanetConfig& Planet,
		const FPlanetTerrainConfig& Terrain,
		const FPlanetGlobeConfig& Globe);

	/**
	 * Half-angle of the surface a viewer can actually see, in degrees.
	 *
	 * Zero on the ground and rising with altitude: from 12 km above a 20 km planet slightly over
	 * half a hemisphere is in view. This is what a terrain patch has to span to be the only surface
	 * worth drawing, which is how the globe and the patch avoid ever having to agree on screen —
	 * only one of them is visible at a time.
	 */
	static double VisibleCapDegrees(const FPlanetConfig& Planet, double AltitudeKilometres);
};
