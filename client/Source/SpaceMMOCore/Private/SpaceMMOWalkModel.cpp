#include "SpaceMMOWalkModel.h"

#include "SpaceMMOSurfaces.h"

namespace
{
	/** Exponential decay, so damping does not depend on frame rate. */
	FVector DecayTowardZero(const FVector& Value, const double RatePerSecond, const double DeltaSeconds)
	{
		if (RatePerSecond <= 0.0)
		{
			return Value;
		}

		return Value * FMath::Exp(-RatePerSecond * DeltaSeconds);
	}
}

FQuat FCharacterWalkModel::AlignToSurface(const FQuat& Current, const FVector& SurfaceNormal)
{
	const FVector Up = SurfaceNormal.GetSafeNormal();

	if (Up.IsNearlyZero())
	{
		return Current;
	}

	// Keep as much of the old heading as the new tangent plane allows.
	FVector Forward = Current.GetForwardVector();
	Forward -= Up * FVector::DotProduct(Forward, Up);

	// Walking over a sharp ridge can leave the old heading pointing straight along the new normal,
	// and the projection above then cancels it completely. Falling back to the old right vector
	// gives a heading that is guaranteed not to be parallel to up, because forward and right were
	// perpendicular before this started.
	if (Forward.IsNearlyZero())
	{
		Forward = Current.GetRightVector();
		Forward -= Up * FVector::DotProduct(Forward, Up);
	}

	// Both degenerate only if the source rotation was itself broken. Any axis will do at that
	// point; the alternative is returning a NaN quaternion and losing the character entirely.
	if (Forward.IsNearlyZero())
	{
		Forward = FMath::Abs(Up.X) < 0.9 ? FVector(1.0, 0.0, 0.0) : FVector(0.0, 1.0, 0.0);
		Forward -= Up * FVector::DotProduct(Forward, Up);
	}

	return FRotationMatrix::MakeFromZX(Up, Forward.GetSafeNormal()).ToQuat();
}

