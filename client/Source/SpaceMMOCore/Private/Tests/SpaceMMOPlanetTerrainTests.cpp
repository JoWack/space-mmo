#include "Misc/AutomationTest.h"
#include "SpaceMMOPlanetPatch.h"
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

// ── Ground contact ───────────────────────────────────────────────────────────
//
// This, not the mesh, is what collision means here: the server has no triangles to collide
// against and must still agree about where a player may stand.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOContactLeavesFlightAloneTest,
	"SpaceMMO.Contact.LeavesFlightAlone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOContactLeavesFlightAloneTest::RunTest(const FString& Parameters)
{
	const FPlanetConfig Planet = TerrainTestPlanet();
	const FPlanetTerrainConfig Terrain = TerrainTestConfig();

	const FVector Direction = FVector(0.3, 0.5, 0.8).GetSafeNormal();
	const FSystemCoordinate Aloft(Planet.Centre.Kilometres + (Direction * 30.0));
	const FVector Velocity(1000.0, -2000.0, 500.0);

	const FGroundContact Contact =
		FPlanetTerrain::ResolveContact(Planet, Terrain, Aloft, Velocity, 0.02);

	TestFalse(TEXT("Ten km up is not on the ground"), Contact.bOnGround);
	TestTrue(TEXT("Position untouched"), Contact.Position.Kilometres.Equals(Aloft.Kilometres, 0.0));
	TestTrue(TEXT("Velocity untouched"), Contact.Velocity.Equals(Velocity, 0.0));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOContactStopsSinkingTest,
	"SpaceMMO.Contact.StopsSinking",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOContactStopsSinkingTest::RunTest(const FString& Parameters)
{
	const FPlanetConfig Planet = TerrainTestPlanet();
	const FPlanetTerrainConfig Terrain = TerrainTestConfig();

	const FVector Direction = FVector(0.1, -0.4, 0.9).GetSafeNormal();

	// Well underground, falling fast — the state a ship reaches if nothing stops it.
	const double GroundRadius =
		FPlanetTerrain::SurfaceRadiusKilometres(Planet, Terrain, Direction);

	const FSystemCoordinate Buried(Planet.Centre.Kilometres + (Direction * (GroundRadius - 2.0)));
	const FVector Falling = Direction * -50000.0;

	const FGroundContact Contact =
		FPlanetTerrain::ResolveContact(Planet, Terrain, Buried, Falling, 0.02);

	TestTrue(TEXT("Detected as ground contact"), Contact.bOnGround);

	// Lifted to exactly the contact height, not merely somewhere above the ground.
	const double Resolved = (Contact.Position.Kilometres - Planet.Centre.Kilometres).Size();

	TestTrue(
		TEXT("Placed exactly on the surface plus its own radius"),
		FMath::IsNearlyEqual(Resolved, GroundRadius + 0.02, 1e-9));

	// No longer heading downward.
	TestTrue(
		TEXT("Inward motion removed"),
		FVector::DotProduct(Contact.Velocity, Contact.SurfaceNormal) >= -1e-6);

	TestTrue(TEXT("Impact speed reported"), Contact.ImpactSpeed > 0.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOContactRestsWithoutDriftTest,
	"SpaceMMO.Contact.RestsWithoutDrift",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOContactRestsWithoutDriftTest::RunTest(const FString& Parameters)
{
	const FPlanetConfig Planet = TerrainTestPlanet();
	const FPlanetTerrainConfig Terrain = TerrainTestConfig();

	const FVector Direction = FVector(0.6, 0.2, 0.4).GetSafeNormal();
	const double GroundRadius =
		FPlanetTerrain::SurfaceRadiusKilometres(Planet, Terrain, Direction);

	FSystemCoordinate Position(Planet.Centre.Kilometres + (Direction * (GroundRadius + 0.02)));
	FVector Velocity = FVector::ZeroVector;

	// Resolved repeatedly, as it would be every frame while parked. A resting object must not
	// creep upward from a bias or sink from a rounding error — either is visible within seconds.
	const double StartRadius = (Position.Kilometres - Planet.Centre.Kilometres).Size();

	for (int32 Frame = 0; Frame < 600; ++Frame)
	{
		const FGroundContact Contact =
			FPlanetTerrain::ResolveContact(Planet, Terrain, Position, Velocity, 0.02);

		Position = Contact.Position;
		Velocity = Contact.Velocity;
	}

	const double EndRadius = (Position.Kilometres - Planet.Centre.Kilometres).Size();

	TestTrue(
		TEXT("Ten seconds of resting does not drift"),
		FMath::Abs(EndRadius - StartRadius) < 1e-9);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOContactAllowsSlidingTest,
	"SpaceMMO.Contact.AllowsSliding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOContactAllowsSlidingTest::RunTest(const FString& Parameters)
{
	const FPlanetConfig Planet = TerrainTestPlanet();
	const FPlanetTerrainConfig Terrain = TerrainTestConfig();

	const FVector Direction = FVector(0.0, 0.0, 1.0);
	const double GroundRadius =
		FPlanetTerrain::SurfaceRadiusKilometres(Planet, Terrain, Direction);

	const FSystemCoordinate Buried(Planet.Centre.Kilometres + (Direction * (GroundRadius - 0.5)));

	// Moving sideways and downward at once. Only the downward part should be taken away — freezing
	// everything on contact would make a ship stick to the ground the instant it touched.
	const FVector Sideways = FVector(30000.0, 0.0, 0.0);
	const FVector Velocity = Sideways + (Direction * -20000.0);

	const FGroundContact Contact =
		FPlanetTerrain::ResolveContact(Planet, Terrain, Buried, Velocity, 0.0);

	TestTrue(TEXT("On the ground"), Contact.bOnGround);

	const FVector Tangential =
		Contact.Velocity - (Contact.SurfaceNormal * FVector::DotProduct(Contact.Velocity, Contact.SurfaceNormal));

	TestTrue(
		TEXT("Sideways motion survives"),
		Tangential.Size() > Sideways.Size() * 0.9);

	// And it settles rather than bouncing: no outward velocity was invented.
	TestTrue(
		TEXT("Does not bounce"),
		FVector::DotProduct(Contact.Velocity, Contact.SurfaceNormal) < 1.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOSurfaceNormalTest,
	"SpaceMMO.Contact.SurfaceNormal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOSurfaceNormalTest::RunTest(const FString& Parameters)
{
	const FPlanetConfig Planet = TerrainTestPlanet();

	FPlanetTerrainConfig Flat = TerrainTestConfig();
	Flat.MaxElevationKilometres = 0.0;

	// With no relief at all the ground is a sphere, so its normal is exactly radial. Anything else
	// means the finite differences are wrong.
	const FVector Direction = FVector(0.3, -0.6, 0.7).GetSafeNormal();

	const FVector FlatNormal = FPlanetTerrain::SurfaceNormal(Planet, Flat, Direction);

	TestTrue(TEXT("Flat ground is radial"), FlatNormal.Equals(Direction, 1e-6));

	// With relief, normals tilt away from radial but must stay outward and unit length, or a ship
	// landing on a slope is pushed through the hill instead of onto it.
	const FPlanetTerrainConfig Rough = TerrainTestConfig();

	double WorstDot = 1.0;
	double WorstLengthError = 0.0;
	bool bAnyTilt = false;

	for (int32 Index = 0; Index < 200; ++Index)
	{
		const double U = Index * 0.037;
		const FVector Sample =
			FVector(FMath::Cos(U * 2.3), FMath::Sin(U * 1.7), FMath::Cos(U)).GetSafeNormal();

		const FVector Normal = FPlanetTerrain::SurfaceNormal(Planet, Rough, Sample);
		const double Dot = FVector::DotProduct(Normal, Sample);

		WorstDot = FMath::Min(WorstDot, Dot);
		WorstLengthError = FMath::Max(WorstLengthError, FMath::Abs(Normal.Size() - 1.0));

		if (Dot < 0.9999)
		{
			bAnyTilt = true;
		}
	}

	TestTrue(TEXT("Never points into the planet"), WorstDot > 0.0);
	TestTrue(TEXT("Always unit length"), WorstLengthError < 1e-6);
	TestTrue(TEXT("Slopes actually tilt the normal"), bAnyTilt);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOContactDoesNotFlickerWhileWalkingTest,
	"SpaceMMO.Contact.DoesNotFlickerWhileWalking",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOContactDoesNotFlickerWhileWalkingTest::RunTest(const FString& Parameters)
{
	const FPlanetConfig Planet = TerrainTestPlanet();

	FPlanetTerrainConfig Terrain = TerrainTestConfig();
	Terrain.MaxElevationKilometres = 0.0;

	const double Radius = 0.001;

	FSystemCoordinate Position(
		Planet.Centre.Kilometres + FVector(0.0, 0.0, Planet.RadiusKilometres + Radius));

	// Walking pace, tangential. Every such step on a curved surface ends fractionally above the
	// ground, which is what made grounded and airborne alternate every single frame.
	const FVector Along(600.0, 0.0, 0.0);

	int32 AirborneFrames = 0;

	for (int32 Frame = 0; Frame < 600; ++Frame)
	{
		const FGroundContact Contact =
			FPlanetTerrain::ResolveContact(Planet, Terrain, Position, Along, Radius);

		if (!Contact.bOnGround)
		{
			++AirborneFrames;
		}

		Position = FSystemCoordinate(
			Contact.Position.Kilometres + (Along / SpaceMMO::Coordinates::CentimetresPerKilometre) * (1.0 / 60.0));
	}

	TestEqual(TEXT("Never leaves the ground while walking"), AirborneFrames, 0);

	// A jump must still work: rising fast, the tolerance band must not pin the character down.
	const FGroundContact Rising = FPlanetTerrain::ResolveContact(
		Planet, Terrain, Position, FVector(0.0, 0.0, 420.0), Radius);

	TestFalse(TEXT("Jumping still leaves the ground"), Rising.bOnGround);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOSurfacePositionSitsOnTheGroundTest,
	"SpaceMMO.Terrain.SurfacePositionSitsOnTheGround",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOSurfacePositionSitsOnTheGroundTest::RunTest(const FString& Parameters)
{
	const FPlanetTerrainConfig Terrain = TerrainTestConfig();
	const FPlanetConfig Planet = TerrainTestPlanet();

	// The round trip that matters. Content places a deposit by direction; this turns it into a
	// point; the server then measures that point's altitude to decide whether a player standing
	// near it is close enough to gather. If the two disagree by even a little, deposits sit
	// visibly on the ground and refuse to be gathered — or hang above it and work fine.
	const TArray<FVector> Directions = {
		FVector(1.0, 0.0, 0.0),
		FVector(-1.0, 0.02, 0.0),          // the authored deposit on the Capital
		FVector(-1.0, -0.015, 0.025),      // and the second one
		FVector(0.0, 0.0, 1.0),            // a pole, which a lat-long scheme would struggle with
		FVector(0.577, 0.577, 0.577),      // a cube corner, where three faces meet
	};

	for (const FVector& Direction : Directions)
	{
		const FSystemCoordinate Position =
			FPlanetTerrain::SurfacePosition(Planet, Terrain, Direction);

		const double Altitude =
			FPlanetTerrain::AltitudeAboveGroundKilometres(Planet, Terrain, Position);

		// A millimetre. Anything looser would hide a real disagreement between the two functions.
		TestTrue(
			FString::Printf(
				TEXT("Direction %s lands on the ground (altitude %f km)"),
				*Direction.ToString(),
				Altitude),
			FMath::Abs(Altitude) < 0.000001);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOSurfacePositionIgnoresDirectionLengthTest,
	"SpaceMMO.Terrain.SurfacePositionIgnoresDirectionLength",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOSurfacePositionIgnoresDirectionLengthTest::RunTest(const FString& Parameters)
{
	const FPlanetTerrainConfig Terrain = TerrainTestConfig();
	const FPlanetConfig Planet = TerrainTestPlanet();

	const FVector Direction = FVector(-1.0, 0.02, 0.0);

	// The API normalises on load and there is a check constraint behind it, but this is also
	// called with raw offsets from a player's position, whose length is a distance in kilometres.
	// Trusting that length would scale the surface radius by it and put the answer in orbit.
	const FSystemCoordinate FromUnit =
		FPlanetTerrain::SurfacePosition(Planet, Terrain, Direction.GetSafeNormal());

	const FSystemCoordinate FromLong =
		FPlanetTerrain::SurfacePosition(Planet, Terrain, Direction * 4000.0);

	const FSystemCoordinate FromShort =
		FPlanetTerrain::SurfacePosition(Planet, Terrain, Direction * 0.0001);

	TestTrue(
		TEXT("A long direction gives the same point"),
		FromUnit.Kilometres.Equals(FromLong.Kilometres, 0.000001));

	TestTrue(
		TEXT("A short direction gives the same point"),
		FromUnit.Kilometres.Equals(FromShort.Kilometres, 0.000001));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOSurfacePositionIsRelativeToTheCentreTest,
	"SpaceMMO.Terrain.SurfacePositionIsRelativeToTheCentre",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOSurfacePositionIsRelativeToTheCentreTest::RunTest(const FString& Parameters)
{
	const FPlanetTerrainConfig Terrain = TerrainTestConfig();

	FPlanetConfig Planet = TerrainTestPlanet();
	const FVector Direction = FVector(0.4, 0.6, -0.7).GetSafeNormal();

	const FSystemCoordinate Before = FPlanetTerrain::SurfacePosition(Planet, Terrain, Direction);

	// Move the planet. A deposit is authored relative to its body, so it must travel with it —
	// the same reason terrain is sampled by direction rather than by an absolute position.
	const FVector Shift = FVector(1000.0, -250.0, 30.0);
	Planet.Centre = FSystemCoordinate(Planet.Centre.Kilometres + Shift);

	const FSystemCoordinate After = FPlanetTerrain::SurfacePosition(Planet, Terrain, Direction);

	TestTrue(
		TEXT("The surface point moves with the planet, and by exactly as much"),
		After.Kilometres.Equals(Before.Kilometres + Shift, 0.000001));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOContactHoldsAtCruiseSpeedTest,
	"SpaceMMO.Terrain.ContactHoldsAtCruiseSpeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOContactHoldsAtCruiseSpeedTest::RunTest(const FString& Parameters)
{
	// A ship skimming the surface reported Touched down and Lifted off four times in 0.36 s while
	// travelling about 600 m (task 90). The question is whether that is the contact rule being
	// wrong or the ground genuinely rising and falling under a fast ship, and it is decidable from
	// the height function alone: at 738 m/s and 60 Hz the ship crosses about 12 m of ground per
	// frame, and the contact tolerance is 20 cm.
	const FPlanetConfig Planet = TerrainTestPlanet();
	const FPlanetTerrainConfig Terrain = TerrainTestConfig();

	constexpr double SpeedMetresPerSecond = 738.0;
	constexpr double CruiseFrameSeconds = 1.0 / 60.0;
	constexpr double ToleranceMetres = 0.2;

	const FVector Start = FVector(0.3, 0.2, -0.9).GetSafeNormal();

	FVector Tangent;
	FVector Bitangent;
	FPlanetPatch::BuildTangentFrame(Start, Tangent, Bitangent);

	const double StepKilometres = (SpeedMetresPerSecond * CruiseFrameSeconds) / 1000.0;

	double Previous = FPlanetTerrain::SurfaceRadiusKilometres(Planet, Terrain, Start);
	double WorstStepMetres = 0.0;
	int32 StepsExceedingTolerance = 0;

	constexpr int32 Steps = 60;

	for (int32 Step = 1; Step <= Steps; ++Step)
	{
		// Walking the direction around the sphere, which is what a ship holding a heading does.
		const FVector Direction =
			(Start + (Tangent * ((StepKilometres * Step) / Planet.RadiusKilometres))).GetSafeNormal();

		const double Radius = FPlanetTerrain::SurfaceRadiusKilometres(Planet, Terrain, Direction);

		const double StepMetres = FMath::Abs(Radius - Previous) * 1000.0;

		WorstStepMetres = FMath::Max(WorstStepMetres, StepMetres);

		if (StepMetres > ToleranceMetres)
		{
			++StepsExceedingTolerance;
		}

		Previous = Radius;
	}

	AddInfo(FString::Printf(
		TEXT("At %.0f m/s: worst per-frame ground change %.2f m, %d of %d frames exceed the %.2f m "
			"contact tolerance."),
		SpeedMetresPerSecond,
		WorstStepMetres,
		StepsExceedingTolerance,
		Steps,
		ToleranceMetres));

	// The claim being pinned is not a number but a consequence: at cruise speed the ground moves
	// further between frames than the band that decides contact, so a rule with one threshold must
	// oscillate. Any fix has to widen the band it releases on, not the one it captures on.
	TestTrue(
		TEXT("Ground moves further per frame than the contact tolerance at cruise speed"),
		WorstStepMetres > ToleranceMetres);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOContactIsWiderToLeaveThanToArriveTest,
	"SpaceMMO.Terrain.ContactIsWiderToLeaveThanToArrive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOContactIsWiderToLeaveThanToArriveTest::RunTest(const FString& Parameters)
{
	const FPlanetConfig Planet = TerrainTestPlanet();
	const FPlanetTerrainConfig Terrain = TerrainTestConfig();

	const FVector Up = FVector(0.3, 0.2, -0.9).GetSafeNormal();

	const double GroundRadius = FPlanetTerrain::SurfaceRadiusKilometres(Planet, Terrain, Up);

	// Half a metre up: outside the twenty-centimetre capture band, inside the two-metre release
	// band, and larger than the 0.33 m the ground moves in a frame at cruise speed. This is exactly
	// the height at which the state used to flap.
	const double GapKilometres = 0.0005;

	const FSystemCoordinate Position(
		Planet.Centre.Kilometres + (Up * (GroundRadius + GapKilometres)));

	// Drifting along the surface rather than climbing, so separation speed cannot be what decides
	// it. Anything genuinely leaving is caught by that instead, and must still be able to leave.
	FVector Tangent;
	FVector Bitangent;
	FPlanetPatch::BuildTangentFrame(Up, Tangent, Bitangent);

	const FVector Velocity = Tangent * 73800.0;

	const FGroundContact Arriving = FPlanetTerrain::ResolveContact(
		Planet, Terrain, Position, Velocity,
		0.0, FPlanetTerrain::DefaultContactToleranceKilometres, false);

	const FGroundContact Staying = FPlanetTerrain::ResolveContact(
		Planet, Terrain, Position, Velocity,
		0.0, FPlanetTerrain::DefaultContactToleranceKilometres, true);

	TestFalse(TEXT("Half a metre up does not count as landing"), Arriving.bOnGround);
	TestTrue(TEXT("Half a metre up does not count as taking off"), Staying.bOnGround);

	// The release band must not swallow a real departure, or a ship could never leave slowly and a
	// jump would read as a stumble.
	const FGroundContact Climbing = FPlanetTerrain::ResolveContact(
		Planet, Terrain, Position, Up * 100.0,
		0.0, FPlanetTerrain::DefaultContactToleranceKilometres, true);

	TestFalse(TEXT("Climbing away still leaves the ground"), Climbing.bOnGround);

	return true;
}

namespace
{
	/** A direction `Metres` along the surface from Centre, heading off along Tangent. */
	FVector AlongSurface(const FVector& Centre, const FVector& Tangent, const double Metres)
	{
		const double Angle = Metres / 1000.0 / TerrainTestPlanet().RadiusKilometres;

		return (Centre * FMath::Cos(Angle) + Tangent * FMath::Sin(Angle)).GetSafeNormal();
	}

	/** The test terrain with one pad on it, 420 m flat and 150 m of blend, as Borlash's. */
	FPlanetTerrainConfig PaddedTestConfig(const FVector& Centre, const double ElevationKilometres)
	{
		FPlanetTerrainConfig Terrain = TerrainTestConfig();
		Terrain.Pads.Add(FPlanetTerrain::MakePad(
			TEXT("station_test"), Centre, 0.42, 0.15, ElevationKilometres,
			TerrainTestPlanet().RadiusKilometres));

		return Terrain;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOTerrainPadIsLevelInsideTest,
	"SpaceMMO.Terrain.PadIsLevelInside",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOTerrainPadIsLevelInsideTest::RunTest(const FString& Parameters)
{
	// Task 168. A city is built flat, so inside its pad the ground has to be exactly one height --
	// not nearly, because the paving sits 30 cm above it and anything that rose through it would show.
	const FVector Centre = FVector(0.026, 0.0, 1.0).GetSafeNormal();
	const FVector East = FVector::CrossProduct(FVector::YAxisVector, Centre).GetSafeNormal();
	const FVector North = FVector::CrossProduct(Centre, East).GetSafeNormal();

	const FPlanetTerrainConfig Terrain = PaddedTestConfig(Centre, 0.172);

	int32 Sampled = 0;

	for (const FVector& Tangent : {East, North, -East, (East + North).GetSafeNormal()})
	{
		for (double Metres = 0.0; Metres <= 419.0; Metres += 19.0)
		{
			const double Height =
				FPlanetTerrain::ElevationKilometres(Terrain, AlongSurface(Centre, Tangent, Metres));

			TestTrue(
				*FString::Printf(TEXT("Level at %.0f m (%.6f km)"), Metres, Height),
				FMath::IsNearlyEqual(Height, 0.172, 1e-12));

			++Sampled;
		}
	}

	TestTrue(TEXT("Sampled the flat ground at all"), Sampled > 40);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOTerrainPadLeavesTheLandAloneTest,
	"SpaceMMO.Terrain.PadLeavesTheLandAlone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOTerrainPadLeavesTheLandAloneTest::RunTest(const FString& Parameters)
{
	// Past the blend ring the planet is exactly what it was. A pad is local by construction, and a
	// pad that leaked would move every deposit and station on the body a little, with nothing at
	// any of them saying why.
	const FVector Centre = FVector(0.026, 0.0, 1.0).GetSafeNormal();
	const FVector East = FVector::CrossProduct(FVector::YAxisVector, Centre).GetSafeNormal();

	const FPlanetTerrainConfig Padded = PaddedTestConfig(Centre, 0.172);
	const FPlanetTerrainConfig Plain = TerrainTestConfig();

	for (double Metres = 571.0; Metres <= 3000.0; Metres += 97.0)
	{
		const FVector Direction = AlongSurface(Centre, East, Metres);

		TestEqual(
			*FString::Printf(TEXT("Untouched at %.0f m"), Metres),
			FPlanetTerrain::ElevationKilometres(Padded, Direction),
			FPlanetTerrain::ElevationKilometres(Plain, Direction));
	}

	// And the far side of the planet, which a pad wider than a hemisphere would otherwise reach.
	TestEqual(
		TEXT("Untouched opposite"),
		FPlanetTerrain::ElevationKilometres(Padded, -Centre),
		FPlanetTerrain::ElevationKilometres(Plain, -Centre));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOTerrainPadBlendsWithoutACliffTest,
	"SpaceMMO.Terrain.PadBlendsWithoutACliff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOTerrainPadBlendsWithoutACliffTest::RunTest(const FString& Parameters)
{
	// Across the ring the ground has to go from the pad to the land continuously. A step anywhere
	// in it is a ledge -- and the character has no step-up, so a ledge is a wall.
	//
	// A pad far below the land, so the blend has real height to cover: 0.0 against land that is
	// mostly a few hundred metres up. One metre apart, no two samples may differ by more than the
	// steepest the blend can be (1.5 times its average) plus whatever the land does on its own.
	const FVector Centre = FVector(-0.4, 0.8, 0.45).GetSafeNormal();
	const FVector Tangent = FVector::CrossProduct(Centre, FVector::ZAxisVector).GetSafeNormal();

	const FPlanetTerrainConfig Terrain = PaddedTestConfig(Centre, 0.0);

	double Worst = 0.0;
	double Previous = FPlanetTerrain::ElevationKilometres(Terrain, AlongSurface(Centre, Tangent, 400.0));

	for (double Metres = 401.0; Metres <= 590.0; Metres += 1.0)
	{
		const double Height =
			FPlanetTerrain::ElevationKilometres(Terrain, AlongSurface(Centre, Tangent, Metres));

		Worst = FMath::Max(Worst, FMath::Abs(Height - Previous) * 1000.0);
		Previous = Height;
	}

	// The highest the land gets across the ring bounds how much height the blend has to cover.
	double Land = 0.0;

	for (double Metres = 400.0; Metres <= 590.0; Metres += 5.0)
	{
		Land = FMath::Max(Land,
			FPlanetTerrain::ElevationKilometres(TerrainTestConfig(), AlongSurface(Centre, Tangent, Metres)));
	}

	// Metres of rise per metre: 1.5 x (land height / 150 m) for the blend, plus a metre of slack for
	// the land's own slope at this frequency.
	const double Allowed = 1.5 * (Land * 1000.0 / 150.0) + 1.0;

	TestTrue(
		*FString::Printf(TEXT("Steepest metre %.3f m, allowed %.3f m"), Worst, Allowed),
		Worst <= Allowed);

	// And the blend actually did something: the pad edge is the pad, the ring's far edge is the land.
	TestTrue(TEXT("The land is well above the pad here, or this proves nothing"), Land > 0.05);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOTerrainPadSizeIsAlongTheSurfaceTest,
	"SpaceMMO.Terrain.PadSizeIsAlongTheSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOTerrainPadSizeIsAlongTheSurfaceTest::RunTest(const FString& Parameters)
{
	// Content authors kilometres; the height function works in angles. MakePad is the only place one
	// becomes the other, and getting it wrong by the radius would be a pad twenty times too big or
	// small -- a city standing on the slope of its own levelled hill, or a hill levelled flat.
	const FPlanetTerrainPad Pad =
		FPlanetTerrain::MakePad(TEXT("station_test"), FVector(0.0, 0.0, 2.0), 0.42, 0.15, 0.1, 20.0);

	TestTrue(TEXT("Direction normalised"), FMath::IsNearlyEqual(Pad.Direction.Size(), 1.0, 1e-12));
	TestTrue(TEXT("420 m on a 20 km planet"), FMath::IsNearlyEqual(Pad.FlatRadians, 0.021, 1e-12));
	TestTrue(TEXT("150 m on a 20 km planet"), FMath::IsNearlyEqual(Pad.BlendRadians, 0.0075, 1e-12));

	// Inside by a metre, outside by a metre: the edges are where content said.
	const FVector Centre = FVector::ZAxisVector;

	TestEqual(TEXT("Flat at 419 m"),
		FPlanetTerrain::PadWeight(Pad, AlongSurface(Centre, FVector::XAxisVector, 419.0)), 1.0);
	TestTrue(TEXT("Easing at 421 m"),
		FPlanetTerrain::PadWeight(Pad, AlongSurface(Centre, FVector::XAxisVector, 421.0)) < 1.0);
	TestEqual(TEXT("Gone at 571 m"),
		FPlanetTerrain::PadWeight(Pad, AlongSurface(Centre, FVector::XAxisVector, 571.0)), 0.0);

	return true;
}

#endif
