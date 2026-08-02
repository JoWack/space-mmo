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
	static constexpr double DefaultBoardingRangeKilometres = 0.1;

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
	 */
	static FSystemCoordinate StepOutPosition(
		const FSystemCoordinate& Ship,
		const FVector& SurfaceNormal,
		const FVector& ShipRight,
		double OffsetKilometres = DefaultStepOutOffsetKilometres);
};
