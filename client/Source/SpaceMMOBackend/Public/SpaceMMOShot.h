#pragma once

#include "CoreMinimal.h"
#include "SpaceMMOBackendTypes.h"

/**
 * Somebody a shot might hit, as the hit test sees them (task 194): a capsule standing on its feet.
 *
 * In system kilometres, because that is where everybody is on the dedicated server, which has no render origin
 * of its own (the gathering component's range check measures there for the same reason).
 */
struct SPACEMMOBACKEND_API FSpaceMMOShotTarget
{
	/** Whatever the caller wants back: an index into its own list. */
	int32 Index = INDEX_NONE;

	/** Their feet, which is what a character's system position is. */
	FVector FeetKilometres = FVector::ZeroVector;

	/** Which way is up for them: the ground's normal under their feet. */
	FVector Up = FVector::UpVector;

	/** Which way they face, along the ground. */
	FVector Forward = FVector::ForwardVector;

	double RadiusMetres = 0.34;

	double HeightMetres = 1.8;
};

/** Where a shot landed, if it landed on somebody. */
struct SPACEMMOBACKEND_API FSpaceMMOShotHit
{
	int32 Index = INDEX_NONE;

	/** From the muzzle to the hit, in metres. */
	double DistanceMetres = 0.0;

	FVector PointKilometres = FVector::ZeroVector;

	EBackendHitZone Zone = EBackendHitZone::Body;

	EBackendHitFacing Facing = EBackendHitFacing::Front;
};

/**
 * Which character a shot hits, where on them, and from which side (task 194, ADR-0017). Pure, and tested without a
 * world.
 *
 * <strong>Not a physics trace.</strong> Characters have no collision another trace would meet, and terrain is a
 * function rather than a mesh (ADR-0013), so the caller asks the world separately whether a building or the
 * ground is in the way, and shortens the shot to that. What this answers is the part with numbers in it.
 */
namespace SpaceMMO::Shot
{
	/** How far up a body each zone starts, as a fraction of its height: legs below 0.47, the head above 0.82. */
	constexpr double LegsTopFraction = 0.47;
	constexpr double HeadBottomFraction = 0.82;

	/** Where a shot is fired from: chest height (Joe chose the bolt from the chest, 16 October). */
	constexpr double MuzzleHeightFraction = 0.75;

	/**
	 * The nearest target a ray meets within range.
	 *
	 * @param FromKilometres Where the shot leaves, in system kilometres.
	 * @param Direction Which way it goes; need not be unit length.
	 * @param RangeMetres How far it reaches. Anything beyond is missed.
	 * @return False if it meets nobody.
	 */
	SPACEMMOBACKEND_API bool Resolve(
		const FVector& FromKilometres,
		const FVector& Direction,
		double RangeMetres,
		TArrayView<const FSpaceMMOShotTarget> Targets,
		FSpaceMMOShotHit& OutHit);

	/** Which zone a height on a body is, as a fraction of its height from the feet. */
	SPACEMMOBACKEND_API EBackendHitZone ZoneAt(double HeightFraction);

	/**
	 * Which side of a target a shot came from. A shot travelling the way they face hit them from behind; one
	 * travelling against it, from the front; anything within 45 degrees of across them, from the side.
	 */
	SPACEMMOBACKEND_API EBackendHitFacing FacingOf(
		const FVector& TargetForward, const FVector& TargetUp, const FVector& ShotDirection);

	/**
	 * Where along a ray it first meets a capsule, or a negative number if it never does. Metres in, metres out,
	 * with the ray starting at the origin.
	 */
	SPACEMMOBACKEND_API double RayMeetsCapsule(
		const FVector& Direction, const FVector& AxisBottom, const FVector& AxisTop, double Radius);
}
