#include "SpaceMMOPlanet.h"
#include "Misc/AutomationTest.h"
#include "SpaceMMOFlightModel.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Tests for spherical gravity and planet proximity.
 *
 * The property that matters is that "down" is a direction computed from where you are, not a
 * constant. That is what makes a curved surface work: walk far enough and down has rotated,
 * without anything having to notice or handle it.
 *
 * See SpaceMMOCoordinatesTests.cpp for how to run these.
 */

namespace
{
	constexpr double PlanetTolerance = 1e-6;

	/** A 20 km world with Earth-like surface gravity, centred at the origin. */
	FPlanetConfig TestPlanet()
	{
		FPlanetConfig Planet;
		Planet.Centre = FSystemCoordinate(0.0, 0.0, 0.0);
		Planet.RadiusKilometres = 20.0;
		Planet.SurfaceGravity = 981.0;
		Planet.AtmosphereHeightKilometres = 12.0;
		Planet.SurfaceBandKilometres = 0.2;
		Planet.ProximityHysteresisKilometres = 1.0;

		return Planet;
	}

	/** A position at a given altitude along an axis. */
	FSystemCoordinate At(const FVector& Direction, const double AltitudeKilometres)
	{
		const FPlanetConfig Planet = TestPlanet();

		return FSystemCoordinate(
			Direction.GetSafeNormal() * (Planet.RadiusKilometres + AltitudeKilometres));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOPlanetAltitudeTest,
	"SpaceMMO.Planet.Altitude",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOPlanetAltitudeTest::RunTest(const FString& Parameters)
{
	const FPlanetConfig Planet = TestPlanet();

	TestEqual(TEXT("At the surface"),
		FPlanetPhysics::AltitudeKilometres(Planet, At(FVector::XAxisVector, 0.0)), 0.0, PlanetTolerance);

	TestEqual(TEXT("Five km up"),
		FPlanetPhysics::AltitudeKilometres(Planet, At(FVector::ZAxisVector, 5.0)), 5.0, PlanetTolerance);

	// Altitude is measured from the surface, not the centre — the distinction that makes "how high
	// am I?" mean what a pilot expects.
	TestEqual(TEXT("Centre is a negative altitude"),
		FPlanetPhysics::AltitudeKilometres(Planet, Planet.Centre), -20.0, PlanetTolerance);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOPlanetUpIsRadialTest,
	"SpaceMMO.Planet.UpIsRadial",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOPlanetUpIsRadialTest::RunTest(const FString& Parameters)
{
	const FPlanetConfig Planet = TestPlanet();

	// The whole point of a round world: up depends on where you are stood.
	TestTrue(TEXT("Up at the north pole"),
		FPlanetPhysics::UpDirection(Planet, At(FVector::ZAxisVector, 1.0))
			.Equals(FVector::ZAxisVector, PlanetTolerance));

	TestTrue(TEXT("Up on the equator"),
		FPlanetPhysics::UpDirection(Planet, At(FVector::XAxisVector, 1.0))
			.Equals(FVector::XAxisVector, PlanetTolerance));

	// A quarter of the way round, up has rotated ninety degrees — which is exactly what a
	// character walking that far has to be reoriented by.
	const FVector PoleUp = FPlanetPhysics::UpDirection(Planet, At(FVector::ZAxisVector, 1.0));
	const FVector EquatorUp = FPlanetPhysics::UpDirection(Planet, At(FVector::XAxisVector, 1.0));

	TestEqual(TEXT("Perpendicular"), FVector::DotProduct(PoleUp, EquatorUp), 0.0, PlanetTolerance);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOPlanetUpAtCentreTest,
	"SpaceMMO.Planet.UpAtCentreIsDefined",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOPlanetUpAtCentreTest::RunTest(const FString& Parameters)
{
	const FPlanetConfig Planet = TestPlanet();

	// There is no real answer at the centre, so callers get a usable one rather than a zero vector
	// that silently breaks any normalisation downstream.
	TestTrue(TEXT("Defined"),
		FPlanetPhysics::UpDirection(Planet, Planet.Centre).IsNormalized());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOPlanetGravityPullsDownTest,
	"SpaceMMO.Planet.GravityPullsTowardCentre",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOPlanetGravityPullsDownTest::RunTest(const FString& Parameters)
{
	const FPlanetConfig Planet = TestPlanet();

	for (const FVector& Axis : { FVector::XAxisVector, FVector::YAxisVector, FVector::ZAxisVector })
	{
		const FSystemCoordinate Position = At(Axis, 5.0);
		const FVector Gravity = FPlanetPhysics::GravityAcceleration(Planet, Position);
		const FVector Up = FPlanetPhysics::UpDirection(Planet, Position);

		// Gravity is exactly opposite to up, wherever you are.
		TestEqual(
			FString::Printf(TEXT("Opposes up along %s"), *Axis.ToCompactString()),
			FVector::DotProduct(Gravity.GetSafeNormal(), Up),
			-1.0,
			1e-6);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOPlanetSurfaceGravityTest,
	"SpaceMMO.Planet.SurfaceGravityMatchesConfig",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOPlanetSurfaceGravityTest::RunTest(const FString& Parameters)
{
	const FPlanetConfig Planet = TestPlanet();

	const double Magnitude =
		FPlanetPhysics::GravityAcceleration(Planet, At(FVector::ZAxisVector, 0.0)).Size();

	// A configured 981 must actually be 981 at the surface, or every planet's feel is off by
	// whatever the formula happens to produce.
	TestEqual(TEXT("Surface gravity"), Magnitude, Planet.SurfaceGravity, 1e-6);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOPlanetInverseSquareTest,
	"SpaceMMO.Planet.GravityFallsOffInverseSquare",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOPlanetInverseSquareTest::RunTest(const FString& Parameters)
{
	const FPlanetConfig Planet = TestPlanet();

	// At twice the radius, a quarter of the pull.
	const double AtTwiceRadius =
		FPlanetPhysics::GravityAcceleration(Planet, At(FVector::ZAxisVector, 20.0)).Size();

	TestEqual(TEXT("Quarter strength"), AtTwiceRadius, Planet.SurfaceGravity / 4.0, 1e-6);

	// And at three times, a ninth.
	const double AtThriceRadius =
		FPlanetPhysics::GravityAcceleration(Planet, At(FVector::ZAxisVector, 40.0)).Size();

	TestEqual(TEXT("Ninth strength"), AtThriceRadius, Planet.SurfaceGravity / 9.0, 1e-6);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOPlanetGravityIsContinuousTest,
	"SpaceMMO.Planet.GravityIsContinuousAtTheSurface",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOPlanetGravityIsContinuousTest::RunTest(const FString& Parameters)
{
	const FPlanetConfig Planet = TestPlanet();

	// Inside and outside use different formulas, so they must agree where they meet — otherwise
	// anything crossing the surface takes a sudden jolt.
	const double JustBelow =
		FPlanetPhysics::GravityAcceleration(Planet, At(FVector::ZAxisVector, -0.001)).Size();

	const double JustAbove =
		FPlanetPhysics::GravityAcceleration(Planet, At(FVector::ZAxisVector, 0.001)).Size();

	TestEqual(TEXT("No discontinuity"), JustBelow, JustAbove, 0.2);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOPlanetGravityAtCentreTest,
	"SpaceMMO.Planet.GravityAtCentreIsZero",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOPlanetGravityAtCentreTest::RunTest(const FString& Parameters)
{
	const FPlanetConfig Planet = TestPlanet();

	// Inverse square would divide by zero here. The linear interior model gives the physically
	// correct answer instead: pull from every direction cancels.
	TestTrue(TEXT("Cancels"),
		FPlanetPhysics::GravityAcceleration(Planet, Planet.Centre).IsNearlyZero());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOPlanetOffsetCentreTest,
	"SpaceMMO.Planet.WorksAwayFromTheSystemOrigin",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOPlanetOffsetCentreTest::RunTest(const FString& Parameters)
{
	// Planets do not sit at the system barycentre, so nothing here may assume the centre is zero.
	FPlanetConfig Planet = TestPlanet();
	Planet.Centre = FSystemCoordinate(1000000.0, -500000.0, 250000.0);

	const FSystemCoordinate Surface(
		Planet.Centre.Kilometres + FVector(Planet.RadiusKilometres, 0.0, 0.0));

	TestEqual(TEXT("Altitude"),
		FPlanetPhysics::AltitudeKilometres(Planet, Surface), 0.0, 1e-4);

	TestEqual(TEXT("Surface gravity"),
		FPlanetPhysics::GravityAcceleration(Planet, Surface).Size(), Planet.SurfaceGravity, 1e-3);

	TestTrue(TEXT("Up is radial"),
		FPlanetPhysics::UpDirection(Planet, Surface).Equals(FVector::XAxisVector, 1e-6));

	return true;
}

// ── Proximity ────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOPlanetProximityTest,
	"SpaceMMO.Planet.ProximityClassification",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOPlanetProximityTest::RunTest(const FString& Parameters)
{
	const FPlanetConfig Planet = TestPlanet();

	TestEqual(TEXT("On the ground"),
		FPlanetPhysics::ClassifyProximity(Planet, At(FVector::ZAxisVector, 0.0)),
		EPlanetProximity::Surface);

	TestEqual(TEXT("In the air"),
		FPlanetPhysics::ClassifyProximity(Planet, At(FVector::ZAxisVector, 5.0)),
		EPlanetProximity::Atmospheric);

	TestEqual(TEXT("In space"),
		FPlanetPhysics::ClassifyProximity(Planet, At(FVector::ZAxisVector, 500.0)),
		EPlanetProximity::Orbital);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOPlanetHysteresisTest,
	"SpaceMMO.Planet.ProximityDoesNotFlicker",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOPlanetHysteresisTest::RunTest(const FString& Parameters)
{
	const FPlanetConfig Planet = TestPlanet();

	// Parked exactly on the atmosphere boundary. Without hysteresis this alternates every frame,
	// and anything keyed to the transition — terrain streaming, physics mode, audio — thrashes
	// with it.
	const FSystemCoordinate OnTheBoundary =
		At(FVector::ZAxisVector, Planet.AtmosphereHeightKilometres);

	EPlanetProximity State = EPlanetProximity::Orbital;
	State = FPlanetPhysics::ClassifyProximity(Planet, OnTheBoundary, State);

	TestEqual(TEXT("Entered the atmosphere"), State, EPlanetProximity::Atmospheric);

	// Now hold position for a while. It must stay put rather than oscillate.
	for (int32 Frame = 0; Frame < 100; ++Frame)
	{
		State = FPlanetPhysics::ClassifyProximity(Planet, OnTheBoundary, State);

		TestEqual(TEXT("Stays put"), State, EPlanetProximity::Atmospheric);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOPlanetLeavesAtmosphereTest,
	"SpaceMMO.Planet.LeavingRequiresClearingHysteresis",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOPlanetLeavesAtmosphereTest::RunTest(const FString& Parameters)
{
	const FPlanetConfig Planet = TestPlanet();

	// Just past the boundary is not enough to leave...
	const EPlanetProximity StillInside = FPlanetPhysics::ClassifyProximity(
		Planet,
		At(FVector::ZAxisVector, Planet.AtmosphereHeightKilometres + 0.5),
		EPlanetProximity::Atmospheric);

	TestEqual(TEXT("Still atmospheric"), StillInside, EPlanetProximity::Atmospheric);

	// ...but clearing the hysteresis band is.
	const EPlanetProximity NowOut = FPlanetPhysics::ClassifyProximity(
		Planet,
		At(FVector::ZAxisVector,
			Planet.AtmosphereHeightKilometres + Planet.ProximityHysteresisKilometres + 0.5),
		EPlanetProximity::Atmospheric);

	TestEqual(TEXT("Left the atmosphere"), NowOut, EPlanetProximity::Orbital);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOPlanetDescentTest,
	"SpaceMMO.Planet.FullDescentPassesThroughEveryState",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOPlanetDescentTest::RunTest(const FString& Parameters)
{
	const FPlanetConfig Planet = TestPlanet();

	// Fly in from 200 km to the ground and confirm the transitions happen in order, with none
	// skipped. Skipping one would mean terrain never started streaming on a fast approach.
	TArray<EPlanetProximity> Sequence;
	EPlanetProximity State = EPlanetProximity::Orbital;

	for (double Altitude = 200.0; Altitude >= 0.0; Altitude -= 0.1)
	{
		const EPlanetProximity Next =
			FPlanetPhysics::ClassifyProximity(Planet, At(FVector::ZAxisVector, Altitude), State);

		if (Next != State)
		{
			Sequence.Add(Next);
			State = Next;
		}
	}

	TestEqual(TEXT("Two transitions"), Sequence.Num(), 2);

	if (Sequence.Num() == 2)
	{
		TestEqual(TEXT("Orbital to atmospheric"), Sequence[0], EPlanetProximity::Atmospheric);
		TestEqual(TEXT("Atmospheric to surface"), Sequence[1], EPlanetProximity::Surface);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOPlanetOrbitSpeedTest,
	"SpaceMMO.Planet.CircularOrbitSpeed",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOPlanetOrbitSpeedTest::RunTest(const FString& Parameters)
{
	const FPlanetConfig Planet = TestPlanet();

	// v = sqrt(g * r) at the surface: 981 cm/s^2 over a 20 km radius.
	const double Expected = FMath::Sqrt(
		Planet.SurfaceGravity * Planet.RadiusKilometres
		* SpaceMMO::Coordinates::CentimetresPerKilometre);

	TestEqual(TEXT("Surface orbit"),
		FPlanetPhysics::CircularOrbitSpeed(Planet, 0.0), Expected, 1.0);

	// Higher orbits are slower, as they should be.
	TestTrue(TEXT("Slower higher up"),
		FPlanetPhysics::CircularOrbitSpeed(Planet, 100.0)
			< FPlanetPhysics::CircularOrbitSpeed(Planet, 0.0));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOAirIsThickAtTheGroundAndAbsentInSpaceTest,
	"SpaceMMO.Planet.AirIsThickAtTheGroundAndAbsentInSpace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOAirIsThickAtTheGroundAndAbsentInSpaceTest::RunTest(const FString& Parameters)
{
	const FPlanetConfig Planet = TestPlanet();

	TestEqual(
		TEXT("Sea level is full density"),
		FPlanetPhysics::AtmosphericDensity(Planet, 0.0), 1.0, PlanetTolerance);

	// Exactly zero, not merely small. Anything above the atmosphere must be in vacuum, or an orbit
	// decays over hours for reasons nobody will connect to a drag model years later.
	TestEqual(
		TEXT("The top of the atmosphere is vacuum"),
		FPlanetPhysics::AtmosphericDensity(Planet, Planet.AtmosphereHeightKilometres),
		0.0, PlanetTolerance);

	TestEqual(
		TEXT("Space is vacuum"),
		FPlanetPhysics::AtmosphericDensity(Planet, 500.0), 0.0, PlanetTolerance);

	// Below the ground is sea level rather than thicker, so a hard landing cannot produce a drag
	// force that grows without bound while the ship is briefly inside the terrain.
	TestEqual(
		TEXT("Underground is not thicker than sea level"),
		FPlanetPhysics::AtmosphericDensity(Planet, -1.0), 1.0, PlanetTolerance);

	TestTrue(
		TEXT("Air thins with height"),
		FPlanetPhysics::AtmosphericDensity(Planet, 2.0)
			> FPlanetPhysics::AtmosphericDensity(Planet, 8.0));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOTerminalSpeedStaysBelowOrbitalTest,
	"SpaceMMO.Planet.TerminalSpeedStaysBelowOrbital",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOTerminalSpeedStaysBelowOrbitalTest::RunTest(const FString& Parameters)
{
	const FPlanetConfig Planet = TestPlanet();

	// The shipped configuration, not numbers typed into a test. Asserting against constants here
	// would let somebody raise the real terminal speed past orbital and still see green, which is
	// the exact failure this test exists to prevent.
	const FShipFlightConfig Flight;

	const double Thrust = Flight.ThrustAcceleration;
	const double TerminalSpeed = Flight.AtmosphericTerminalSpeed;
	const double BoostMultiplier = Flight.BoostMultiplier;

	// The whole point of the number. A ship faster than circular orbit a few metres up is thrown
	// off the ground by its own speed and cannot fly along the surface at all, only skip across it
	// -- which is what a real flight did, 66 contact transitions in three minutes (task 90).
	const double Orbital = FPlanetPhysics::CircularOrbitSpeed(Planet, 0.0);

	// Boost multiplies thrust, and terminal speed goes as its square root, so this is the fastest
	// a pilot can hold at sea level.
	const double BoostedTerminal = TerminalSpeed * FMath::Sqrt(BoostMultiplier);

	AddInfo(FString::Printf(
		TEXT("Terminal %.0f m/s, boosted %.0f m/s, circular orbit %.0f m/s."),
		TerminalSpeed / 100.0, BoostedTerminal / 100.0, Orbital / 100.0));

	TestTrue(
		TEXT("Even boosted, a ship cannot hold orbital speed in the air"),
		BoostedTerminal < Orbital);

	// At terminal speed drag cancels thrust exactly, which is what makes the name honest.
	const FVector Drag = FPlanetPhysics::AtmosphericDrag(
		Planet, 0.0, FVector(TerminalSpeed, 0.0, 0.0), Thrust, TerminalSpeed);

	TestEqual(TEXT("Drag cancels full thrust at terminal speed"), Drag.Size(), Thrust, 1e-6);
	TestTrue(TEXT("Drag opposes motion"), Drag.X < 0.0);

	// Quadratic: half the speed is a quarter of the force, so slow flight is unencumbered.
	const FVector Half = FPlanetPhysics::AtmosphericDrag(
		Planet, 0.0, FVector(TerminalSpeed * 0.5, 0.0, 0.0), Thrust, TerminalSpeed);

	TestEqual(TEXT("Half speed is a quarter of the drag"), Half.Size(), Thrust * 0.25, 1e-6);

	// Nothing in vacuum, however fast, or leaving the atmosphere would still cost speed.
	const FVector InSpace = FPlanetPhysics::AtmosphericDrag(
		Planet, 100.0, FVector(200000.0, 0.0, 0.0), Thrust, TerminalSpeed);

	TestTrue(TEXT("No drag in space"), InSpace.IsNearlyZero());

	// A stationary ship must feel nothing rather than a NaN from normalising a zero vector.
	const FVector AtRest = FPlanetPhysics::AtmosphericDrag(
		Planet, 0.0, FVector::ZeroVector, Thrust, TerminalSpeed);

	TestTrue(TEXT("No drag at rest"), AtRest.IsNearlyZero());

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
