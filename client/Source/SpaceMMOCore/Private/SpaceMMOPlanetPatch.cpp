#include "SpaceMMOPlanetPatch.h"

namespace
{
	/**
	 * The graded grid's shape, as a map from index coordinates to patch coordinates, both -1..1.
	 *
	 * The spacing is even out to Fine and then grows linearly, and the growth is whatever makes the
	 * last step land exactly on the rim. Written as the integral of that spacing, which is why the
	 * inverse is a quadratic rather than a search.
	 */
	struct FGridGrading
	{
		double Fine = 0.4;
		double Growth = 0.0;
		double Total = 1.0;

		explicit FGridGrading(const FPlanetPatchConfig& Patch)
		{
			// A floor on the centre spacing, because a grid squeezed to nothing at its centre puts
			// every vertex it saved into a rim so coarse it stops describing the ground at all.
			const double CentreSpacing = FMath::Clamp(Patch.CentreSpacing, 0.05, 1.0);

			Fine = FMath::Clamp(Patch.FineExtent, 0.0, 0.95);
			Total = 1.0 / CentreSpacing;
			Growth = 2.0 * (Total - 1.0) / FMath::Square(1.0 - Fine);
		}

		/** Index coordinate to patch coordinate. */
		double Warp(const double S) const
		{
			const double X = FMath::Min(FMath::Abs(S), 1.0);

			const double Integral = X <= Fine
				? X
				: X + (0.5 * Growth * FMath::Square(X - Fine));

			return FMath::Sign(S) * (Integral / Total);
		}

		/** Patch coordinate back to index coordinate: the quadratic Warp solves, taken exactly. */
		double Unwarp(const double U) const
		{
			const double Integral = FMath::Min(FMath::Abs(U), 1.0) * Total;

			if (Integral <= Fine || Growth <= 0.0)
			{
				return FMath::Sign(U) * Integral;
			}

			const double PastFine =
				(-1.0 + FMath::Sqrt(1.0 + (2.0 * Growth * (Integral - Fine)))) / Growth;

			return FMath::Sign(U) * (Fine + PastFine);
		}
	};

	/** Distance from the planet's centre along a unit direction to the plane through three points. */
	double AlongToPlane(const FVector& A, const FVector& B, const FVector& C, const FVector& Unit)
	{
		const FVector Normal = FVector::CrossProduct(B - A, C - A);

		const double Facing = FVector::DotProduct(Normal, Unit);

		// A face seen exactly edge-on from the planet's centre. Never built -- the patch is a height
		// field over directions, so every face crosses every ray through it -- but a division by
		// zero would answer with infinity rather than with nothing.
		return FMath::IsNearlyZero(Facing) ? 0.0 : FVector::DotProduct(Normal, A) / Facing;
	}
}

void FPlanetPatch::BuildTangentFrame(
	const FVector& Direction, FVector& OutTangent, FVector& OutBitangent)
{
	const FVector Up = Direction.GetSafeNormal();

	// The reference axis is chosen to be the one the direction points along least. Crossing with a
	// fixed axis produces a zero vector whenever the direction happens to equal it, which collapses
	// the patch at exactly the two poles nobody thinks to test.
	const FVector Reference =
		FMath::Abs(Up.Z) < 0.9 ? FVector(0.0, 0.0, 1.0) : FVector(1.0, 0.0, 0.0);

	OutTangent = FVector::CrossProduct(Reference, Up).GetSafeNormal();
	OutBitangent = FVector::CrossProduct(Up, OutTangent).GetSafeNormal();
}

bool FPlanetPatch::ShouldRebuild(
	const FVector& PatchDirection,
	const FVector& ViewerDirection,
	const double AngularRadiusDegrees,
	const double DriftFraction)
{
	// No patch yet, or a viewer with no meaningful direction: build one.
	if (PatchDirection.IsNearlyZero() || ViewerDirection.IsNearlyZero())
	{
		return true;
	}

	const double Cosine = FVector::DotProduct(
		PatchDirection.GetSafeNormal(), ViewerDirection.GetSafeNormal());

	// Clamped before the arc cosine: floating point routinely produces dot products a hair outside
	// -1..1 for nearly parallel vectors, and acos of 1.0000000001 is NaN — which would compare
	// false against any threshold and silently stop rebuilding forever.
	const double DriftDegrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Cosine, -1.0, 1.0)));

	return DriftDegrees > AngularRadiusDegrees * FMath::Clamp(DriftFraction, 0.01, 0.9);
}

double FPlanetPatch::GridCoordinate(const FPlanetPatchConfig& Patch, const int32 Index)
{
	const int32 Resolution = FMath::Clamp(Patch.Resolution, 2, 512);

	const double Even = -1.0 + ((2.0 * Index) / (Resolution - 1));

	return FGridGrading(Patch).Warp(Even);
}

