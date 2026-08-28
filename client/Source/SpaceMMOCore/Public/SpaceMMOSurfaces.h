#pragma once

#include "CoreMinimal.h"

/**
 * What meeting a solid surface does to a motion.
 *
 * <strong>One arithmetic, three things that move.</strong> A walking character, a flying ship and
 * ground contact all answer the same question when they run into something — how much of this
 * motion survives — and they had begun to answer it in three places. The rule is fully inelastic
 * and tangential-preserving: the part heading into the surface is removed rather than reflected, so
 * a thing stops against a wall instead of bouncing off it and keeps whatever was running along the
 * wall so it can slide.
 *
 * Deliberately *not* including what to do when the normal is degenerate, because the callers
 * genuinely differ: a velocity can be left alone and retried next frame, while a position is spent
 * the moment it is applied and must not be spent in a direction nobody measured.
 */
namespace SpaceMMO::Surfaces
{
	/**
	 * The part of a motion that survives meeting a surface.
	 *
	 * @param Motion  A velocity or a displacement; the arithmetic does not care which.
	 * @param Surface Outward unit normal of what was hit, pointing back at the thing that hit it.
	 *                Callers normalise and rule out the degenerate case first.
	 */
	SPACEMMOCORE_API FVector SlideAlong(const FVector& Motion, const FVector& Surface);

	/**
	 * How far along the normal to place something so it is clear of what it hit, in centimetres.
	 *
	 * <strong>A hair further than exactly clear.</strong> Resolving to exactly touching leaves the
	 * next query starting inside the surface by whatever floating point does, and a thing that
	 * alternates between clear and penetrating jitters against everything it touches.
	 */
	SPACEMMOCORE_API double SeparationCentimetres(double DepthCentimetres);
}
