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

	// Lifted half a metre, not two. Enough that the character settles onto the ground rather than
	// starting inside a hill, small enough that stepping out is a step rather than a drop.
	const FVector Offset =
		(Sideways.GetSafeNormal() * FMath::Max(0.0, OffsetKilometres)) + (Up * 0.0005);

	return FSystemCoordinate(Ship.Kilometres + Offset);
}
