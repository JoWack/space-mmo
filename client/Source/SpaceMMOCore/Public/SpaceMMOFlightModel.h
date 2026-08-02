#pragma once

#include "CoreMinimal.h"
#include "SpaceMMOCoordinates.h"
#include "SpaceMMOFlightModel.generated.h"

/**
 * Pilot intent for one frame, each axis in -1..1.
 *
 * Intent, never outcome. The client sends this and the server integrates it, which is what keeps
 * flight server-authoritative without paying for full server-side rewind on every input.
 */
USTRUCT(BlueprintType)
struct SPACEMMOCORE_API FShipFlightInput
{
	GENERATED_BODY()

	/** Ship-local thrust: X forward, Y right, Z up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Flight")
	FVector Thrust = FVector::ZeroVector;

	/** Ship-local torque: X roll, Y pitch, Z yaw. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Flight")
	FVector Torque = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Flight")
	bool bBoost = false;

	/**
	 * Clamps every axis into range.
	 *
	 * Applied before integration because input arrives from a client, and a client that sends 100
	 * on an axis would otherwise fly a hundred times faster than anyone else.
	 */
	FShipFlightInput Sanitised() const
	{
		FShipFlightInput Result;
		Result.Thrust = Thrust.BoundToBox(FVector(-1.0), FVector(1.0));
		Result.Torque = Torque.BoundToBox(FVector(-1.0), FVector(1.0));
		Result.bBoost = bBoost;

		return Result;
	}
};

/**
 * A hull's handling characteristics. Content, not code — these come from the ship's definition.
 */
USTRUCT(BlueprintType)
struct SPACEMMOCORE_API FShipFlightConfig
{
	GENERATED_BODY()

	/**
	 * Centimetres per second squared at full thrust.
	 *
	 * Sized against MaxSpeed rather than picked in isolation. At 2,000 the ship needed a hundred
	 * seconds and a hundred kilometres just to reach its own top speed, so the top speed was
	 * decoration and every journey was a commute. At 20,000 it gets there in ten seconds, which
	 * makes both numbers mean something.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Flight")
	double ThrustAcceleration = 20000.0;

	/** Degrees per second squared at full torque. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Flight")
	double AngularAcceleration = 120.0;

	/**
	 * Fraction of velocity shed per second when not thrusting — "flight assist".
	 *
	 * Zero is Newtonian: cut the engines and you coast forever, which is physically honest and
	 * unforgiving to fly. Non-zero bleeds speed off so a released stick means slowing down.
	 * <strong>Whether this defaults on is a real design decision and is not settled</strong>; it
	 * shapes how flying feels more than any other number here.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Flight")
	double LinearDamping = 0.4;

	/** Fraction of angular velocity shed per second when not applying torque. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Flight")
	double AngularDamping = 3.0;

	/** Hard speed ceiling, in centimetres per second. 2 km/s by default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Flight")
	double MaxSpeed = 200000.0;

	/** Degrees per second ceiling, so a ship cannot spin itself into unrenderable rates. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Flight")
	double MaxAngularSpeed = 180.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Flight")
	double BoostMultiplier = 4.0;

	/**
	 * Fraction of sideways speed a landed ship sheds per second.
	 *
	 * Ground contact cancels motion <em>into</em> the surface but leaves motion along it, which is
	 * right for a landing and wrong for a parked ship: gravity's tangential component on any slope
	 * accelerates it downhill forever. It drifted about ten metres a minute, which is invisible
	 * while watching and quite enough to put a ship out of boarding range while its pilot is off
	 * walking around.
	 *
	 * High, because this is standing friction rather than air resistance — a ship on its landing
	 * gear should stay where it was put.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Flight")
	double GroundFriction = 6.0;
};

/**
 * A ship's motion at an instant.
 */
USTRUCT(BlueprintType)
struct SPACEMMOCORE_API FShipFlightState
{
	GENERATED_BODY()

	/** Velocity in system-frame axes, centimetres per second. */
	UPROPERTY(BlueprintReadWrite, Category = "SpaceMMO|Flight")
	FVector Velocity = FVector::ZeroVector;

	/** Angular velocity in ship-local axes, degrees per second: X roll, Y pitch, Z yaw. */
	UPROPERTY(BlueprintReadWrite, Category = "SpaceMMO|Flight")
	FVector AngularVelocity = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category = "SpaceMMO|Flight")
	FQuat Rotation = FQuat::Identity;

	/** Speed in centimetres per second. */
	double Speed() const { return Velocity.Size(); }

	/** Speed in kilometres per second, which is the unit that means anything in space. */
	double SpeedKilometresPerSecond() const
	{
		return Velocity.Size() / SpaceMMO::Coordinates::CentimetresPerKilometre;
	}
};

/**
 * Where a ship is, and the window currently used to render it.
 *
 * The ship's authoritative position is a system-space coordinate in kilometres. Unreal's world
 * location is derived from it relative to a movable render origin, so the engine transform is a
 * <em>view</em> of the position rather than the truth of it.
 */
USTRUCT(BlueprintType)
struct SPACEMMOCORE_API FShipNavigation
{
	GENERATED_BODY()

	/** The truth: where the ship is in system space. */
	UPROPERTY(BlueprintReadWrite, Category = "SpaceMMO|Flight")
	FSystemCoordinate SystemPosition;

	/** The system position that currently maps to Unreal's world origin. */
	UPROPERTY(BlueprintReadWrite, Category = "SpaceMMO|Flight")
	FSystemCoordinate RenderOrigin;

