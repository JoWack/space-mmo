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

const TArray<double>& FCharacterWalkModel::RollProfile()
{
	// Frames 3 to 62 of anim_Dodge_Roll's root, less frame 3's: as the importer logged it.
	static const TArray<double> Profile = []
	{
		const double Captured[] = {
			0.0, 0.2, 1.4, 5.1, 11.4, 18.4, 24.5, 28.5, 30.9, 32.6, 34.7, 38.0, 43.3, 50.0, 57.3, 64.6, 71.1, 76.8, 81.9,
			86.3, 90.4, 95.3, 102.3, 112.9, 126.8, 138.5, 146.7, 154.7, 162.3, 169.8, 177.2, 184.4, 191.4, 197.9, 203.9,
			209.3, 214.1, 217.9, 220.9, 223.2, 224.9, 226.2, 227.3, 228.5, 229.8, 231.4, 233.2, 235.4, 238.0, 241.1,
			244.7, 248.6, 252.4, 255.8, 258.5, 260.4, 262.3, 264.9, 269.3, 275.9, 283.0, 288.8, 291.2};

		TArray<double> Out;

		for (int32 Frame = RollStartFrame; Frame < UE_ARRAY_COUNT(Captured); ++Frame)
		{
			Out.Add(Captured[Frame] - Captured[RollStartFrame]);
		}

		return Out;
	}();

	return Profile;
}

double FCharacterWalkModel::DodgeDuration(const EWalkDodge Dodge, const FWalkConfig& Config)
{
	switch (Dodge)
	{
	case EWalkDodge::Roll:
		return (RollProfile().Num() - 1) / RollCaptureRate / FMath::Max(Config.RollPlayRate, 0.01);

	case EWalkDodge::Hop:
		return Config.HopSeconds;

	default:
		return 0.0;
	}
}

double FCharacterWalkModel::DodgeDistance(const EWalkDodge Dodge, const double Seconds, const FWalkConfig& Config)
{
	const double Duration = DodgeDuration(Dodge, Config);

	if (Duration <= 0.0)
	{
		return 0.0;
	}

	const double T = FMath::Clamp(Seconds, 0.0, Duration);

	if (Dodge == EWalkDodge::Hop)
	{
		// Eased in and out: a push off, the flight, a landing.
		const double U = T / Duration;

		return Config.HopDistanceCentimetres * U * U * (3.0 - 2.0 * U);
	}

	const TArray<double>& Profile = RollProfile();
	const double Frame = T * RollCaptureRate * Config.RollPlayRate;
	const int32 Below = FMath::Clamp(FMath::FloorToInt32(Frame), 0, Profile.Num() - 1);
	const int32 Above = FMath::Min(Below + 1, Profile.Num() - 1);

	return FMath::Lerp(Profile[Below], Profile[Above], Frame - Below);
}

bool FCharacterWalkModel::IsUntouchable(const FWalkState& State, const FWalkConfig& Config)
{
	return State.Dodge != EWalkDodge::None && State.DodgeSeconds < Config.DodgeUntouchableSeconds;
}

