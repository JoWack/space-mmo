#include "Misc/AutomationTest.h"
#include "SpaceMMOPlanetTerrain.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FPlanetTerrainConfig TerrainTestConfig()
	{
		FPlanetTerrainConfig Terrain;
		Terrain.Seed = 20260801;
		Terrain.MaxElevationKilometres = 0.5;
		Terrain.Octaves = 5;

		return Terrain;
	}

	FPlanetConfig TerrainTestPlanet()
	{
		FPlanetConfig Planet;
		Planet.Centre = FSystemCoordinate(FVector(200.0, 0.0, 0.0));
		Planet.RadiusKilometres = 20.0;

		return Planet;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOTerrainIsDeterministicTest,
	"SpaceMMO.Terrain.IsDeterministic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOTerrainIsDeterministicTest::RunTest(const FString& Parameters)
{
	const FPlanetTerrainConfig Terrain = TerrainTestConfig();
	const FVector Direction = FVector(0.3, -0.7, 0.5).GetSafeNormal();

	// The whole authority model rests on this: the server decides where the ground is and the
	// client draws it, and they are only ever the same surface if this function is a function.
	const double First = FPlanetTerrain::ElevationKilometres(Terrain, Direction);
	const double Second = FPlanetTerrain::ElevationKilometres(Terrain, Direction);

	TestEqual(TEXT("Same input, same height"), First, Second);

	// An unnormalised direction is the same query — callers pass raw offsets from a planet centre.
	const double Scaled = FPlanetTerrain::ElevationKilometres(Terrain, Direction * 12345.0);

	TestTrue(TEXT("Magnitude does not affect height"), FMath::IsNearlyEqual(First, Scaled, 1e-9));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOTerrainSeedChangesTheWorldTest,
	"SpaceMMO.Terrain.SeedChangesTheWorld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOTerrainSeedChangesTheWorldTest::RunTest(const FString& Parameters)
{
	FPlanetTerrainConfig A = TerrainTestConfig();
	FPlanetTerrainConfig B = TerrainTestConfig();
	B.Seed = A.Seed + 1;

	// Sampled across many directions rather than one, because a single coincidental match proves
	// nothing and a seed that does nothing would still pass a one-point test surprisingly often.
	int32 Differences = 0;

	for (int32 Index = 0; Index < 64; ++Index)
	{
		const double Angle = Index * 0.31;
		const FVector Direction =
			FVector(FMath::Cos(Angle), FMath::Sin(Angle), FMath::Cos(Angle * 0.7)).GetSafeNormal();

		if (!FMath::IsNearlyEqual(
			FPlanetTerrain::ElevationKilometres(A, Direction),
			FPlanetTerrain::ElevationKilometres(B, Direction),
			1e-6))
		{
			++Differences;
		}
	}

	TestTrue(TEXT("A different seed is a different planet"), Differences > 60);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOTerrainStaysInRangeTest,
	"SpaceMMO.Terrain.StaysInRange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOTerrainStaysInRangeTest::RunTest(const FString& Parameters)
{
	const FPlanetTerrainConfig Terrain = TerrainTestConfig();

	double Lowest = TNumericLimits<double>::Max();
	double Highest = TNumericLimits<double>::Lowest();

	for (int32 Index = 0; Index < 2000; ++Index)
	{
		const double U = Index * 0.0137;
		const FVector Direction = FVector(
			FMath::Cos(U * 3.1), FMath::Sin(U * 1.7), FMath::Sin(U * 2.3)).GetSafeNormal();

		const double Elevation = FPlanetTerrain::ElevationKilometres(Terrain, Direction);

		Lowest = FMath::Min(Lowest, Elevation);
		Highest = FMath::Max(Highest, Elevation);
	}

	// Never negative: the nominal radius is the sea floor, so terrain cannot dip inside the radius
	// gravity is defined against.
	TestTrue(TEXT("Never below the nominal radius"), Lowest >= 0.0);
	TestTrue(TEXT("Never above the ceiling"), Highest <= Terrain.MaxElevationKilometres + 1e-9);

	// A range this narrow would mean the noise is not actually varying — a flat planet passes the
	// two bounds above perfectly well.
	TestTrue(TEXT("Terrain actually varies"), Highest - Lowest > Terrain.MaxElevationKilometres * 0.2);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOTerrainHasNoCubeSeamsTest,
	"SpaceMMO.Terrain.HasNoCubeSeams",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOTerrainHasNoCubeSeamsTest::RunTest(const FString& Parameters)
{
	const FPlanetTerrainConfig Terrain = TerrainTestConfig();

	// The classic cube-sphere failure. Sampling noise per cube face makes the six faces disagree
	// along every shared edge, and worst of all at the eight corners where three faces meet.
	// Sampling by direction should make those places completely unremarkable.
	//
	// Straddle each of the twelve cube edges and all eight corners, and check that stepping across
	// where the boundary falls changes the height no more than stepping the same distance
	// anywhere else.
	const TArray<FVector> Boundaries = {
		FVector(1, 1, 0), FVector(1, -1, 0), FVector(-1, 1, 0), FVector(-1, -1, 0),
		FVector(1, 0, 1), FVector(1, 0, -1), FVector(-1, 0, 1), FVector(-1, 0, -1),
		FVector(0, 1, 1), FVector(0, 1, -1), FVector(0, -1, 1), FVector(0, -1, -1),
		FVector(1, 1, 1), FVector(1, 1, -1), FVector(1, -1, 1), FVector(1, -1, -1),
		FVector(-1, 1, 1), FVector(-1, 1, -1), FVector(-1, -1, 1), FVector(-1, -1, -1),
	};

	const double Step = 1e-4;

	double WorstBoundaryJump = 0.0;

	for (const FVector& Boundary : Boundaries)
	{
		const FVector Centre = Boundary.GetSafeNormal();

		// Nudge to either side along an axis that actually crosses the boundary.
		const FVector Nudge = FVector(Boundary.Y, Boundary.Z, Boundary.X).GetSafeNormal() * Step;

		const double Before = FPlanetTerrain::ElevationKilometres(Terrain, Centre - Nudge);
		const double After = FPlanetTerrain::ElevationKilometres(Terrain, Centre + Nudge);

		WorstBoundaryJump = FMath::Max(WorstBoundaryJump, FMath::Abs(After - Before));
	}

	// The control: the same size of step taken well away from any cube boundary.
	double WorstInteriorJump = 0.0;

	for (int32 Index = 0; Index < 200; ++Index)
	{
		const double U = Index * 0.017;
		const FVector Centre =
			FVector(FMath::Cos(U * 2.1) + 0.31, FMath::Sin(U * 1.3) + 0.17, FMath::Sin(U)).GetSafeNormal();

		const FVector Nudge =
			FVector(-Centre.Y, Centre.X, 0.0).GetSafeNormal() * Step;

		const double Before = FPlanetTerrain::ElevationKilometres(Terrain, Centre - Nudge);
		const double After = FPlanetTerrain::ElevationKilometres(Terrain, Centre + Nudge);

		WorstInteriorJump = FMath::Max(WorstInteriorJump, FMath::Abs(After - Before));
	}

	// Not "small", but "no worse than anywhere else". A cube boundary should be undetectable, and
	// a threshold in absolute metres would pass a design that merely hides the seam well.
	TestTrue(
		TEXT("Cube boundaries are no rougher than open ground"),
		WorstBoundaryJump <= FMath::Max(WorstInteriorJump * 2.0, 1e-9));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOTerrainIsContinuousTest,
	"SpaceMMO.Terrain.IsContinuous",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOTerrainIsContinuousTest::RunTest(const FString& Parameters)
{
	const FPlanetTerrainConfig Terrain = TerrainTestConfig();

	// Walk a great circle in small steps. A hashing mistake shows up here as a cliff — terrain
	// that teleports between adjacent samples — which is invisible in a single-point test.
	const int32 Steps = 4000;

	double Previous = FPlanetTerrain::ElevationKilometres(Terrain, FVector(1, 0, 0));
	double LargestStep = 0.0;

	for (int32 Index = 1; Index <= Steps; ++Index)
	{
		const double Angle = (Index * 2.0 * PI) / Steps;

		const double Current = FPlanetTerrain::ElevationKilometres(
			Terrain, FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0));

		LargestStep = FMath::Max(LargestStep, FMath::Abs(Current - Previous));
		Previous = Current;
	}

	// One step is a thousandth of the way round the planet, so a change of even a few percent of
	// full elevation would be a wall.
	TestTrue(
		TEXT("No cliffs between adjacent samples"),
		LargestStep < Terrain.MaxElevationKilometres * 0.05);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOCubeToSphereTest,
	"SpaceMMO.Terrain.CubeToSphere",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOCubeToSphereTest::RunTest(const FString& Parameters)
{
	// Face centres are already on the sphere and must not move.
	TestTrue(
		TEXT("Face centre is fixed"),
		FPlanetTerrain::CubeToSphere(FVector(1, 0, 0)).Equals(FVector(1, 0, 0), 1e-9));

	// Every point on the cube's surface must land exactly on the unit sphere — corners included,
	// which is where a naive mapping is furthest out.
	double WorstError = 0.0;

	for (int32 I = 0; I <= 8; ++I)
	{
		for (int32 J = 0; J <= 8; ++J)
		{
			const double U = -1.0 + (I * 0.25);
			const double V = -1.0 + (J * 0.25);

			for (const FVector& Face : {
				FVector(1.0, U, V), FVector(-1.0, U, V),
				FVector(U, 1.0, V), FVector(U, -1.0, V),
				FVector(U, V, 1.0), FVector(U, V, -1.0) })
			{
				WorstError = FMath::Max(
					WorstError, FMath::Abs(FPlanetTerrain::CubeToSphere(Face).Size() - 1.0));
			}
		}
	}

	TestTrue(TEXT("Every mapped point is on the unit sphere"), WorstError < 1e-9);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOTerrainAltitudeTest,
	"SpaceMMO.Terrain.AltitudeAboveGround",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOTerrainAltitudeTest::RunTest(const FString& Parameters)
{
	const FPlanetConfig Planet = TerrainTestPlanet();
	const FPlanetTerrainConfig Terrain = TerrainTestConfig();

	const FVector Direction = FVector(0.2, 0.9, -0.3).GetSafeNormal();

	const double SurfaceRadius =
		FPlanetTerrain::SurfaceRadiusKilometres(Planet, Terrain, Direction);

	// Standing exactly on the ground is zero altitude, whatever the terrain does there.
	const FSystemCoordinate OnGround(Planet.Centre.Kilometres + (Direction * SurfaceRadius));

	TestTrue(
		TEXT("On the ground reads as zero"),
		FMath::Abs(FPlanetTerrain::AltitudeAboveGroundKilometres(Planet, Terrain, OnGround)) < 1e-9);

	// Ten kilometres straight up is ten kilometres up.
	const FSystemCoordinate Aloft(Planet.Centre.Kilometres + (Direction * (SurfaceRadius + 10.0)));

	TestTrue(
		TEXT("Ten km up reads as ten km"),
		FMath::IsNearlyEqual(
			FPlanetTerrain::AltitudeAboveGroundKilometres(Planet, Terrain, Aloft), 10.0, 1e-9));

	// Below the surface is negative, which is what a server uses to reject a position.
	const FSystemCoordinate Buried(Planet.Centre.Kilometres + (Direction * (SurfaceRadius - 1.0)));

	TestTrue(
		TEXT("Underground is negative"),
		FPlanetTerrain::AltitudeAboveGroundKilometres(Planet, Terrain, Buried) < 0.0);

	// The ground is always at or above the nominal radius, never inside it.
	TestTrue(TEXT("Surface never sinks below the radius"), SurfaceRadius >= Planet.RadiusKilometres);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOTerrainDegenerateInputTest,
	"SpaceMMO.Terrain.DegenerateInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOTerrainDegenerateInputTest::RunTest(const FString& Parameters)
{
	const FPlanetTerrainConfig Terrain = TerrainTestConfig();

	// A direction of zero has no "up" to have terrain along. It must not produce a NaN, because a
	// NaN here propagates into a position and then into everything that touches it.
	const double AtCentre = FPlanetTerrain::ElevationKilometres(Terrain, FVector::ZeroVector);

	TestFalse(TEXT("Centre is not NaN"), FMath::IsNaN(AtCentre));
	TestEqual(TEXT("Centre is the sea floor"), AtCentre, 0.0);

	// A single octave is legal and must not divide by zero when normalising amplitude.
	FPlanetTerrainConfig Single = Terrain;
	Single.Octaves = 1;

	const double OneOctave = FPlanetTerrain::ElevationKilometres(Single, FVector(1, 0, 0));

	TestFalse(TEXT("One octave is not NaN"), FMath::IsNaN(OneOctave));

	// Zero gain means only the first octave contributes; it must still be finite and in range.
	FPlanetTerrainConfig NoGain = Terrain;
	NoGain.Gain = 0.0;

	const double Flat = FPlanetTerrain::ElevationKilometres(NoGain, FVector(1, 0, 0));

	TestTrue(TEXT("Zero gain stays in range"), Flat >= 0.0 && Flat <= NoGain.MaxElevationKilometres);

	return true;
}

#endif
