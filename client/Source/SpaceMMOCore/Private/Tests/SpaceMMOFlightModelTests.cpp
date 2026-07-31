#include "SpaceMMOFlightModel.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Tests for 6DOF flight (design-bible §8).
 *
 * The property that matters most is that velocity lives in the system frame while thrust is
 * applied in the ship's — that separation is what makes a ship keep drifting the way it was going
 * while it turns to face somewhere else, and it is most of what makes space flight feel like space
 * flight rather than like driving.
 *
 * See SpaceMMOCoordinatesTests.cpp for how to run these.
 */

namespace
{
	constexpr double FlightTolerance = 1e-3;

	/** One frame at 60 Hz. */
	constexpr double FrameSeconds = 1.0 / 60.0;

	FShipFlightConfig TestConfig()
	{
		FShipFlightConfig Config;
		Config.ThrustAcceleration = 1000.0;
		Config.AngularAcceleration = 100.0;
		Config.LinearDamping = 0.0;   // Newtonian unless a test asks otherwise.
		Config.AngularDamping = 0.0;
		Config.MaxSpeed = 100000.0;
		Config.MaxAngularSpeed = 180.0;
		Config.BoostMultiplier = 4.0;

		return Config;
	}

	FShipFlightInput ThrustForward(const double Amount = 1.0)
	{
		FShipFlightInput Input;
		Input.Thrust = FVector(Amount, 0.0, 0.0);

		return Input;
	}

