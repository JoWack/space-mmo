#include "SpaceMMOPlanet.h"
#include "Misc/AutomationTest.h"

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

#endif // WITH_DEV_AUTOMATION_TESTS
