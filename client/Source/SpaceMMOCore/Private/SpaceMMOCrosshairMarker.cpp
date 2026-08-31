#include "SpaceMMOCrosshairMarker.h"

bool FCrosshairMarker::ScreenOffset(
	const double ForwardComponent,
	const double RightComponent,
	const double UpComponent,
	const double FocalLengthPixels,
	const double MaxRadiusPixels,
	FVector2D& OutOffset)
{
	OutOffset = FVector2D::ZeroVector;

	if (FocalLengthPixels <= 0.0 || MaxRadiusPixels <= 0.0)
	{
		return false;
	}

	// Screen Y runs down and the camera's up runs up. That negation is the one sign in here that is
	// easy to get wrong and impossible to catch in a still frame: everything looks correct until the
	// ship climbs, and then the marker dives.
	const FVector2D Lateral(RightComponent, -UpComponent);

	// Far enough in front to divide by. The projection runs away to infinity well before a direction
	// is actually behind the camera, so the pinned case has to begin here rather than at zero.
	constexpr double InFront = 0.05;

	if (ForwardComponent > InFront)
	{
		const FVector2D Projected = (Lateral / ForwardComponent) * FocalLengthPixels;

		// Pinned rather than allowed off screen. A marker outside the viewport has stopped answering
		// the question it exists for, and one on the edge still says which way to turn.
		OutOffset = Projected.SizeSquared() > MaxRadiusPixels * MaxRadiusPixels
			? Projected.GetSafeNormal() * MaxRadiusPixels
			: Projected;

		return true;
	}

	// Behind, or so near the plane of the camera that the projection means nothing.
	//
	// Pinned to the side the direction actually lies on, so turning toward the marker brings it
	// round. Projecting it instead would put it on the opposite side -- a behind-camera point lands
	// mirrored -- and a pilot following that turns away from where they are going.
	if (Lateral.IsNearlyZero())
	{
		// Straight out of the back. There is no side to pin it to, and choosing one would send
		// somebody turning in a direction nothing picked.
		return false;
	}

	OutOffset = Lateral.GetSafeNormal() * MaxRadiusPixels;

	return true;
}