	/** Runs the model for a while, so behaviour over time can be checked rather than one step. */
	FShipFlightState Simulate(
		FShipFlightState State,
		const FShipFlightInput& Input,
		const FShipFlightConfig& Config,
		const int32 Frames)
	{
		for (int32 Index = 0; Index < Frames; ++Index)
		{
			State = FShipFlightModel::Step(State, Input, Config, FrameSeconds);
		}

		return State;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOFlightThrustAcceleratesTest,
	"SpaceMMO.Flight.ThrustAccelerates",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOFlightThrustAcceleratesTest::RunTest(const FString& Parameters)
{
	const FShipFlightState After =
		FShipFlightModel::Step(FShipFlightState(), ThrustForward(), TestConfig(), 1.0);

	// A second at 1000 cm/s^2 is 1000 cm/s, along the ship's forward axis.
	TestEqual(TEXT("Forward speed"), After.Velocity.X, 1000.0, FlightTolerance);
	TestEqual(TEXT("No sideways drift"), After.Velocity.Y, 0.0, FlightTolerance);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOFlightThrustFollowsFacingTest,
	"SpaceMMO.Flight.ThrustFollowsFacing",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOFlightThrustFollowsFacingTest::RunTest(const FString& Parameters)
{
	FShipFlightState State;
	State.Rotation = FQuat(FVector::UpVector, FMath::DegreesToRadians(90.0));

	const FShipFlightState After =
		FShipFlightModel::Step(State, ThrustForward(), TestConfig(), 1.0);

	// Yawed 90 degrees, "forward" is +Y in system axes. If this came out along X, thrust was never
	// rotated and every ship would accelerate the same direction regardless of facing.
	TestEqual(TEXT("Accelerates along Y"), After.Velocity.Y, 1000.0, FlightTolerance);
	TestEqual(TEXT("Not along X"), After.Velocity.X, 0.0, FlightTolerance);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOFlightDriftsWhileTurningTest,
	"SpaceMMO.Flight.DriftsWhileTurning",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOFlightDriftsWhileTurningTest::RunTest(const FString& Parameters)
{
	const FShipFlightConfig Config = TestConfig();

	// Build up speed going "forward"...
	FShipFlightState State = Simulate(FShipFlightState(), ThrustForward(), Config, 60);

	const double SpeedBefore = State.Speed();

	// ...then turn hard with the engines off.
	FShipFlightInput Yaw;
	Yaw.Torque = FVector(0.0, 0.0, 1.0);

	State = Simulate(State, Yaw, Config, 60);

	// The single most characteristic behaviour of space flight: turning changes where the ship
	// points, not where it is going. Velocity is unchanged while the nose has swung round.
	TestEqual(TEXT("Speed unchanged"), State.Speed(), SpeedBefore, FlightTolerance);
	TestEqual(TEXT("Still drifting along X"), State.Velocity.X, SpeedBefore, FlightTolerance);
	TestTrue(TEXT("But facing has changed"), !State.Rotation.IsIdentity(0.01));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOFlightNewtonianCoastTest,
	"SpaceMMO.Flight.NewtonianCoast",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOFlightNewtonianCoastTest::RunTest(const FString& Parameters)
{
	FShipFlightConfig Config = TestConfig();
	Config.LinearDamping = 0.0;

	FShipFlightState State = Simulate(FShipFlightState(), ThrustForward(), Config, 60);
	const double SpeedBefore = State.Speed();

	// Engines off for ten seconds. With no assist, nothing slows the ship down.
	State = Simulate(State, FShipFlightInput(), Config, 600);

	TestEqual(TEXT("Coasts unchanged"), State.Speed(), SpeedBefore, FlightTolerance);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOFlightAssistSlowsShipTest,
	"SpaceMMO.Flight.FlightAssistSlowsShip",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOFlightAssistSlowsShipTest::RunTest(const FString& Parameters)
{
	FShipFlightConfig Config = TestConfig();
	Config.LinearDamping = 1.0;

	FShipFlightState State = Simulate(FShipFlightState(), ThrustForward(), Config, 60);
	const double SpeedBefore = State.Speed();

	State = Simulate(State, FShipFlightInput(), Config, 60);

	// One second at a rate of 1.0 leaves roughly 1/e of the speed.
	TestTrue(TEXT("Slowed"), State.Speed() < SpeedBefore);
	TestEqual(TEXT("Decayed exponentially"), State.Speed(), SpeedBefore * FMath::Exp(-1.0), 1.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOFlightDampingIsFrameRateIndependentTest,
	"SpaceMMO.Flight.DampingIsFrameRateIndependent",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOFlightDampingIsFrameRateIndependentTest::RunTest(const FString& Parameters)
{
	FShipFlightConfig Config = TestConfig();
	Config.LinearDamping = 2.0;

	FShipFlightState Start;
	Start.Velocity = FVector(10000.0, 0.0, 0.0);

	// The same second of simulated time, at 60 Hz and at 10 Hz.
	FShipFlightState Fast = Start;
	for (int32 Index = 0; Index < 60; ++Index)
	{
		Fast = FShipFlightModel::Step(Fast, FShipFlightInput(), Config, 1.0 / 60.0);
	}

	FShipFlightState Slow = Start;
	for (int32 Index = 0; Index < 10; ++Index)
	{
		Slow = FShipFlightModel::Step(Slow, FShipFlightInput(), Config, 1.0 / 10.0);
	}

	// Exponential decay makes these agree. A linear subtraction would not, and a player on a weak
	// machine would decelerate differently from everyone else — which in a server-authoritative
	// game means constant correction snapping.
	TestEqual(TEXT("Same result at either rate"), Fast.Speed(), Slow.Speed(), 1.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOFlightSpeedClampTest,
	"SpaceMMO.Flight.SpeedIsClamped",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOFlightSpeedClampTest::RunTest(const FString& Parameters)
{
	const FShipFlightConfig Config = TestConfig();

	// Thrust for far longer than it takes to reach the ceiling.
	const FShipFlightState State =
		Simulate(FShipFlightState(), ThrustForward(), Config, 60 * 600);

	TestEqual(TEXT("Held at the ceiling"), State.Speed(), Config.MaxSpeed, 1.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOFlightClampPreservesDirectionTest,
	"SpaceMMO.Flight.ClampPreservesDirection",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOFlightClampPreservesDirectionTest::RunTest(const FString& Parameters)
{
	const FShipFlightConfig Config = TestConfig();

	FShipFlightState State;
	State.Rotation = FQuat(FVector::UpVector, FMath::DegreesToRadians(45.0));

	const FShipFlightState After = Simulate(State, ThrustForward(), Config, 60 * 600);

	// Clamping scales the vector rather than clipping components, so a diagonal heading stays
	// diagonal. Per-component clamping would silently bend a ship's course as it sped up.
	TestEqual(TEXT("Still 45 degrees"), After.Velocity.X, After.Velocity.Y, 1.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOFlightBoostTest,
	"SpaceMMO.Flight.Boost",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOFlightBoostTest::RunTest(const FString& Parameters)
{
	const FShipFlightConfig Config = TestConfig();

	FShipFlightInput Boosted = ThrustForward();
	Boosted.bBoost = true;

	const FShipFlightState Normal =
		FShipFlightModel::Step(FShipFlightState(), ThrustForward(), Config, 1.0);

	const FShipFlightState Fast =
		FShipFlightModel::Step(FShipFlightState(), Boosted, Config, 1.0);

	TestEqual(TEXT("Boost multiplies thrust"), Fast.Speed(), Normal.Speed() * Config.BoostMultiplier, FlightTolerance);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOFlightInputIsClampedTest,
	"SpaceMMO.Flight.InputIsClamped",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOFlightInputIsClampedTest::RunTest(const FString& Parameters)
{
	const FShipFlightConfig Config = TestConfig();

	// A client sending 100 on an axis must fly exactly as fast as one sending 1. Input arrives
	// over the network, so this is a cheat, not a rounding concern.
	FShipFlightInput Cheating;
	Cheating.Thrust = FVector(100.0, 0.0, 0.0);

	const FShipFlightState Cheated =
		FShipFlightModel::Step(FShipFlightState(), Cheating, Config, 1.0);

	const FShipFlightState Honest =
		FShipFlightModel::Step(FShipFlightState(), ThrustForward(), Config, 1.0);

	TestEqual(TEXT("No advantage"), Cheated.Speed(), Honest.Speed(), FlightTolerance);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOFlightAngularClampTest,
	"SpaceMMO.Flight.AngularSpeedIsClamped",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOFlightAngularClampTest::RunTest(const FString& Parameters)
{
	const FShipFlightConfig Config = TestConfig();

	FShipFlightInput Spin;
	Spin.Torque = FVector(1.0, 1.0, 1.0);

	const FShipFlightState State = Simulate(FShipFlightState(), Spin, Config, 60 * 60);

	TestTrue(
		TEXT("Within the ceiling"),
		State.AngularVelocity.Size() <= Config.MaxAngularSpeed + FlightTolerance);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOFlightPositionDeltaTest,
	"SpaceMMO.Flight.PositionDeltaInKilometres",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOFlightPositionDeltaTest::RunTest(const FString& Parameters)
{
	FShipFlightState State;

	// 100,000 cm/s is 1 km/s, so one second covers exactly one kilometre of system space.
	State.Velocity = FVector(100000.0, 0.0, 0.0);

	const FVector Delta = FShipFlightModel::PositionDeltaKilometres(State, 1.0);

	TestEqual(TEXT("One kilometre"), Delta.X, 1.0, FlightTolerance);
	TestEqual(TEXT("Speed helper agrees"), State.SpeedKilometresPerSecond(), 1.0, FlightTolerance);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOFlightZeroTimeIsNoOpTest,
	"SpaceMMO.Flight.ZeroDeltaTimeIsNoOp",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOFlightZeroTimeIsNoOpTest::RunTest(const FString& Parameters)
{
	FShipFlightState State;
	State.Velocity = FVector(1234.0, 5678.0, 9012.0);

	// A paused or hitching frame must not move anything, and must not divide by zero either.
	const FShipFlightState Stepped =
		FShipFlightModel::Step(State, ThrustForward(), TestConfig(), 0.0);

	TestEqual(TEXT("Velocity untouched"), Stepped.Velocity.X, State.Velocity.X, FlightTolerance);
	TestTrue(
		TEXT("No movement"),
		FShipFlightModel::PositionDeltaKilometres(State, 0.0).IsNearlyZero());

	return true;
}

// ── Navigation and render-origin rebasing ────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMONavigationAccumulatesTest,
	"SpaceMMO.Navigation.PositionAccumulates",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMONavigationAccumulatesTest::RunTest(const FString& Parameters)
{
	FShipFlightState State;
	State.Velocity = FVector(100000.0, 0.0, 0.0); // 1 km/s

	FShipNavigation Navigation;

	for (int32 Second = 0; Second < 5; ++Second)
	{
		Navigation = FShipFlightModel::Advance(Navigation, State, 1.0);
	}

	TestEqual(TEXT("Five kilometres"), Navigation.SystemPosition.Kilometres.X, 5.0, FlightTolerance);
	TestEqual(TEXT("No rebase needed yet"), Navigation.RebaseCount, 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMONavigationRebasesTest,
	"SpaceMMO.Navigation.RebasesWhenOutOfBudget",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMONavigationRebasesTest::RunTest(const FString& Parameters)
{
	FShipFlightState State;
	State.Velocity = FVector(100000.0, 0.0, 0.0); // 1 km/s

	FShipNavigation Navigation;

	// The budget is 20 km, so 30 seconds must cross it.
	for (int32 Second = 0; Second < 30; ++Second)
	{
		Navigation = FShipFlightModel::Advance(Navigation, State, 1.0);
	}

	TestTrue(TEXT("Rebased"), Navigation.RebaseCount > 0);

	// The ship really is 30 km out...
	TestEqual(TEXT("System position kept"), Navigation.SystemPosition.Kilometres.X, 30.0, FlightTolerance);

	// ...but renders near the origin, which is the entire point.
	TestTrue(
		TEXT("Renders near origin"),
		Navigation.RenderLocationCentimetres().Size() < SpaceMMO::Coordinates::LocalSpaceLimitCentimetres);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMONavigationStaysInBudgetTest,
	"SpaceMMO.Navigation.AlwaysStaysWithinPhysicsBudget",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMONavigationStaysInBudgetTest::RunTest(const FString& Parameters)
{
	// The guarantee the whole mechanism exists to provide: however far the ship travels, and
	// however fast, the render location never leaves the range Chaos behaves well in.
	FShipNavigation Navigation;

	for (const double SpeedCmPerSecond : { 1000.0, 100000.0, 5000000.0, 200000000.0 })
	{
		FShipFlightState State;
		State.Velocity = FVector(SpeedCmPerSecond, SpeedCmPerSecond * 0.5, 0.0);

		for (int32 Frame = 0; Frame < 600; ++Frame)
		{
			Navigation = FShipFlightModel::Advance(Navigation, State, 1.0 / 60.0);

			TestTrue(
				FString::Printf(
					TEXT("Within budget at %.0f cm/s: render location was %.1f cm"),
					SpeedCmPerSecond,
					Navigation.RenderLocationCentimetres().Size()),
				Navigation.RenderLocationCentimetres().Size()
					<= SpaceMMO::Coordinates::LocalSpaceLimitCentimetres);
		}
	}

	// And after all that it is genuinely a long way from where it started. Four ten-second phases
	// at |v| = speed * sqrt(1.25) come to about 22,931 km, so anything near that confirms the ship
	// really travelled rather than the loop quietly doing nothing.
	TestTrue(
		FString::Printf(
			TEXT("Travelled far: %.0f km"), Navigation.SystemPosition.Kilometres.Size()),
		Navigation.SystemPosition.Kilometres.Size() > 22000.0);

	TestTrue(TEXT("Rebased many times"), Navigation.RebaseCount > 10);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMONavigationRebaseIsInvisibleTest,
	"SpaceMMO.Navigation.RebaseDoesNotMoveTheShip",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMONavigationRebaseIsInvisibleTest::RunTest(const FString& Parameters)
{
	FShipFlightState State;
	State.Velocity = FVector(3000000.0, 0.0, 0.0); // 30 km/s — one step crosses the budget

	FShipNavigation Before;
	Before.SystemPosition = FSystemCoordinate(1000.0, 0.0, 0.0);
	Before.RenderOrigin = Before.SystemPosition;

	const FShipNavigation After = FShipFlightModel::Advance(Before, State, 1.0);

	// A rebase happened, and the ship's actual position advanced by exactly the distance flown —
	// no more and no less. If rebasing ever altered the system position, ships would teleport
	// whenever they crossed a boundary.
	TestTrue(TEXT("Rebased"), After.RebaseCount == Before.RebaseCount + 1);
	TestEqual(TEXT("Moved exactly 30 km"), After.SystemPosition.Kilometres.X, 1030.0, FlightTolerance);

	// And having just rebased, it sits exactly at the origin.
	TestTrue(TEXT("At the origin"), After.RenderLocationCentimetres().IsNearlyZero(1.0));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMONavigationStationaryTest,
	"SpaceMMO.Navigation.StationaryShipNeverRebases",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMONavigationStationaryTest::RunTest(const FString& Parameters)
{
	// Rebasing costs a discontinuity for everything else being rendered, so a parked ship must
	// never trigger one however long it sits there.
	FShipNavigation Navigation;
	Navigation.SystemPosition = FSystemCoordinate(500.0, -200.0, 75.0);
	Navigation.RenderOrigin = Navigation.SystemPosition;

	for (int32 Frame = 0; Frame < 3600; ++Frame)
	{
		Navigation = FShipFlightModel::Advance(Navigation, FShipFlightState(), 1.0 / 60.0);
	}

	TestEqual(TEXT("Never rebased"), Navigation.RebaseCount, 0);
	TestTrue(TEXT("Never moved"), Navigation.RenderLocationCentimetres().IsNearlyZero(FlightTolerance));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
