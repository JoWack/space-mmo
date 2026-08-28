#include "SpaceMMOSurfaces.h"

FVector SpaceMMO::Surfaces::SlideAlong(const FVector& Motion, const FVector& Surface)
{
	const double Into = FVector::DotProduct(Motion, Surface);

	if (Into >= 0.0)
	{
		// Already heading away from the surface. Nothing to take.
		return Motion;
	}

	return Motion - Surface * Into;
}

double SpaceMMO::Surfaces::SeparationCentimetres(const double DepthCentimetres)
{
	// A millimetre past clear. Small enough that nobody sees it, large enough that the next query
	// starts outside the surface rather than inside it by a rounding error.
	constexpr double SkinCentimetres = 0.1;

	return FMath::Max(0.0, DepthCentimetres) + SkinCentimetres;
}
