#include "SpaceMMOBoarding.h"

bool FBoarding::CanDisembark(const bool bShipOnGround)
{
	return bShipOnGround;
}

bool FBoarding::CanEmbark(
	const FSystemCoordinate& Character,
	const FSystemCoordinate& Ship,
	const double RangeKilometres)
{
	return (Character.Kilometres - Ship.Kilometres).Size()
		<= FMath::Max(0.0, RangeKilometres);
}

FSystemCoordinate FBoarding::StepOutPosition(
	const FSystemCoordinate& Ship,
	const FVector& SurfaceNormal,
	const FVector& ShipRight,
	const double OffsetKilometres)
{
	const FVector Up = SurfaceNormal.GetSafeNormal();

	if (Up.IsNearlyZero())
	{
		return Ship;
	}

	// Flattened into the tangent plane, so the character steps sideways along the ground rather
	// than into it or off it. A ship parked nose-down on a slope has a right vector with a
	// component along the normal, and using it unmodified would bury the character.
	FVector Sideways = ShipRight - (Up * FVector::DotProduct(ShipRight, Up));

	// A ship rolled so that its right vector points straight up leaves nothing to flatten. Any
	// tangent direction will do at that point, and having one is what matters.
	if (Sideways.IsNearlyZero())
	{
		const FVector Reference =
			FMath::Abs(Up.Z) < 0.9 ? FVector(0.0, 0.0, 1.0) : FVector(1.0, 0.0, 0.0);

		Sideways = FVector::CrossProduct(Reference, Up);
	}

	// Lifted slightly as well as offset, so the character starts just above the ground and settles
	// onto it rather than starting inside a hill and being pushed out on the first frame.
	const FVector Offset =
		(Sideways.GetSafeNormal() * FMath::Max(0.0, OffsetKilometres)) + (Up * 0.002);

	return FSystemCoordinate(Ship.Kilometres + Offset);
}