	/** How many times the origin has moved. Rebasing leaves no other trace when it works. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Flight")
	int32 RebaseCount = 0;

	/** Where to place the actor, in centimetres. */
	FVector RenderLocationCentimetres() const
	{
		return SystemPosition.ToLocalCentimetres(RenderOrigin);
	}
};

/**
 * How a client resolves disagreement with the server about where it is.
 *
 * Prediction always drifts: the client integrates ahead of the server on its own clock, and packet
 * loss, jitter and float divergence all pull the two apart. The question is never whether to
 * correct, only how visibly.
 */
USTRUCT(BlueprintType)
struct SPACEMMOCORE_API FShipReconciliation
{
	GENERATED_BODY()

	/**
	 * Error beyond which the client stops blending and simply snaps, in kilometres.
	 *
	 * A blend that has to cover a large error takes long enough that the ship is visibly flying
	 * the wrong path while it catches up. Past some distance an honest jump reads better than a
	 * prolonged lie, and it also bounds how far a client can be dragged by a bad prediction.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Flight")
	double SnapThresholdKilometres = 1.0;

	/**
	 * Fraction of the remaining error removed per second while blending.
	 *
	 * Exponential rather than linear so the correction does not depend on frame rate — the same
	 * reason damping is exponential. A linear catch-up overshoots at low frame rates and produces
	 * a shudder exactly when the connection is already struggling.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Flight")
	double BlendRatePerSecond = 5.0;
};

/**
 * Six-degree-of-freedom flight integration.
 *
 * Pure functions over plain state, deliberately knowing nothing about actors, components or
 * ticking. The same code therefore runs on the server, on the client for prediction, and in a
 * test — and there is no second implementation to drift out of agreement with the first.
 *
 * Velocity is held in the <em>system</em> frame while thrust is applied in the <em>ship's</em>
 * frame. That is what makes a ship keep drifting the way it was going while it turns to face
 * somewhere else, which is most of what makes space flight feel like space flight.
 */
class SPACEMMOCORE_API FShipFlightModel
{
public:
	/**
	 * Advances one step.
	 *
	 * @param DeltaSeconds           Frame time. Zero or negative returns the state untouched.
	 * @param ExternalAcceleration   Environmental forces in system-frame axes, centimetres per
	 *                               second squared — gravity today, drag and tractor beams later.
	 *
	 * External acceleration is a separate parameter rather than part of the input, because the
	 * input is <em>pilot intent</em> and this is the world acting on the ship. Keeping them apart
	 * is what lets flight assist damp the pilot's velocity without also cancelling gravity, which
	 * would leave ships hovering over planets with the engines off.
	 */
	static FShipFlightState Step(
		const FShipFlightState& State,
		const FShipFlightInput& Input,
		const FShipFlightConfig& Config,
		double DeltaSeconds,
		const FVector& ExternalAcceleration = FVector::ZeroVector);

	/**
	 * How far a ship travels this step, in kilometres, ready to add to a grid's system origin.
	 *
	 * Kilometres because that is what system space is in. Doing the conversion here keeps the
	 * centimetre-to-kilometre factor in one place rather than scattered across every caller that
	 * moves something.
	 */
	static FVector PositionDeltaKilometres(const FShipFlightState& State, double DeltaSeconds);

	/**
	 * Thrust direction in system-frame axes, for a given rotation and input.
	 *
	 * Exposed because it is the piece most worth checking on its own: if this is wrong, a ship
	 * accelerates somewhere other than where it is pointing.
	 */
	static FVector ThrustDirection(const FQuat& Rotation, const FVector& LocalThrust);

	/**
	 * Moves a ship through system space and rebases the render origin when it drifts too far.
	 *
	 * The guarantee: after this returns, the render location is always inside the physics budget,
	 * however far the ship has actually travelled. That is what keeps Chaos in the range it
	 * behaves well in over a flight of any length.
	 *
	 * Rebasing is meant to be invisible. The system position is untouched and only the window onto
	 * it moves, so nothing on screen jumps — which is also why the count exists, since a rebase
	 * that works leaves nothing else to observe.
	 */
	static FShipNavigation Advance(
		const FShipNavigation& Navigation,
		const FShipFlightState& State,
		double DeltaSeconds);

	/**
	 * Moves a predicted position toward the server's, either by blending or by snapping.
	 *
	 * <strong>Position is reconciled in system space, never in Unreal world space.</strong> Every
	 * client rebases its own render origin independently, so the same system coordinate maps to a
	 * different world location on each of them — comparing world transforms across the wire would
	 * be comparing two numbers that were never in the same frame of reference.
	 *
	 * @param DeltaSeconds Frame time. Zero or negative returns the prediction untouched, so a
	 *                     paused or hitching client is not silently dragged.
	 */
	static FSystemCoordinate ReconcilePosition(
		const FSystemCoordinate& Predicted,
		const FSystemCoordinate& Authoritative,
		const FShipReconciliation& Rules,
		double DeltaSeconds);

	/**
	 * Where a ship should be drawn now, given the last state the server sent and how long ago.
	 *
	 * Used for <em>other</em> players' ships. Replication arrives at a fraction of the frame rate,
	 * so holding the last received position would make every remote ship visibly stutter. Carrying
	 * it forward along its known velocity is what makes other traffic look like it is flying
	 * rather than teleporting.
	 *
	 * Extrapolation is capped: past a point, continuing to fly a stale heading takes a ship
	 * further from the truth than simply waiting would, and the correction becomes worse than the
	 * stutter it was hiding.
	 */
	static FSystemCoordinate Extrapolate(
		const FSystemCoordinate& LastKnown,
		const FVector& Velocity,
		double SecondsSinceUpdate,
		double MaxExtrapolationSeconds = 0.5);
};
