#include "SpaceMMOFlightModel.h"

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
	const double DeltaSeconds)
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
