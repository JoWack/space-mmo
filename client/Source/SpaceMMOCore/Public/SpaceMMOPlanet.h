#pragma once

#include "CoreMinimal.h"
#include "SpaceMMOCoordinates.h"
#include "SpaceMMOPlanet.generated.h"

/**
 * How close something is to a planet.
 *
 * Drives what the game does rather than what it draws — which physics apply, whether terrain
 * streams in, whether landing gear is allowed down.
 */
UENUM(BlueprintType)
enum class EPlanetProximity : uint8
{
	/** Far enough out that the planet is scenery. */
	Orbital,

	/** Inside the atmosphere: drag applies and terrain should be streaming. */
	Atmospheric,

	/** At the surface. */
	Surface,
};

/**
 * A planet's physical properties.
 *
 * Content, not code. Radii are in kilometres of system space, which is already at the 1:10
 * universe scale (ADR-0001) — an Earth-analog is 637.1 km here, not 6371.
 */
USTRUCT(BlueprintType)
struct SPACEMMOCORE_API FPlanetConfig
{
	GENERATED_BODY()

	/** Centre of the planet, in system space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Planet")
	FSystemCoordinate Centre;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Planet")
	double RadiusKilometres = 20.0;

	/** Acceleration at the surface, in centimetres per second squared. Earth is 981. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Planet")
	double SurfaceGravity = 981.0;

	/** Height of the atmosphere above the surface, in kilometres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Planet")
	double AtmosphereHeightKilometres = 12.0;

	/** Altitude below which something counts as being at the surface, in kilometres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Planet")
	double SurfaceBandKilometres = 0.2;

	/**
	 * How far past a boundary something must travel before the classification changes back.
	 *
	 * Without this a ship hovering exactly at the atmosphere edge would flip between states every
	 * frame, and anything keyed to the transition — terrain streaming, physics mode, audio — would
	 * thrash along with it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Planet")
	double ProximityHysteresisKilometres = 1.0;
};

/**
 * Spherical gravity and altitude.
 *
 * Pure functions, like the flight model, so the same maths runs on the server, on the client, and
 * in a test. Everything here is expressed relative to the planet's centre, which is what makes
 * "down" a direction that changes as you walk rather than a constant — the thing that makes a
 * curved surface work at all.
 */
class SPACEMMOCORE_API FPlanetPhysics
{
public:
	/** Distance from the planet's centre, in kilometres. */
	static double DistanceFromCentreKilometres(
		const FPlanetConfig& Planet, const FSystemCoordinate& Position);

	/**
	 * Height above the surface, in kilometres. Negative inside the planet.
	 */
	static double AltitudeKilometres(
		const FPlanetConfig& Planet, const FSystemCoordinate& Position);

	/**
	 * Local up: the outward surface normal at a position.
	 *
	 * On a sphere this is simply the direction from the centre, and it is what a character's
	 * orientation and a ship's landing alignment are built on. Returns +Z at the exact centre,
	 * where the direction is genuinely undefined.
	 */
	static FVector UpDirection(const FPlanetConfig& Planet, const FSystemCoordinate& Position);

	/**
	 * Gravitational acceleration at a position, in centimetres per second squared, in system-frame
	 * axes.
	 *
	 * Inverse-square above the surface, so orbits behave and altitude matters. Inside the planet it
	 * falls linearly to zero at the centre — physically what a uniform-density body does, and more
	 * importantly it avoids the singularity that inverse-square has at r = 0.
	 */
	static FVector GravityAcceleration(
		const FPlanetConfig& Planet, const FSystemCoordinate& Position);

	/**
	 * Classifies how close a position is, given what it was classified as last frame.
	 *
	 * The previous state is what makes hysteresis possible: boundaries sit slightly further out
	 * when leaving than when entering, so hovering on one does not flip the state every frame.
	 */
	static EPlanetProximity ClassifyProximity(
		const FPlanetConfig& Planet,
		const FSystemCoordinate& Position,
		EPlanetProximity Previous = EPlanetProximity::Orbital);

	/**
	 * Speed of a circular orbit at a given altitude, in centimetres per second.
	 *
	 * Useful for placing things in orbit and for telling a pilot what they need to hold. Derived
	 * from the same surface gravity, so it stays consistent with whatever the planet is configured
	 * to pull at.
	 */
	static double CircularOrbitSpeed(const FPlanetConfig& Planet, double AltitudeKilometres);
};
