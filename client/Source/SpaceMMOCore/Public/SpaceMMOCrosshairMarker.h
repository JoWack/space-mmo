#pragma once

#include "CoreMinimal.h"

/**
 * Where a direction in the world lands on screen, relative to the middle of it.
 *
 * <strong>For the marker that says where a ship is actually going.</strong> This flight model has
 * real inertia, so pointing one way and travelling another is ordinary rather than exceptional, and
 * nothing on screen said so. A static reticle answers "where is the nose"; this answers "where will
 * I end up", and when the two coincide the ship is flying straight.
 *
 * Pure, and handed a direction already in the camera's frame rather than a world one, so the awkward
 * case can be pinned down without a viewport: a direction behind the camera projects to a point in
 * front of it and lands on exactly the wrong side of the screen. That is a sign error which looks
 * plausible in every still frame and is obvious only while turning.
 */
struct SPACEMMOCORE_API FCrosshairMarker
{
	/**
	 * Offset from the centre of the screen, in pixels.
	 *
	 * @param ForwardComponent  How much of the direction lies along the camera's forward.
	 * @param RightComponent    ...along its right.
	 * @param UpComponent       ...along its up.
	 * @param FocalLengthPixels Half the viewport width over the tangent of half the horizontal field
	 *                          of view: the pixels-per-radian of the projection.
	 * @param MaxRadiusPixels   How far from the centre the marker may be pinned when it cannot be
	 *                          drawn where it belongs.
	 * @param OutOffset         X right, Y down, which is the way screen space runs.
	 *
	 * @return False when there is nowhere honest to put it. A direction exactly behind the camera
	 *         names no side of the screen, and inventing one turns a pilot the wrong way; the caller
	 *         draws nothing.
	 */
	static bool ScreenOffset(
		double ForwardComponent,
		double RightComponent,
		double UpComponent,
		double FocalLengthPixels,
		double MaxRadiusPixels,
		FVector2D& OutOffset);
};
