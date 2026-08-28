#include "SpaceMMOFlightModel.h"
#include "SpaceMMOSurfaces.h"

namespace
{
	/**
	 * Applies exponential decay over a timestep.
	 *
	 * Exponential rather than a linear subtraction so the result does not depend on frame rate. A
	 * linear `Value -= Rate * Dt` overshoots past zero at low frame rates and can flip the sign,
	 * which shows up as a ship jittering backwards when the game hitches.
	 */
	FVector ApplyDamping(const FVector& Value, const double RatePerSecond, const double DeltaSeconds)
	{
		if (RatePerSecond <= 0.0)
		{
			return Value;
		}

		return Value * FMath::Exp(-RatePerSecond * DeltaSeconds);
	}

	/** Clamps a vector's magnitude, leaving direction alone. */
	FVector ClampMagnitude(const FVector& Value, const double MaxMagnitude)
	{
		const double Magnitude = Value.Size();

		if (MaxMagnitude <= 0.0 || Magnitude <= MaxMagnitude || Magnitude <= UE_DOUBLE_SMALL_NUMBER)
		{
			return Value;
		}

		return Value * (MaxMagnitude / Magnitude);
	}
}

FVector FShipFlightModel::ThrustDirection(const FQuat& Rotation, const FVector& LocalThrust)
{
	// Thrust is authored in the ship's own axes and has to be rotated into the system frame before
	// it means anything. Skipping this is the classic bug where a ship accelerates north whichever
	// way its nose is pointing.
	return Rotation.RotateVector(LocalThrust);
}

FShipFlightState FShipFlightModel::Step(
	const FShipFlightState& State,
	const FShipFlightInput& Input,
	const FShipFlightConfig& Config,
	const double DeltaSeconds,
	const FVector& ExternalAcceleration)
{
	if (DeltaSeconds <= 0.0)
	{
		return State;
	}

	const FShipFlightInput Clean = Input.Sanitised();

	FShipFlightState Result = State;

	// ── Rotation ─────────────────────────────────────────────────────────────

	const FVector AngularAcceleration = Clean.Torque * Config.AngularAcceleration;

	Result.AngularVelocity += AngularAcceleration * DeltaSeconds;

	// Damping applies only on axes the pilot is not driving. Damping a held input would fight the
	// pilot and make the ship feel like it is wading through something.
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		if (FMath::IsNearlyZero(Clean.Torque[Axis]))
		{
			const FVector Damped = ApplyDamping(
				FVector(Result.AngularVelocity[Axis], 0.0, 0.0), Config.AngularDamping, DeltaSeconds);

			Result.AngularVelocity[Axis] = Damped.X;
		}
	}

	Result.AngularVelocity = ClampMagnitude(Result.AngularVelocity, Config.MaxAngularSpeed);

	// Integrate orientation. Rotations compose in the ship's local frame, so the delta goes on the
	// right — the other order would apply the turn in system axes and make a rolled ship pitch in
	// the wrong direction entirely.
	const FVector RotationStep = Result.AngularVelocity * DeltaSeconds;

	const FQuat DeltaRotation =
		FQuat(FRotator(RotationStep.Y, RotationStep.Z, RotationStep.X));

	Result.Rotation = (Result.Rotation * DeltaRotation).GetNormalized();

	// ── Translation ──────────────────────────────────────────────────────────

	const double ThrustScale = Clean.bBoost
		? Config.ThrustAcceleration * Config.BoostMultiplier
		: Config.ThrustAcceleration;

	// Rotated by the *new* orientation, so a frame of turning and thrusting pushes the ship where
	// it ends up pointing rather than where it started.
	const FVector Acceleration = ThrustDirection(Result.Rotation, Clean.Thrust) * ThrustScale;

	Result.Velocity += Acceleration * DeltaSeconds;

	// Velocity lives in the system frame, so "is the pilot thrusting?" is a question about the
	// whole input rather than per-axis: any thrust at all suspends assist.
	if (Clean.Thrust.IsNearlyZero())
	{
		Result.Velocity = ApplyDamping(Result.Velocity, Config.LinearDamping, DeltaSeconds);
	}

	// Applied after damping, deliberately. Damping models the ship's own manoeuvring thrusters
	// holding it steady, and those have no business cancelling gravity — if they did, cutting the
	// engines over a planet would leave the ship hovering rather than falling.
	Result.Velocity += ExternalAcceleration * DeltaSeconds;

	Result.Velocity = ClampMagnitude(Result.Velocity, Config.MaxSpeed);

	return Result;
}