double FPlanetPatch::FineRadiusDegrees(const FPlanetPatchConfig& Patch)
{
	const FGridGrading Grading(Patch);

	if (Grading.Growth <= 0.0)
	{
		return Patch.AngularRadiusDegrees;
	}

	// Where the spacing, 1 + Growth * (X - Fine) in units of the centre's, reaches one and a half.
	const double Index = FMath::Min(Grading.Fine + (0.5 / Grading.Growth), 1.0);

	const double Extent = FMath::Tan(FMath::DegreesToRadians(
		FMath::Clamp(Patch.AngularRadiusDegrees, 0.0001, 75.0)));

	return FMath::RadiansToDegrees(FMath::Atan(Grading.Warp(Index) * Extent));
}

bool FPlanetPatch::DrawnRadiusKilometres(
	const FPlanetConfig& Planet,
	const FPlanetTerrainConfig& Terrain,
	const FPlanetPatchConfig& Patch,
	const FVector& Direction,
	double& OutRadiusKilometres)
{
	const FVector Centre = Patch.CentreDirection.GetSafeNormal();
	const FVector Unit = Direction.GetSafeNormal();

	const double Along = FVector::DotProduct(Unit, Centre);

	if (Centre.IsNearlyZero() || Along <= 0.0)
	{
		return false;
	}

	FVector Tangent;
	FVector Bitangent;
	BuildTangentFrame(Centre, Tangent, Bitangent);

	// DirectionAt run backwards. A gnomonic grid is a projection from the planet's centre, so a
	// direction's patch coordinates do not depend on how far out along it the ground happens to be.
	const double Extent = FMath::Tan(FMath::DegreesToRadians(
		FMath::Clamp(Patch.AngularRadiusDegrees, 0.0001, 75.0)));

	const double U = FVector::DotProduct(Unit, Tangent) / (Along * Extent);
	const double V = FVector::DotProduct(Unit, Bitangent) / (Along * Extent);

	if (FMath::Abs(U) > 1.0 || FMath::Abs(V) > 1.0)
	{
		return false;
	}

	const int32 Resolution = FMath::Clamp(Patch.Resolution, 2, 512);
	const FGridGrading Grading(Patch);

	auto CellOf = [&Grading, Resolution](const double Coordinate)
	{
		const double Index = (Grading.Unwarp(Coordinate) + 1.0) * 0.5 * (Resolution - 1);

		return FMath::Clamp(FMath::FloorToInt32(Index), 0, Resolution - 2);
	};

	const int32 Column = CellOf(U);
	const int32 Row = CellOf(V);

	const double U0 = GridCoordinate(Patch, Column);
	const double U1 = GridCoordinate(Patch, Column + 1);
	const double V0 = GridCoordinate(Patch, Row);
	const double V1 = GridCoordinate(Patch, Row + 1);

	// Which of the cell's two triangles, decided in patch coordinates. The projection maps each
	// face onto a triangle there, and the diagonal Build splits on runs from the cell's top-right
	// corner to its bottom-left one.
	const double Across = (U - U0) / (U1 - U0);
	const double Down = (V - V0) / (V1 - V0);

	auto Corner = [&](const double AtU, const double AtV)
	{
		const FVector At = DirectionAt(Centre, Patch.AngularRadiusDegrees, AtU, AtV);

		return At * FPlanetTerrain::SurfaceRadiusKilometres(Planet, Terrain, At);
	};

	const bool bFirst = (Across + Down) <= 1.0;

	const FVector A = bFirst ? Corner(U0, V0) : Corner(U1, V0);
	const FVector B = bFirst ? Corner(U1, V0) : Corner(U1, V1);
	const FVector C = Corner(U0, V1);

	OutRadiusKilometres = AlongToPlane(A, B, C, Unit);

	return OutRadiusKilometres > 0.0;
}

FVector FPlanetPatch::DirectionAt(
	const FVector& CentreDirection,
	const double AngularRadiusDegrees,
	const double U,
	const double V)
{
	const FVector Up = CentreDirection.GetSafeNormal();

	FVector Tangent;
	FVector Bitangent;
	BuildTangentFrame(Up, Tangent, Bitangent);

	// Gnomonic: step across a plane tangent at the centre, then project back onto the sphere.
	// Faithful near the middle and stretched toward the edges, which is why the angular radius is
	// capped well short of a hemisphere.
	const double Extent = FMath::Tan(FMath::DegreesToRadians(
		FMath::Clamp(AngularRadiusDegrees, 0.0001, 75.0)));

	return (Up + (Tangent * (U * Extent)) + (Bitangent * (V * Extent))).GetSafeNormal();
}

