#pragma once

#include "CoreMinimal.h"
#include "SpaceMMOCoordinates.h"
#include "SpaceMMOWalkModel.generated.h"

/**
 * What a person on foot is trying to do this frame.
 */
USTRUCT(BlueprintType)
struct SPACEMMOCORE_API FWalkInput
{
	GENERATED_BODY()

	/** Movement intent in the character's own tangent plane: X forward, Y right, each -1..1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Walk")
	FVector2D Move = FVector2D::ZeroVector;

	/** Turn rate about the surface normal, in -1..1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Walk")
	double Turn = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Walk")
	bool bJump = false;

	/** Clamps every axis into range, for the same reason ship input is clamped: clients lie. */
	FWalkInput Sanitised() const
	{
		FWalkInput Result;
		Result.Move = FVector2D(
			FMath::Clamp(Move.X, -1.0, 1.0), FMath::Clamp(Move.Y, -1.0, 1.0));
		Result.Turn = FMath::Clamp(Turn, -1.0, 1.0);
		Result.bJump = bJump;

		return Result;
	}
};

/**
 * How a character moves on foot. Content, not code.
 */
USTRUCT(BlueprintType)
struct SPACEMMOCORE_API FWalkConfig
{
	GENERATED_BODY()

	/** Top speed on level ground, centimetres per second. 600 is a brisk walk. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Walk")
	double WalkSpeed = 600.0;

	/** How quickly the character reaches that speed, centimetres per second squared. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Walk")
	double GroundAcceleration = 3000.0;

	/** Fraction of horizontal speed shed per second when not being driven. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Walk")
	double GroundDamping = 8.0;

	/**
	 * How much of ground acceleration is available in mid-air, 0..1.
	 *
	 * Not zero. Realistically a jumping person cannot change direction at all, but a character who
	 * cannot be steered mid-jump feels broken to play rather than realistic.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Walk")
	double AirControl = 0.25;

	/** Speed imparted straight up on a jump, centimetres per second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Walk")
	double JumpSpeed = 420.0;

	/** Degrees per second the character turns at full input. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Walk")
	double TurnRate = 180.0;
};

/**
 * A character's motion, in the same system frame everything else uses.
 */
USTRUCT(BlueprintType)
struct SPACEMMOCORE_API FWalkState
{
	GENERATED_BODY()

	/** Velocity in system-frame axes, centimetres per second. */
	UPROPERTY(BlueprintReadWrite, Category = "SpaceMMO|Walk")
	FVector Velocity = FVector::ZeroVector;

	/** Orientation, with Z aligned to whatever counts as up where the character is standing. */
	UPROPERTY(BlueprintReadWrite, Category = "SpaceMMO|Walk")
	FQuat Rotation = FQuat::Identity;
};

/**
 * Walking on a sphere.
 *
 * <strong>Up is the surface normal, not the world Z axis.</strong> That single substitution is
 * what makes a planet walkable: keep going and the ground tilts under you until you are standing
 * on what used to be the underside of the world, and nothing in here ever notices, because
 * nothing in here has an opinion about which way is up beyond what it is handed.
 *
 * Pure, like the flight model and for the same reasons: the server integrates it, the client
 * predicts with it, and a test drives it a full lap around a planet without a world existing.
 */
class SPACEMMOCORE_API FCharacterWalkModel
{
public:
	/**
	 * Advances one step.
	 *
	 * @param SurfaceNormal Outward normal of the ground beneath. Defines up for this frame.
	 * @param Gravity       Acceleration from every nearby body, centimetres per second squared.
	 * @param bOnGround     Whether the character was resting on the surface at the start of this
	 *                      step. Grounded and airborne movement are genuinely different.
	 */
	static FWalkState Step(
		const FWalkState& State,
		const FWalkInput& Input,
		const FWalkConfig& Config,
		const FVector& SurfaceNormal,
		const FVector& Gravity,
		bool bOnGround,
		double DeltaSeconds);

	/**
	 * Rebuilds an orientation so its up axis matches the surface, keeping the heading.
	 *
	 * The awkward case is a heading that has drifted parallel to the new normal — walking over a
	 * sharp ridge, or being handed a normal from a cliff face. Projecting the old forward onto the
	 * new tangent plane then leaves nothing to normalise, and a naive implementation produces a
	 * NaN rotation that propagates into the transform and makes the character vanish.
	 */
	static FQuat AlignToSurface(const FQuat& Current, const FVector& SurfaceNormal);

	/** How far the character moves this step, in kilometres, ready to add to a system position. */
	static FVector PositionDeltaKilometres(const FWalkState& State, double DeltaSeconds);

	/**
	 * Speed across the ground, ignoring any rise or fall. Centimetres per second.
	 *
	 * <strong>What a walk cycle should be played against, and not the same as the speed of the
	 * velocity vector.</strong> A character stepping off a ledge is travelling fast downward while
	 * moving nowhere across the ground; blending a run animation on total speed would have them
	 * sprinting in mid-air, faster the further they fall.
	 */
	static double GroundSpeed(const FWalkState& State, const FVector& SurfaceNormal);

	/**
	 * Speed along the surface normal: positive rising, negative falling. Centimetres per second.
	 *
	 * The sign is the whole point — it is what tells a jump from a fall, and they are different
	 * animations.
	 */
	static double VerticalSpeed(const FWalkState& State, const FVector& SurfaceNormal);

	/**
	 * Which way the character is travelling relative to the way it is facing, in degrees.
	 *
	 * Zero straight ahead, +90 to the right, -90 to the left, ±180 backwards — the convention a
	 * directional blend space expects, so strafing plays a sidestep rather than a forward run
	 * performed sideways.
	 *
	 * <strong>Measured in the character's own frame, against the surface normal.</strong> Doing it
	 * against world axes would be right at one point on a planet and wrong everywhere else, which
	 * is the mistake this codebase has made in one form or another several times: up is the ground's
	 * normal, never Z.
	 *
	 * Zero when standing still, which is what a blend space wants when the speed weight is zero
	 * anyway.
	 */
	static double MoveDirectionDegrees(const FWalkState& State, const FVector& SurfaceNormal);
};
