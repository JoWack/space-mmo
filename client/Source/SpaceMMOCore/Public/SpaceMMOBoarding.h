#pragma once

#include "CoreMinimal.h"
#include "SpaceMMOCoordinates.h"

/**
 * When a player may step out of a ship, and where they end up.
 *
 * Pure, because these are rules rather than presentation. <strong>The server decides whether a
 * boarding happens</strong>, and it has to reach that decision without a camera, a mesh or an
 * input device — a client that asks to disembark in deep space, or to board a ship a hundred
 * kilometres away, is asking rather than telling.
 */
class SPACEMMOCORE_API FBoarding
{
public:
	/**
	 * How close a character must be to a ship to climb into it, in kilometres.
	 *
	 * A hundred metres. Fifty was tight enough that a few seconds of walking put the ship out of
	 * range, and with nothing marking where it is, out of range means lost.
	 */
	static constexpr double DefaultBoardingRangeKilometres = 0.015;

	/** How far to one side a character appears when stepping out, in kilometres. */
	static constexpr double DefaultStepOutOffsetKilometres = 0.03;

	/**
	 * Whether a ship's occupant may step out.
	 *
	 * Only when landed. Opening the door in flight would drop a character into the sky with no way
	 * back, and in space it would be worse — there is no EVA yet, so the honest answer is no rather
	 * than a slow death nobody chose.
	 */
	static bool CanDisembark(bool bShipOnGround);

	/** Whether a character is close enough to a ship to board it. */
	static bool CanEmbark(
		const FSystemCoordinate& Character,
		const FSystemCoordinate& Ship,
		double RangeKilometres = DefaultBoardingRangeKilometres);

	/**
	 * Where a character appears when stepping out of a ship.
	 *
	 * Beside the hull rather than inside it, offset along the ship's own right-hand side and
	 * flattened into the surface's tangent plane. Placing the character at the ship's position
	 * would put it inside the hull, and offsetting along an axis that is not tangent to the ground
	 * would bury it or leave it hanging.
	 *
	 * <strong>The caller passes an offset measured off the hull it is stepping out of.</strong> The
	 * default below is a fallback for a ship with no mesh to measure; it used to be the only value,
	 * at thirty metres, chosen when a ship was an engine cone of no particular size and there was
	 * nothing near it. Beside a twelve-metre hull that reads as being teleported into a field, and
	 * it would have gone wrong again the next time a ship changed size.
	 */
	static FSystemCoordinate StepOutPosition(
		const FSystemCoordinate& Ship,
		const FVector& SurfaceNormal,
		const FVector& ShipRight,
		double OffsetKilometres = DefaultStepOutOffsetKilometres);

	/**
	 * Which way a character faces when it steps out of a ship.
	 *
	 * <strong>The ship's forward, not the ship's right.</strong> A character steps out sideways and
	 * then looks where the ship is pointing, which is the direction they flew in from and the
	 * direction everything they came to look at is. Facing along the step-out direction instead
	 * leaves somebody staring at their own hull.
	 *
	 * Flattened into the surface's tangent plane for the same reason StepOutPosition flattens its
	 * offset: a ship parked nose-up on a slope has a forward vector with a component along the
	 * normal, and a character built from it unmodified leans.
	 *
	 * @param SurfaceNormal Outward normal of the ground beneath. Defines up.
	 * @param ShipForward   The ship's own forward, +X in its local frame.
	 */
	static FQuat StepOutRotation(const FVector& SurfaceNormal, const FVector& ShipForward);
};