FWalkState FCharacterWalkModel::Step(
	const FWalkState& State,
	const FWalkInput& Input,
	const FWalkConfig& Config,
	const FVector& SurfaceNormal,
	const FVector& Gravity,
	const bool bOnGround,
	const double DeltaSeconds)
{
	if (DeltaSeconds <= 0.0)
	{
		return State;
	}

	const FWalkInput Clean = Input.Sanitised();
	const FVector Up = SurfaceNormal.GetSafeNormal();

	FWalkState Result = State;

	// Orientation first, so this frame's movement uses the heading the player will actually see.
	Result.Rotation = AlignToSurface(Result.Rotation, Up);

	if (!FMath::IsNearlyZero(Clean.Turn))
	{
		// Turning is about the surface normal, not about world Z. On the far side of a planet
		// those point opposite ways, and using world Z would invert the controls.
		const FQuat Yaw(Up, FMath::DegreesToRadians(Config.TurnRate * Clean.Turn * DeltaSeconds));

		Result.Rotation = (Yaw * Result.Rotation).GetNormalized();
	}

	const FVector Forward = Result.Rotation.GetForwardVector();
	const FVector Right = Result.Rotation.GetRightVector();

	// Split velocity into the part along the ground and the part into or away from it. They obey
	// completely different rules — one is under the player's control, the other is gravity's.
	const double IntoSurface = FVector::DotProduct(Result.Velocity, Up);
	FVector Along = Result.Velocity - (Up * IntoSurface);
	double Away = IntoSurface;

	// Sprint raises the speed being accelerated toward, and nothing else. Adding a push instead
	// would make tapping the key a shove, and multiplying the acceleration would make a sprinting
	// character turn sharper than a walking one, which reads as the controls changing under you.
	//
	// It applies in the air too, because the alternative is worse: a character who loses their run
	// the instant they leave the ground decelerates mid-jump, and a jump that travels less far the
	// faster you were going is the sort of thing that feels broken without being explicable. Air
	// control is already a quarter of ground acceleration, so this changes very little up there.
	const double TopSpeed = Clean.bSprint
		? Config.WalkSpeed * FMath::Max(1.0, Config.SprintMultiplier)
		: Config.WalkSpeed;

	const FVector Wanted =
		((Forward * Clean.Move.X) + (Right * Clean.Move.Y)).GetClampedToMaxSize(1.0)
		* TopSpeed;

	const double Acceleration = bOnGround
		? Config.GroundAcceleration
		: Config.GroundAcceleration * FMath::Clamp(Config.AirControl, 0.0, 1.0);

	if (!Wanted.IsNearlyZero())
	{
		// Accelerate toward the requested velocity rather than snapping to it, and never overshoot
		// it in a single step — at low frame rates a fixed increment would sail past the target and
		// oscillate.
		const FVector Difference = Wanted - Along;
		const double Step = Acceleration * DeltaSeconds;

		Along += Difference.Size() <= Step ? Difference : Difference.GetSafeNormal() * Step;
	}
	else if (bOnGround)
	{
		// Only on the ground. Air has nothing to brake against, and damping mid-jump would make a
		// character stop dead in mid-air.
		Along = DecayTowardZero(Along, Config.GroundDamping, DeltaSeconds);
	}

	if (bOnGround)
	{
		// Standing on something cancels any residual inward motion, so gravity does not accumulate
		// frame after frame into a downward speed the character can never shed.
		Away = FMath::Max(Away, 0.0);

		if (Clean.bJump)
		{
			Away = Config.JumpSpeed;
		}
	}
	else
	{
		Away += FVector::DotProduct(Gravity, Up) * DeltaSeconds;
	}

	Result.Velocity = Along + (Up * Away);

	// Gravity's sideways component still applies in the air — on a slope it is what makes a jump
	// arc rather than travel in a straight line.
	if (!bOnGround)
	{
		const FVector SidewaysGravity = Gravity - (Up * FVector::DotProduct(Gravity, Up));

		Result.Velocity += SidewaysGravity * DeltaSeconds;
	}

	return Result;
}

FVector FCharacterWalkModel::PositionDeltaKilometres(const FWalkState& State, const double DeltaSeconds)
{
	if (DeltaSeconds <= 0.0)
	{
		return FVector::ZeroVector;
	}

	return (State.Velocity * DeltaSeconds) / SpaceMMO::Coordinates::CentimetresPerKilometre;
}

double FCharacterWalkModel::GroundSpeed(const FWalkState& State, const FVector& SurfaceNormal)
{
	const FVector Up = SurfaceNormal.GetSafeNormal();

	if (Up.IsNearlyZero())
	{
		return State.Velocity.Size();
	}

	// The part of the velocity lying in the tangent plane, which is the only part that moves the
	// character across ground somebody is watching them walk on.
	const FVector Across = State.Velocity - (Up * FVector::DotProduct(State.Velocity, Up));

	return Across.Size();
}

double FCharacterWalkModel::VerticalSpeed(const FWalkState& State, const FVector& SurfaceNormal)
{
	const FVector Up = SurfaceNormal.GetSafeNormal();

	if (Up.IsNearlyZero())
	{
		return 0.0;
	}

	return FVector::DotProduct(State.Velocity, Up);
}

double FCharacterWalkModel::MoveDirectionDegrees(
	const FWalkState& State, const FVector& SurfaceNormal)
{
	const FVector Up = SurfaceNormal.GetSafeNormal();

	if (Up.IsNearlyZero())
	{
		return 0.0;
	}

	const FVector Across = State.Velocity - (Up * FVector::DotProduct(State.Velocity, Up));

	if (Across.IsNearlyZero())
	{
		return 0.0;
	}

	// The heading, flattened onto the same tangent plane the movement was. An orientation aligned
	// to the surface already has its forward in that plane, but one handed a normal from a cliff
	// face may not, and a forward with a component along the normal would tilt the whole frame.
	const FVector Forward =
		(State.Rotation.GetForwardVector()
			- (Up * FVector::DotProduct(State.Rotation.GetForwardVector(), Up))).GetSafeNormal();

	if (Forward.IsNearlyZero())
	{
		return 0.0;
	}

	// Right = Up x Forward, which is Unreal's handedness: X cross Y is Z, so Z cross X is Y.
	const FVector Right = FVector::CrossProduct(Up, Forward);

	return FMath::RadiansToDegrees(
		FMath::Atan2(
			FVector::DotProduct(Across, Right),
			FVector::DotProduct(Across, Forward)));
}

