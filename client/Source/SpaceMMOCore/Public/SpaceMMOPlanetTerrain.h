#pragma once

#include "CoreMinimal.h"
#include "SpaceMMOPlanet.h"
#include "SpaceMMOPlanetTerrain.generated.h"

/**
 * What a planet's surface looks like, as authored content.
 *
 * Terrain is a <em>function</em>, not a heightmap asset. Nothing is stored, so the same surface
 * exists on the server, on every client, and in a test, without shipping a single byte of it
 * (ADR-0002). Changing a value here changes the whole planet.
 */
USTRUCT(BlueprintType)
struct SPACEMMOCORE_API FPlanetTerrainConfig
{
	GENERATED_BODY()

	/** Decorrelates one planet's surface from another's. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Terrain")
	int64 Seed = 1;

	/**
	 * Highest the surface rises above the planet's nominal radius, in kilometres.
	 *
	 * Modest by design. Everest is 8.8 km on a 6371 km Earth — barely a thousandth of the radius.
	 * Terrain that reads as dramatic from the ground is nearly invisible from orbit, and building
	 * it the other way round makes a planet look like a golf ball.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Terrain")
	double MaxElevationKilometres = 0.5;

	/** How many layers of detail are summed. Each is finer and weaker than the last. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Terrain")
	int32 Octaves = 5;

	/** Features per planet radius at the coarsest octave. Low values give continents. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Terrain")
	double BaseFrequency = 2.0;

	/** Frequency multiplier per octave. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Terrain")
	double Lacunarity = 2.0;

	/** Amplitude multiplier per octave. Below 1, or the fine detail drowns the coarse shape. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Terrain")
	double Gain = 0.5;
};

/**
 * The result of resolving something against the ground.
 */
USTRUCT(BlueprintType)
struct SPACEMMOCORE_API FGroundContact
{
	GENERATED_BODY()

	/** True if the object was touching or below the surface and had to be resolved. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Terrain")
	bool bOnGround = false;

	/** Position after resolution. Unchanged when airborne. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Terrain")
	FSystemCoordinate Position;

	/** Velocity after resolution. Unchanged when airborne. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Terrain")
	FVector Velocity = FVector::ZeroVector;

	/** Outward normal of the ground beneath, whether or not it was touched. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Terrain")
	FVector SurfaceNormal = FVector::UpVector;

	/** How fast the object was moving into the ground before resolution, in cm/s. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Terrain")
	double ImpactSpeed = 0.0;
};

/**
 * The shape of a planet's surface.
 *
 * <strong>Height is sampled from a three-dimensional direction, not from a per-face coordinate.</strong>
 * That one decision is why this has no seams. The usual way to texture a cube-sphere is to give
 * each of the six faces a 2D coordinate and sample noise on it — and then the faces disagree along
 * every shared edge, and the fix is a pile of special cases at edges and corners that never quite
 * works at the eight corners where three faces meet.
 *
 * Sampling by direction removes the problem rather than solving it: the input space is the sphere
 * itself, which has no edges, so two points either side of where a cube boundary happens to fall
 * are simply two nearby directions. The cube-sphere becomes purely a <em>tessellation</em>
 * strategy — a way to decide where to put vertices — and stops having anything to do with what
 * the terrain looks like.
 *
 * <strong>Pure, and deliberately so.</strong> The dedicated server has no renderer but still has
 * to know how high the ground is to decide where a player may stand. A terrain that only exists
 * as GPU-generated geometry cannot answer that. This can, from either side, in a test, with no
 * world and no actor.
 */
class SPACEMMOCORE_API FPlanetTerrain
{
public:
	/**
	 * Elevation above the planet's nominal radius, in kilometres.
	 *
	 * @param Direction Any non-zero vector from the planet centre. Normalised internally, so
	 *                  callers may pass an unnormalised offset without thinking about it.
	 * @return A value in <c>[0, MaxElevationKilometres]</c>. Never negative: the nominal radius is
	 *         the sea floor, not the average, so a planet never has terrain inside its own
	 *         gravity-defining radius.
	 */
	static double ElevationKilometres(const FPlanetTerrainConfig& Terrain, const FVector& Direction);

