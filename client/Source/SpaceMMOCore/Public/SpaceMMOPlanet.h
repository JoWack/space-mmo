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
	 * The same classification, against an altitude the caller has already decided how to measure.
	 *
	 * <strong>"At the surface" has to mean height above the ground, not above the sphere.</strong>
	 * A planet with half a kilometre of relief puts a standing player half a kilometre above the
	 * nominal radius, which the surface band — two hundred metres — reads as flying. Anyone with
	 * terrain to hand should measure with
	 * <see cref="FPlanetTerrain::AltitudeAboveGroundKilometres"/> and pass it here; the overload
	 * above is for callers that have only a sphere, and it is the one that called a landed ship
	 * airborne.
	 */
	static EPlanetProximity ClassifyProximityAtAltitude(
		const FPlanetConfig& Planet,
		double AltitudeKilometres,
		EPlanetProximity Previous = EPlanetProximity::Orbital);

	/**
	 * Speed of a circular orbit at a given altitude, in centimetres per second.
	 *
	 * Useful for placing things in orbit and for telling a pilot what they need to hold. Derived
	 * from the same surface gravity, so it stays consistent with whatever the planet is configured
	 * to pull at.
	 */
	static double CircularOrbitSpeed(const FPlanetConfig& Planet, double AltitudeKilometres);

	/**
	 * How thick the air is at an altitude, as a fraction of sea level. 1 at the ground, 0 at the
	 * top of the atmosphere and everywhere above it.
	 *
	 * Reaching exactly zero matters more than the shape of the curve. An exponential tail would
	 * leave a whisper of drag acting in orbit forever, which is both wrong and the kind of thing
	 * that is only ever noticed as an orbit mysteriously decaying weeks later. Squared, so the air
	 * thins quickly with height rather than ending abruptly at the boundary.
	 */
	static double AtmosphericDensity(const FPlanetConfig& Planet, double AltitudeKilometres);

	/**
	 * Drag acceleration on something moving through that air, in centimetres per second squared.
	 *
	 * <strong>Why this exists:</strong> orbital speed on a 20 km world is only about 443 m/s, and a
	 * ship makes 2,000. Without air resistance nothing stops a pilot skimming the ground at twice
	 * orbital velocity, at which point the ship is thrown off the surface by its own speed and
	 * cannot be flown along the ground at all, only skipped across it. A flight of exactly that was
	 * what prompted this (task 90).
	 *
	 * Quadratic in speed, so it is negligible when slow and firm when fast, and expressed through
	 * TerminalSpeed rather than a bare coefficient: drag exactly cancels ThrustAcceleration at that
	 * speed at sea level, which is the number worth choosing and the only one a designer should
	 * have to think about.
	 *
	 * @param ThrustAcceleration What full thrust is worth, so terminal speed means what it says.
	 * @param TerminalSpeed      Speed at which drag balances full thrust at sea level, cm/s.
	 */
	static FVector AtmosphericDrag(
		const FPlanetConfig& Planet,
		double AltitudeKilometres,
		const FVector& Velocity,
		double ThrustAcceleration,
		double TerminalSpeed);
};
