#pragma once

#include "CoreMinimal.h"
#include "SpaceMMOCoordinates.h"
#include "SpaceMMOPlanet.h"

/**
 * Which body a position belongs to, when there is more than one.
 *
 * <strong>Written down because "the planet" used to mean "the first one the actor iterator
 * returned".</strong> The scene held exactly one planet from the day it was built until 8
 * September, so every lookup that took the first and broke was indistinguishable from one that
 * chose correctly — and there were about ten of them. Two carried comments claiming a
 * nearest-planet rule that did not exist anywhere in the project (task 157).
 *
 * A pure function over configurations rather than a loop over actors, so the rule can be tested
 * headless with no world, no spawning and no engine. The actor-level convenience is
 * <c>ASpaceMMOPlanetActor::NearestTo</c>, and it defers to this.
 */
struct SPACEMMOCORE_API FSpaceMMOBodySelection
{
	/**
	 * Index of the body whose <em>surface</em> is closest to a position, or INDEX_NONE for none.
	 *
	 * <strong>Surface, not centre.</strong> With every body drawn at one radius the two rules agree
	 * and the choice looks arbitrary; with bodies of different sizes they do not, and centre
	 * distance gives the wrong answer in the case that matters — standing on a small world beside a
	 * much larger one, where the large body's centre can be nearer than the ground underfoot. Every
	 * body is drawn at 20 km today, so this is deliberately the rule that keeps working when that
	 * stops being true rather than the one that happens to agree with it now.
	 *
	 * Ties go to the earlier index. Two bodies equidistant is a content mistake the validator
	 * refuses, not a case worth a rule.
	 */
	static int32 IndexOfNearest(
		TArrayView<const FPlanetConfig> Bodies, const FSystemCoordinate& Where)
	{
		int32 Nearest = INDEX_NONE;

		double NearestAltitude = TNumericLimits<double>::Max();

		for (int32 Index = 0; Index < Bodies.Num(); ++Index)
		{
			const double Altitude = AltitudeAbove(Bodies[Index], Where);

			if (Altitude < NearestAltitude)
			{
				NearestAltitude = Altitude;
				Nearest = Index;
			}
		}

		return Nearest;
	}

	/**
	 * Height of a position above a body's nominal sphere, in kilometres. Negative inside it.
	 *
	 * The nominal sphere rather than the terrain, on purpose: this answers "which body is this
	 * near", and asking the height function per body per frame to answer it would cost far more
	 * than the question is worth. Once a body has been chosen, the terrain is what decides
	 * everything else about the ground.
	 */
	static double AltitudeAbove(const FPlanetConfig& Body, const FSystemCoordinate& Where)
	{
		return (Where.Kilometres - Body.Centre.Kilometres).Size() - Body.RadiusKilometres;
	}
};