	/** Distance from the planet centre to the ground, in kilometres. */
	static double SurfaceRadiusKilometres(
		const FPlanetConfig& Planet,
		const FPlanetTerrainConfig& Terrain,
		const FVector& Direction);

	/**
	 * Height of a point above the ground beneath it, in kilometres. Negative means underground.
	 *
	 * This is what a server checks a player's position against, and what a landing sequence
	 * measures. Distinct from <see cref="FPlanetPhysics::AltitudeKilometres"/>, which measures
	 * against the smooth sphere and is what gravity uses — gravity should not vary with whether a
	 * ship happens to be over a mountain.
	 */
	static double AltitudeAboveGroundKilometres(
		const FPlanetConfig& Planet,
		const FPlanetTerrainConfig& Terrain,
		const FSystemCoordinate& Position);

	/**
	 * The outward normal of the ground at a direction.
	 *
	 * Computed by sampling the height a small angle either side and taking the cross product of
	 * the two resulting tangents, rather than returning the radial direction. On a slope those are
	 * not the same thing, and it is the difference between a ship settling onto a hillside at the
	 * angle of the hill and standing bolt upright through it.
	 *
	 * @param SampleAngleDegrees How far apart to sample. Too small and floating point noise
	 *                           dominates the difference; too large and a hill is averaged flat.
	 */
	static FVector SurfaceNormal(
		const FPlanetConfig& Planet,
		const FPlanetTerrainConfig& Terrain,
		const FVector& Direction,
		double SampleAngleDegrees = 0.05);

	/**
	 * Stops something passing through the ground.
	 *
	 * <strong>This, not the mesh, is what collision means here.</strong> The dedicated server has
	 * no mesh to collide against and must still agree about where a player may stand, so contact
	 * has to be a function of position — the same function the client tessellates. Colliding
	 * against generated triangles would give the server and the client two different surfaces and
	 * no way to notice they had diverged.
	 *
	 * Fully inelastic: the inward component of velocity is removed rather than reflected, so a
	 * ship settles rather than bouncing. Tangential motion is left alone, so it can still slide
	 * along a slope, and the impact speed is reported so a caller can decide whether that was a
	 * landing or a crash.
	 *
	 * @param ContactRadiusKilometres How far the object's surface is from its centre.
	 */
	static FGroundContact ResolveContact(
		const FPlanetConfig& Planet,
		const FPlanetTerrainConfig& Terrain,
		const FSystemCoordinate& Position,
		const FVector& Velocity,
		double ContactRadiusKilometres);

	/**
	 * Maps a point on a unit cube to the unit sphere.
	 *
	 * Used for tessellation: walking a regular grid across each cube face and pushing it out to
	 * the sphere gives far more even triangles than subdividing an icosahedron or, worse,
	 * a latitude-longitude grid, which crowds every vertex it has at the poles.
	 *
	 * This is the spherified-cube mapping rather than a plain normalise. Normalising bunches
	 * vertices toward face centres and stretches them at corners; this spreads them.
	 *
	 * @param CubePoint A point on the surface of the cube spanning -1..1 on each axis.
	 */
	static FVector CubeToSphere(const FVector& CubePoint);

	/**
	 * Fractal noise on the unit sphere, in <c>[0, 1]</c>.
	 *
	 * Exposed because it is the piece worth testing on its own: if this is not continuous, every
	 * planet has cliffs in it, and if it is not deterministic, the server and the client disagree
	 * about where the ground is.
	 */
	static double FractalNoise(const FPlanetTerrainConfig& Terrain, const FVector& UnitDirection);

private:
	/** SplitMix64's finalising mix. Mirrors SpaceMMO.Domain's SplitMix64 so both sides agree. */
	static uint64 Mix(uint64 Value);

	/** Deterministic value in [0, 1) for an integer lattice point. */
	static double LatticeValue(int64 X, int64 Y, int64 Z, uint64 Seed);

	/** Trilinearly interpolated value noise at a point in space. */
	static double ValueNoise(const FVector& Point, uint64 Seed);
};