double FCharacterWalkModel::SeparationCentimetres(const double DepthCentimetres)
{
	return SpaceMMO::Surfaces::SeparationCentimetres(DepthCentimetres);
}

FWalkState FCharacterWalkModel::ResolveBlockingHit(
	const FWalkState& State, const FVector& Normal, const double DepthCentimetres)
{
	FWalkState Result = State;

	const FVector Surface = Normal.GetSafeNormal();

	if (Surface.IsNearlyZero())
	{
		// A hit with no normal names no direction to be pushed along, and inventing one would move
		// the character somewhere arbitrary. Leaving the state alone lets the next frame try again.
		return Result;
	}

	// Only motion into the surface is removed. Taking all of it would freeze a character the moment
	// they brushed a wall, and reversing it would make a wall a trampoline -- the same two failures
	// ground contact avoids the same way, and a ship hitting a hangar wall now avoids through the
	// same function.
	Result.Velocity = SpaceMMO::Surfaces::SlideAlong(Result.Velocity, Surface);

	return Result;
}

FVector FCharacterWalkModel::SlideDeltaCentimetres(
	const FVector& RemainingDelta, const FVector& Normal)
{
	const FVector Surface = Normal.GetSafeNormal();

	if (Surface.IsNearlyZero())
	{
		// Nothing is known about which way the surface faces, so no direction can be called safe.
		// Spending nothing costs one frame of motion; spending it in an unknown direction spends it
		// straight through whatever was hit. This is the opposite choice from ResolveBlockingHit
		// deliberately: that one is about velocity next frame, which can afford to wait, and this is
		// about a position now, which cannot be taken back.
		return FVector::ZeroVector;
	}

	return SpaceMMO::Surfaces::SlideAlong(RemainingDelta, Surface);
}

bool FCharacterWalkModel::StandsOn(
	const FVector& Normal,
	const FVector& Up,
	const double GapCentimetres,
	const double SeparationSpeedCentimetres,
	const bool bWasStanding)
{
	const FVector Surface = Normal.GetSafeNormal();
	const FVector Above = Up.GetSafeNormal();

	if (Surface.IsNearlyZero() || Above.IsNearlyZero())
	{
		return false;
	}

	// Too steep to be a floor. Without this a downward probe alongside a wall reports the wall,
	// and a character told to stand on it climbs the outside of the building.
	const double Cosine =
		FMath::Cos(FMath::DegreesToRadians(SteepestWalkableSlopeDegrees));

	if (FVector::DotProduct(Surface, Above) < Cosine)
	{
		return false;
	}

	// Climbing away deliberately. The same threshold and the same reason as ground contact: on a
	// curved surface a purely tangential velocity always has a small positive component along the
	// normal, so testing the sign alone reports every walking step as a departure.
	constexpr double MinimumSeparationSpeedCentimetres = 50.0;

	if (GapCentimetres > 0.0
		&& SeparationSpeedCentimetres > MinimumSeparationSpeedCentimetres)
	{
		return false;
	}

	// Wider to leave than to arrive, so a character walking down a shallow step stays attached
	// instead of going airborne for a frame at every one of them.
	const double Tolerance =
		bWasStanding ? FloorReleaseGapCentimetres : FloorCaptureGapCentimetres;

	return GapCentimetres <= Tolerance;
}