FPlanetPatchMesh FPlanetPatch::Build(
	const FPlanetConfig& Planet,
	const FPlanetTerrainConfig& Terrain,
	const FPlanetPatchConfig& Patch)
{
	FPlanetPatchMesh Result;

	const int32 Resolution = FMath::Clamp(Patch.Resolution, 2, 512);
	const FVector Centre = Patch.CentreDirection.GetSafeNormal();

	// The patch is anchored at the ground beneath its own centre, so its vertices are small
	// numbers regardless of where the planet is in the system.
	const double CentreRadius = FPlanetTerrain::SurfaceRadiusKilometres(Planet, Terrain, Centre);

	Result.Origin = FSystemCoordinate(Planet.Centre.Kilometres + (Centre * CentreRadius));

	Result.Positions.Reserve(Resolution * Resolution);
	Result.Normals.SetNumZeroed(Resolution * Resolution);
	Result.SurfaceUVs.Reserve(Resolution * Resolution);

	// Kept because steepness needs a normal, and the patch accumulates its normals from the faces
	// that share each vertex -- so they do not exist until every triangle does.
	TArray<FVector> Directions;
	TArray<double> Radii;

	Directions.Reserve(Resolution * Resolution);
	Radii.Reserve(Resolution * Resolution);

	// Where each row and column falls, once rather than per vertex. Even steps unless the patch is
	// graded, in which case they are fine at the centre and widen toward the rim (task 164).
	TArray<double> Coordinates;
	Coordinates.Reserve(Resolution);

	for (int32 Index = 0; Index < Resolution; ++Index)
	{
		Coordinates.Add(GridCoordinate(Patch, Index));
	}

	for (int32 Row = 0; Row < Resolution; ++Row)
	{
		for (int32 Column = 0; Column < Resolution; ++Column)
		{
			const double U = Coordinates[Column];
			const double V = Coordinates[Row];

			const FVector Direction = DirectionAt(Centre, Patch.AngularRadiusDegrees, U, V);
			const double Radius = FPlanetTerrain::SurfaceRadiusKilometres(Planet, Terrain, Direction);

			const FSystemCoordinate Point(Planet.Centre.Kilometres + (Direction * Radius));

			Result.Positions.Add(Point.ToLocalCentimetres(Result.Origin));

			// The same function the globe calls, from the same direction. The patch's rim is where
			// one mesh hands over to the other, and a parameterisation of its own would draw a line
			// of jumped texture right around it.
			Result.SurfaceUVs.Add(FPlanetTerrain::SurfaceUV(Direction));

			// Height now; steepness once the normals exist, which is after the triangles.
			Directions.Add(Direction);
			Radii.Add(Radius);
		}
	}

	Result.Triangles.Reserve((Resolution - 1) * (Resolution - 1) * 6);

	for (int32 Row = 0; Row < Resolution - 1; ++Row)
	{
		for (int32 Column = 0; Column < Resolution - 1; ++Column)
		{
			const int32 TopLeft = (Row * Resolution) + Column;
			const int32 TopRight = TopLeft + 1;
			const int32 BottomLeft = TopLeft + Resolution;
			const int32 BottomRight = BottomLeft + 1;

			// Wound so that cross(second - first, third - first) points away from the planet.
			//
			// The tangent frame is right-handed with the outward direction as its third axis
			// (tangent × bitangent = up), so a triangle must step along the tangent before the
			// bitangent to face outward. Taking them the other way round produces inward normals
			// and a patch lit from underneath — which is what the first version did.
			Result.Triangles.Add(TopLeft);
			Result.Triangles.Add(TopRight);
			Result.Triangles.Add(BottomLeft);

			Result.Triangles.Add(TopRight);
			Result.Triangles.Add(BottomRight);
			Result.Triangles.Add(BottomLeft);
		}
	}

	// Vertex normals accumulated from the faces that share them, rather than taken as the radial
	// direction. Radial normals are cheap and would light the patch as a smooth ball no matter
	// what the terrain does — the whole point is that hills catch the light.
	for (int32 Index = 0; Index + 2 < Result.Triangles.Num(); Index += 3)
	{
		const int32 A = Result.Triangles[Index];
		const int32 B = Result.Triangles[Index + 1];
		const int32 C = Result.Triangles[Index + 2];

		const FVector FaceNormal = FVector::CrossProduct(
			Result.Positions[B] - Result.Positions[A],
			Result.Positions[C] - Result.Positions[A]);

		Result.Normals[A] += FaceNormal;
		Result.Normals[B] += FaceNormal;
		Result.Normals[C] += FaceNormal;
	}

	for (int32 Index = 0; Index < Result.Normals.Num(); ++Index)
	{
		// A degenerate accumulation falls back to radial, which is always outward and never zero.
		Result.Normals[Index] = Result.Normals[Index].IsNearlyZero()
			? (Result.Positions[Index] + (Centre * 1000.0)).GetSafeNormal()
			: Result.Normals[Index].GetSafeNormal();
	}

	// Steepness last, now that every normal is final.
	Result.GroundKinds.Reserve(Result.Normals.Num());

	for (int32 Index = 0; Index < Result.Normals.Num(); ++Index)
	{
		const double Rise = Radii[Index] - Planet.RadiusKilometres;

		const double Height = Terrain.MaxElevationKilometres > 0.0
			? FMath::Clamp(Rise / Terrain.MaxElevationKilometres, 0.0, 1.0)
			: 0.0;

		// The sine of the slope angle; see the note in the globe builder for why not 1 - cos.
		const double Level = FMath::Clamp(
			FVector::DotProduct(Result.Normals[Index], Directions[Index]), -1.0, 1.0);

		const double Steepness = FMath::Sqrt(FMath::Max(0.0, 1.0 - (Level * Level)));

		Result.GroundKinds.Add(FVector2D(Height, Steepness));
	}

	return Result;
}