bool FCharacterWalkModel::IsSprinting(const FWalkState& State, const FWalkInput& Input)
{
	return Input.bSprint && !State.bSprintSpent && State.Stamina > 0.0 && State.Dodge == EWalkDodge::None
		&& !Input.Move.IsNearlyZero();
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

	// Stamina and the dodge (task 195, ADR-0017). Simulated here, where the server and the owning client both step,
	// so they agree on when sprint runs out and when a roll began -- the second is what decides a hit.
	Result.Stamina = FMath::Clamp(Result.Stamina, 0.0, Config.StaminaMax);

	bool bSpent = false;

	if (Result.Dodge != EWalkDodge::None)
	{
		Result.DodgeSeconds += DeltaSeconds;

		if (Result.DodgeSeconds >= DodgeDuration(Result.Dodge, Config))
		{
			Result.Dodge = EWalkDodge::None;
			Result.DodgeSeconds = 0.0;
		}
	}

	if (Clean.DodgePresses != Result.DodgePressesSeen)
	{
		Result.DodgePressesSeen = Clean.DodgePresses;

		if (Result.Dodge == EWalkDodge::None && bOnGround)
		{
			if (Result.Stamina >= Config.DodgeStaminaCost)
			{
				const FVector Moving = (Forward * Clean.Move.X) + (Right * Clean.Move.Y);

				// The way they were going, or straight back standing still.
				Result.Dodge = Moving.IsNearlyZero() ? EWalkDodge::Hop : EWalkDodge::Roll;
				Result.DodgeDirection = Moving.IsNearlyZero() ? -Forward : Moving.GetSafeNormal();
				Result.DodgeSeconds = 0.0;
				Result.Stamina -= Config.DodgeStaminaCost;
				++Result.DodgesStarted;
				bSpent = true;
			}
			else
			{
				++Result.DodgesRefused;
			}
		}
	}

	// Sprint at zero stays a run until the key is let go: otherwise the trickle of refill would flick it on and off.
	if (!Clean.bSprint)
	{
		Result.bSprintSpent = false;
	}

	const bool bSprinting = IsSprinting(Result, Clean);

	if (bSprinting)
	{
		Result.Stamina = FMath::Max(Result.Stamina - Config.SprintStaminaPerSecond * DeltaSeconds, 0.0);
		Result.bSprintSpent = Result.Stamina <= 0.0;
		bSpent = true;
	}

	Result.SinceSpentSeconds = bSpent ? 0.0 : Result.SinceSpentSeconds + DeltaSeconds;

	if (Result.SinceSpentSeconds >= Config.StaminaRegenDelaySeconds)
	{
		Result.Stamina = FMath::Min(Result.Stamina + Config.StaminaRegenPerSecond * DeltaSeconds, Config.StaminaMax);
	}

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
	const double TopSpeed = Config.TopSpeed(bSprinting);

	const FVector Wanted =
		((Forward * Clean.Move.X) + (Right * Clean.Move.Y)).GetClampedToMaxSize(1.0)
		* TopSpeed;

	const double Acceleration = bOnGround
		? Config.GroundAcceleration
		: Config.GroundAcceleration * FMath::Clamp(Config.AirControl, 0.0, 1.0);

	if (Result.Dodge != EWalkDodge::None)
	{
		// The dodge carries them, at its own clip's speed through it, and nothing the player presses steers it. Laid
		// along today's ground, which may have turned since it began.
		const double Before = DodgeDistance(Result.Dodge, Result.DodgeSeconds - DeltaSeconds, Config);
		const double After = DodgeDistance(Result.Dodge, Result.DodgeSeconds, Config);
		const FVector Way = (Result.DodgeDirection - Up * FVector::DotProduct(Result.DodgeDirection, Up)).GetSafeNormal();

		Along = Way * ((After - Before) / DeltaSeconds);
	}
	else if (!Wanted.IsNearlyZero())
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

		if (Clean.bJump && Result.Dodge == EWalkDodge::None)
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

bool FCharacterWalkModel::WayOutIsUp(const FVector& WayOut, const FVector& Up)
{
	const FVector Out = WayOut.GetSafeNormal();
	const FVector Above = Up.GetSafeNormal();

	if (Out.IsNearlyZero() || Above.IsNearlyZero())
	{
		return false;
	}

	// The same limit a floor is held to: anything shallower than this is a wall's side.
	return FVector::DotProduct(Out, Above)
		>= FMath::Cos(FMath::DegreesToRadians(SteepestWalkableSlopeDegrees));
}

bool FCharacterWalkModel::ClimbsOutOnto(
	const FVector& WayOut,
	const FVector& FloorNormal,
	const FVector& Up,
	const double FloorAboveFeetCentimetres)
{
	return WayOutIsUp(WayOut, Up)
		&& WayOutIsUp(FloorNormal, Up)
		&& FloorAboveFeetCentimetres > 0.0
		&& FloorAboveFeetCentimetres <= DeepestClimbOutCentimetres;
}

FWalkInput FCharacterWalkModel::ForServer(
	const FWalkInput& Input, const FWalkState& State, const bool bFacingIsOurs, const uint8 FacingEpoch)
{
	FWalkInput Sent = Input;

	Sent.Turn = 0.0;
	Sent.Facing = State.Rotation;
	Sent.bHasFacing = bFacingIsOurs;
	Sent.FacingEpoch = FacingEpoch;

	return Sent;
}

void FCharacterWalkModel::AdoptClientFacing(
	FWalkState& State, FWalkInput& Input, const uint8 ServerFacingEpoch)
{
	// A facing turned from one the server has since replaced is the old way round, not the player's
	// choice: taking it would undo the server's turn before the client had seen it (task 145).
	if (!Input.bHasFacing || Input.FacingEpoch != ServerFacingEpoch)
	{
		return;
	}

	// Taken whole; Step aligns it to the ground the server is standing the character on, which is
	// the same function the client aligned it to.
	State.Rotation = Input.Facing;
	Input.Turn = 0.0;
}

FQuat FCharacterWalkModel::FacingToward(const FVector& Up, const FVector& Toward, const FQuat& Fallback)
{
	const FVector Normal = Up.GetSafeNormal();

	if (Normal.IsNearlyZero())
	{
		return Fallback;
	}

	const FVector Along = Toward - (Normal * FVector::DotProduct(Toward, Normal));

	if (Along.IsNearlyZero())
	{
		return Fallback;
	}

	// X forward and Z up, the frame AlignToSurface builds, so the two agree about which way is ahead.
	return FRotationMatrix::MakeFromZX(Normal, Along.GetSafeNormal()).ToQuat();
}
