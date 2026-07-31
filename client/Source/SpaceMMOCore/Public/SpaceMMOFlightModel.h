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

	/** Centimetres per second squared at full thrust. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Flight")
	double ThrustAcceleration = 2000.0;

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
};
