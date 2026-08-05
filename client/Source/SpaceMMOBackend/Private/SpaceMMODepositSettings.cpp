#include "SpaceMMODepositSettings.h"

double FDepositPlacement::UniformScale(const FVector& LocalBoxExtent)
{
	const double Width = FMath::Max(LocalBoxExtent.X, LocalBoxExtent.Y) * 2.0;
	const double Height = LocalBoxExtent.Z * 2.0;

	// A degenerate mesh scales by one rather than by infinity. A zero extent means the bounds were
	// never built or the model is empty, and neither is worth turning into a division by zero that
	// takes the frame with it.
	if (Width <= UE_DOUBLE_SMALL_NUMBER || Height <= UE_DOUBLE_SMALL_NUMBER)
	{
		return 1.0;
	}

	// The smaller of the two fits: matching the larger would push the other dimension past its
	// target and produce a rock wider than the character standing next to it.
	return FMath::Min(TargetWidthCentimetres / Width, TargetHeightCentimetres / Height);
}

double FDepositPlacement::BaseLift(
	const FVector& LocalOrigin, const FVector& LocalBoxExtent, const double Scale)
{
	// Where the mesh's lowest point sits relative to its pivot. Zero for a model authored with its
	// pivot on the ground, negative for one centred on its own middle.
	const double LocalBottom = LocalOrigin.Z - LocalBoxExtent.Z;

	return -LocalBottom * Scale;
}
