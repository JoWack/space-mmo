#include "SpaceMMOPlanetGlobe.h"

#include "SpaceMMOPlanet.h"

const TArray<FPlanetGlobe::FFace>& FPlanetGlobe::Faces()
{
	// Across x Down == Normal for every entry, which is what makes one winding rule work on all six.
	static const TArray<FFace> Six = {
		{ FVector(1.0, 0.0, 0.0), FVector(0.0, 1.0, 0.0), FVector(0.0, 0.0, 1.0) },
		{ FVector(-1.0, 0.0, 0.0), FVector(0.0, 0.0, 1.0), FVector(0.0, 1.0, 0.0) },
		{ FVector(0.0, 1.0, 0.0), FVector(0.0, 0.0, 1.0), FVector(1.0, 0.0, 0.0) },
		{ FVector(0.0, -1.0, 0.0), FVector(1.0, 0.0, 0.0), FVector(0.0, 0.0, 1.0) },
		{ FVector(0.0, 0.0, 1.0), FVector(1.0, 0.0, 0.0), FVector(0.0, 1.0, 0.0) },
		{ FVector(0.0, 0.0, -1.0), FVector(0.0, 1.0, 0.0), FVector(1.0, 0.0, 0.0) },
	};

	return Six;
}

double FPlanetGlobe::VisibleCapDegrees(
	const FPlanetConfig& Planet, const double AltitudeKilometres)
{
	if (Planet.RadiusKilometres <= 0.0 || AltitudeKilometres <= 0.0)
	{
		return 0.0;
	}

	// The tangent from the eye to the sphere touches it at acos(R / (R + h)) from the point
	// underneath. Everything nearer than that is in view; everything past it is over the horizon.
	const double Ratio = Planet.RadiusKilometres / (Planet.RadiusKilometres + AltitudeKilometres);

	return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Ratio, -1.0, 1.0)));
}

namespace
{
	/**
	 * How high and how steep the ground is at one point, each 0..1.
	 *
	 * Height is the fraction of the planet's maximum relief; steepness is one minus the dot of the
	 * surface normal with straight up, so level ground is zero and a cliff approaches one.
	 *
	 * Shared by the globe and the patch deliberately. They are two samplings of one height function
	 * and a material that banded them differently would put a visible line around the patch, which
	 * is the same class of disagreement task 86 exists to prevent.
	 */
	FVector2D GroundKindAt(
		const FPlanetConfig& Planet,
		const FPlanetTerrainConfig& Terrain,
		const FVector& Direction,
		const FVector& Normal,
		const double SurfaceRadiusKilometres)
	{
		const double Rise = SurfaceRadiusKilometres - Planet.RadiusKilometres;

		const double Height = Terrain.MaxElevationKilometres > 0.0
			? FMath::Clamp(Rise / Terrain.MaxElevationKilometres, 0.0, 1.0)
			: 0.0;

		const double Steepness =
			FMath::Clamp(1.0 - FVector::DotProduct(Normal, Direction), 0.0, 1.0);

		return FVector2D(Height, Steepness);
	}
}

FPlanetGlobeMesh FPlanetGlobe::Build(
	const FPlanetConfig& Planet,
	const FPlanetTerrainConfig& Terrain,
	const FPlanetGlobeConfig& Globe)
{
	FPlanetGlobeMesh Result;

	const int32 Resolution = FMath::Clamp(Globe.Resolution, 2, 256);
	const TArray<FFace>& AllFaces = Faces();

	// Normals are read straight from the height function rather than accumulated from the triangles
	// around each vertex. Two reasons, and the first is the important one: the six faces meet along
	// twelve edges where the vertices are duplicated, and an accumulated normal there would only
	// know about its own face's triangles — so every cube edge would light as a visible crease, on
	// a surface whose whole design is that it has no seams. Sampling by direction has no edges to
	// crease along.
	//
	// The sample angle is one cell wide, so the lighting describes the same scale of feature the
	// geometry does. Sampling much finer would light hills the mesh does not have.
	const double SampleAngleDegrees = 90.0 / FMath::Max(Resolution - 1, 1);

	Result.Positions.Reserve(AllFaces.Num() * Resolution * Resolution);
	Result.Normals.Reserve(AllFaces.Num() * Resolution * Resolution);
	Result.Triangles.Reserve(AllFaces.Num() * (Resolution - 1) * (Resolution - 1) * 6);

	for (const FFace& Face : AllFaces)
	{
		const int32 FaceStart = Result.Positions.Num();

		for (int32 Row = 0; Row < Resolution; ++Row)
		{
			const double V = -1.0 + ((2.0 * Row) / (Resolution - 1));

			for (int32 Column = 0; Column < Resolution; ++Column)
			{
				const double U = -1.0 + ((2.0 * Column) / (Resolution - 1));

				// The point on the cube, then out to the sphere. Two faces meeting at an edge
				// generate the identical cube point there and so the identical direction, which is
				// why the seams close exactly rather than nearly.
				const FVector CubePoint = Face.Normal + (Face.Across * U) + (Face.Down * V);
				const FVector Direction = FPlanetTerrain::CubeToSphere(CubePoint);

				const double Radius =
					FPlanetTerrain::SurfaceRadiusKilometres(Planet, Terrain, Direction);

				Result.Positions.Add(
					Direction * Radius * SpaceMMO::Coordinates::CentimetresPerKilometre);

				const FVector Normal = FPlanetTerrain::SurfaceNormal(
					Planet, Terrain, Direction, SampleAngleDegrees);

				Result.Normals.Add(Normal);

				// From the direction, not from this loop's own U and V. The patch computes the same
				// number the same way, so the two agree exactly where one hands over to the other.
				Result.SurfaceUVs.Add(FPlanetTerrain::SurfaceUV(Direction));

				Result.GroundKinds.Add(GroundKindAt(Planet, Terrain, Direction, Normal, Radius));
			}
		}

		for (int32 Row = 0; Row < Resolution - 1; ++Row)
		{
			for (int32 Column = 0; Column < Resolution - 1; ++Column)
			{
				const int32 TopLeft = FaceStart + (Row * Resolution) + Column;
				const int32 TopRight = TopLeft + 1;
				const int32 BottomLeft = TopLeft + Resolution;
				const int32 BottomRight = BottomLeft + 1;

				// Stepping along Across before Down, so the face normal comes out along
				// Across x Down, which every entry in Faces() defines to be outward.
				Result.Triangles.Add(TopLeft);
				Result.Triangles.Add(TopRight);
				Result.Triangles.Add(BottomLeft);

				Result.Triangles.Add(TopRight);
				Result.Triangles.Add(BottomRight);
				Result.Triangles.Add(BottomLeft);
			}
		}
	}

	return Result;
}
