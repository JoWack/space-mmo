#include "SpaceMMOCoordinates.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Tests for the three-tier coordinate model (ADR-0001).
 *
 * These guard the properties that make seamless planet landing possible: that a position far from
 * the origin can still be resolved to centimetre accuracy nearby, and that converting back and
 * forth loses nothing. Getting this wrong does not look like a coordinate bug — it looks like
 * geometry jittering, physics exploding, and objects in the wrong place.
 *
 * Run with:
 *
 *   UnrealEditor-Cmd.exe SpaceMMO.uproject \
 *     -ExecCmds="Automation RunTests SpaceMMO.Coordinates" \
 *     -testexit="Automation Test Queue Empty" \
 *     -unattended -nopause -nosplash -log
 *
 * Three details, each of which cost a debugging cycle to find:
 *
 *   -testexit is required. Putting "Quit" in -ExecCmds exits as soon as the tests are *queued*,
 *   so the editor shuts down before a single one runs and reports success having done nothing.
 *
 *   -nullrhi crashes UE 5.8 on a TNotNull assertion immediately after engine init.
 *
 *   -NoShaderCompile trips `Assertion failed: AllowShaderCompiling()`. The editor needs to be
 *   able to compile shaders even when nothing is being rendered.
 */

namespace
{
	constexpr double CoordinateTolerance = 1e-6;

	/** One kilometre in system space is one kilometre of Unreal space, in centimetres. */
	constexpr double CentimetresPerKilometre = SpaceMMO::Coordinates::CentimetresPerKilometre;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOSystemToLocalTest,
	"SpaceMMO.Coordinates.SystemToLocal",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOSystemToLocalTest::RunTest(const FString& Parameters)
{
	const FSystemCoordinate Origin(1000.0, 2000.0, 3000.0);
	const FSystemCoordinate Position(1001.0, 2000.0, 3000.0);

	const FVector Local = Position.ToLocalCentimetres(Origin);

	// One kilometre away in system space is 100,000 cm of Unreal space. No scale factor: the
	// universe scale is applied when content is authored, not on every conversion.
	TestEqual(TEXT("X"), Local.X, CentimetresPerKilometre, CoordinateTolerance);
	TestEqual(TEXT("Y"), Local.Y, 0.0, CoordinateTolerance);
	TestEqual(TEXT("Z"), Local.Z, 0.0, CoordinateTolerance);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOAtOriginIsZeroTest,
	"SpaceMMO.Coordinates.AtGridOriginIsZero",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOAtOriginIsZeroTest::RunTest(const FString& Parameters)
{
	// Whatever a grid's system position, the thing it is centred on renders at exactly zero.
	// This is the property that keeps physics near the origin.
	const FSystemCoordinate Origin(149597870.0, -88000000.0, 12345.678);

	TestTrue(TEXT("Renders at zero"), Origin.ToLocalCentimetres(Origin).IsNearlyZero());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMORoundTripTest,
	"SpaceMMO.Coordinates.RoundTrip",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMORoundTripTest::RunTest(const FString& Parameters)
{
	const FSystemCoordinate Origin(500000.0, -250000.0, 75000.0);

	const TArray<FSystemCoordinate> Positions =
	{
		FSystemCoordinate(500000.0, -250000.0, 75000.0),
		FSystemCoordinate(500001.5, -249998.25, 75000.125),
		FSystemCoordinate(499990.0, -250010.0, 74990.0),
	};

	for (const FSystemCoordinate& Position : Positions)
	{
		const FVector Local = Position.ToLocalCentimetres(Origin);
		const FSystemCoordinate Restored = FSystemCoordinate::FromLocalCentimetres(Local, Origin);

		TestEqual(TEXT("X"), Restored.Kilometres.X, Position.Kilometres.X, CoordinateTolerance);
		TestEqual(TEXT("Y"), Restored.Kilometres.Y, Position.Kilometres.Y, CoordinateTolerance);
		TestEqual(TEXT("Z"), Restored.Kilometres.Z, Position.Kilometres.Z, CoordinateTolerance);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOPrecisionFarFromOriginTest,
	"SpaceMMO.Coordinates.PrecisionFarFromOrigin",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOPrecisionFarFromOriginTest::RunTest(const FString& Parameters)
{
	// The reason the tier split exists. A hundred million kilometres out — beyond where single
	// precision has any resolution at all — a one-centimetre step must still be one centimetre.
	// Subtracting in system space first is what preserves it; converting first would not.
	const FSystemCoordinate FarOrigin(100000000.0, 100000000.0, 100000000.0);

	constexpr double OneCentimetreInKilometres = 1.0 / CentimetresPerKilometre;

	const FSystemCoordinate OneCentimetreAway(
		FarOrigin.Kilometres + FVector(OneCentimetreInKilometres, 0.0, 0.0));

	const FVector Local = OneCentimetreAway.ToLocalCentimetres(FarOrigin);

	TestEqual(TEXT("One centimetre survives"), Local.X, 1.0, 0.01);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOLocalSpaceLimitTest,
	"SpaceMMO.Coordinates.LocalSpaceLimit",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOLocalSpaceLimitTest::RunTest(const FString& Parameters)
{
	const FSystemCoordinate Origin(0.0, 0.0, 0.0);

	// The limit is 20 km of Unreal space, which is 20 km of system space.
	const FSystemCoordinate Inside(19.0, 0.0, 0.0);
	const FSystemCoordinate Outside(21.0, 0.0, 0.0);

	TestTrue(TEXT("Inside the budget"), Inside.IsWithinLocalSpaceOf(Origin));
	TestFalse(TEXT("Outside the budget"), Outside.IsWithinLocalSpaceOf(Origin));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOGalaxyDistanceTest,
	"SpaceMMO.Coordinates.GalaxyDistance",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOGalaxyDistanceTest::RunTest(const FString& Parameters)
{
	const FGalaxyCoordinate Origin(0, 0, 0);
	const FGalaxyCoordinate Nearby(3, 4, 0);

	// Exact integer arithmetic, so a warp-range check has a definite answer rather than one that
	// depends on rounding.
	TestEqual(TEXT("Squared distance"), Origin.DistanceSquaredTo(Nearby), static_cast<int64>(25));
	TestEqual(TEXT("Distance"), Origin.DistanceTo(Nearby), 5.0, CoordinateTolerance);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOGalaxyEqualityTest,
	"SpaceMMO.Coordinates.GalaxyEquality",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOGalaxyEqualityTest::RunTest(const FString& Parameters)
{
	// Galaxy coordinates are map keys and generation inputs, so equality and hashing have to be
	// exact — which is why they are integers rather than doubles.
	const FGalaxyCoordinate A(-9000000000LL, 42, 7);
	const FGalaxyCoordinate B(-9000000000LL, 42, 7);
	const FGalaxyCoordinate C(-9000000000LL, 42, 8);

	TestTrue(TEXT("Equal"), A == B);
	TestTrue(TEXT("Not equal"), A != C);
	TestEqual(TEXT("Hashes match"), GetTypeHash(A), GetTypeHash(B));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOScaledRadiusTest,
	"SpaceMMO.Coordinates.ScaledRadius",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOScaledRadiusTest::RunTest(const FString& Parameters)
{
	// Earth's true radius scaled for storage. ADR-0001 quotes ~637 km, and the seeded content
	// uses 637.1 — this is where that number comes from.
	const double Scaled = USpaceMMOCoordinateLibrary::ScaledRadiusKilometres(6371.0);

	TestEqual(TEXT("Earth-analog radius"), Scaled, 637.1, 1e-9);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