FVector FShipFlightModel::PositionDeltaKilometres(
	const FShipFlightState& State, const double DeltaSeconds)
{
	if (DeltaSeconds <= 0.0)
	{
		return FVector::ZeroVector;
	}

	return (State.Velocity * DeltaSeconds) / SpaceMMO::Coordinates::CentimetresPerKilometre;
}

FSystemCoordinate FShipFlightModel::ReconcilePosition(
	const FSystemCoordinate& Predicted,
	const FSystemCoordinate& Authoritative,
	const FShipReconciliation& Rules,
	const double DeltaSeconds)
{
	if (DeltaSeconds <= 0.0)
	{
		return Predicted;
	}

	const FVector Error = Authoritative.Kilometres - Predicted.Kilometres;

	// Snap once the disagreement is large enough that blending would have the ship visibly flying
	// a path neither side believes in.
	if (Error.Size() >= Rules.SnapThresholdKilometres)
	{
		return Authoritative;
	}

	if (Rules.BlendRatePerSecond <= 0.0)
	{
		return Predicted;
	}

	// Exponential: remove a fraction of what remains, so the result is frame-rate independent.
	const double Alpha = 1.0 - FMath::Exp(-Rules.BlendRatePerSecond * DeltaSeconds);

	return FSystemCoordinate(Predicted.Kilometres + (Error * Alpha));
}

FSystemCoordinate FShipFlightModel::Extrapolate(
	const FSystemCoordinate& LastKnown,
	const FVector& Velocity,
	const double SecondsSinceUpdate,
	const double MaxExtrapolationSeconds)
{
	if (SecondsSinceUpdate <= 0.0)
	{
		return LastKnown;
	}

	// Clamped, not extended indefinitely. A ship that stopped updating has probably stopped doing
	// what it was doing, and flying its last heading for a whole second puts it somewhere it never
	// was — which costs more on correction than the stutter would have.
	const double Seconds = FMath::Min(SecondsSinceUpdate, FMath::Max(0.0, MaxExtrapolationSeconds));

	const FVector DeltaKilometres =
		(Velocity * Seconds) / SpaceMMO::Coordinates::CentimetresPerKilometre;

	return FSystemCoordinate(LastKnown.Kilometres + DeltaKilometres);
}

FShipNavigation FShipFlightModel::Advance(
	const FShipNavigation& Navigation,
	const FShipFlightState& State,
	const double DeltaSeconds)
{
	FShipNavigation Result = Navigation;

	Result.SystemPosition = FSystemCoordinate(
		Navigation.SystemPosition.Kilometres + PositionDeltaKilometres(State, DeltaSeconds));

	if (Result.SystemPosition.IsWithinLocalSpaceOf(Result.RenderOrigin))
	{
		return Result;
	}

	// Rebase. The origin snaps to the ship rather than stepping toward it, so a single frame of
	// very high speed cannot leave the render location outside the budget — which a fixed-size
	// step could, and then the budget would be a suggestion rather than a guarantee.
	Result.RenderOrigin = Result.SystemPosition;
	++Result.RebaseCount;

	return Result;
}

FShipFlightState FShipFlightModel::ResolveBlockingHit(
	const FShipFlightState& State, const FVector& Normal)
{
	FShipFlightState Result = State;

	const FVector Surface = Normal.GetSafeNormal();

	if (Surface.IsNearlyZero())
	{
		// A hit with no normal names no direction, and inventing one would push a ship somewhere
		// arbitrary at flight speeds. Leaving the state alone lets the next frame try again.
		return Result;
	}

	Result.Velocity = SpaceMMO::Surfaces::SlideAlong(Result.Velocity, Surface);

	return Result;
}

FVector FShipFlightModel::SlideDeltaCentimetres(
	const FVector& RemainingDelta, const FVector& Normal)
{
	const FVector Surface = Normal.GetSafeNormal();

	if (Surface.IsNearlyZero())
	{
		// The opposite choice from ResolveBlockingHit, deliberately: a velocity can afford to wait
		// a frame, and a position is spent the moment it is applied.
		return FVector::ZeroVector;
	}

	return SpaceMMO::Surfaces::SlideAlong(RemainingDelta, Surface);
}
