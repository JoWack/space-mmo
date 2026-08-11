#include "SpaceMMOPlanet.h"

double FPlanetPhysics::DistanceFromCentreKilometres(
	const FPlanetConfig& Planet, const FSystemCoordinate& Position)
{
	return Position.DistanceTo(Planet.Centre);
}

double FPlanetPhysics::AltitudeKilometres(
	const FPlanetConfig& Planet, const FSystemCoordinate& Position)
{
	return DistanceFromCentreKilometres(Planet, Position) - Planet.RadiusKilometres;
}

FVector FPlanetPhysics::UpDirection(
	const FPlanetConfig& Planet, const FSystemCoordinate& Position)
{
	const FVector Outward = Position.Kilometres - Planet.Centre.Kilometres;

	// At the exact centre there is no meaningful outward direction. Returning a fixed axis keeps
	// callers from having to handle a zero vector at the one place it can occur.
	return Outward.IsNearlyZero() ? FVector::UpVector : Outward.GetSafeNormal();
}

FVector FPlanetPhysics::GravityAcceleration(
	const FPlanetConfig& Planet, const FSystemCoordinate& Position)
{
	if (Planet.RadiusKilometres <= 0.0 || Planet.SurfaceGravity <= 0.0)
	{
		return FVector::ZeroVector;
	}

	const double Distance = DistanceFromCentreKilometres(Planet, Position);
	const FVector Down = -UpDirection(Planet, Position);

	if (Distance <= UE_DOUBLE_SMALL_NUMBER)
	{
		// Dead centre: gravity from every direction cancels.
		return FVector::ZeroVector;
	}

	if (Distance >= Planet.RadiusKilometres)
	{
		// Inverse square above the surface, normalised so the magnitude is exactly SurfaceGravity
		// at r = R.
		const double Ratio = Planet.RadiusKilometres / Distance;

		return Down * (Planet.SurfaceGravity * Ratio * Ratio);
	}

	// Below the surface it falls off linearly to zero at the centre, which is what a body of
	// uniform density actually does — and, usefully, has no singularity at r = 0 for inverse
	// square to trip over.
	return Down * (Planet.SurfaceGravity * (Distance / Planet.RadiusKilometres));
}

EPlanetProximity FPlanetPhysics::ClassifyProximity(
	const FPlanetConfig& Planet,
	const FSystemCoordinate& Position,
	const EPlanetProximity Previous)
{
	return ClassifyProximityAtAltitude(Planet, AltitudeKilometres(Planet, Position), Previous);
}

EPlanetProximity FPlanetPhysics::ClassifyProximityAtAltitude(
	const FPlanetConfig& Planet,
	const double Altitude,
	const EPlanetProximity Previous)
{
	const double Hysteresis = FMath::Max(0.0, Planet.ProximityHysteresisKilometres);

	// Boundaries sit further out when leaving than when entering, so a position parked exactly on
	// one settles instead of oscillating.
	const double SurfaceCeiling = Previous == EPlanetProximity::Surface
		? Planet.SurfaceBandKilometres + Hysteresis
		: Planet.SurfaceBandKilometres;

	if (Altitude <= SurfaceCeiling)
	{
		return EPlanetProximity::Surface;
	}

	const double AtmosphereCeiling = Previous == EPlanetProximity::Orbital
		? Planet.AtmosphereHeightKilometres
		: Planet.AtmosphereHeightKilometres + Hysteresis;

	return Altitude <= AtmosphereCeiling
		? EPlanetProximity::Atmospheric
		: EPlanetProximity::Orbital;
}

double FPlanetPhysics::CircularOrbitSpeed(
	const FPlanetConfig& Planet, const double AltitudeKilometres)
{
	const double Radius = Planet.RadiusKilometres + AltitudeKilometres;

	if (Radius <= 0.0 || Planet.SurfaceGravity <= 0.0)
	{
		return 0.0;
	}

	// v = sqrt(g(r) * r), with g(r) the inverse-square value at that radius. Working in
	// centimetres throughout, so the radius converts from kilometres first.
	const double RadiusCentimetres = Radius * SpaceMMO::Coordinates::CentimetresPerKilometre;
	const double Ratio = Planet.RadiusKilometres / Radius;
	const double GravityHere = Planet.SurfaceGravity * Ratio * Ratio;

	return FMath::Sqrt(GravityHere * RadiusCentimetres);
}

double FPlanetPhysics::AtmosphericDensity(
	const FPlanetConfig& Planet, const double AltitudeKilometres)
{
	if (Planet.AtmosphereHeightKilometres <= 0.0)
	{
		return 0.0;
	}

	// Below the ground counts as sea level rather than as thicker air. A ship briefly inside the
	// terrain during a hard landing should not be handed a drag force that grows without limit.
	const double Height = FMath::Clamp(
		AltitudeKilometres / Planet.AtmosphereHeightKilometres, 0.0, 1.0);

	const double Thinning = 1.0 - Height;

	return Thinning * Thinning;
}

FVector FPlanetPhysics::AtmosphericDrag(
	const FPlanetConfig& Planet,
	const double AltitudeKilometres,
	const FVector& Velocity,
	const double ThrustAcceleration,
	const double TerminalSpeed)
{
	if (TerminalSpeed <= 0.0 || ThrustAcceleration <= 0.0)
	{
		return FVector::ZeroVector;
	}

	const double Density = AtmosphericDensity(Planet, AltitudeKilometres);

	if (Density <= 0.0)
	{
		return FVector::ZeroVector;
	}

	const double Speed = Velocity.Size();

	// Not merely an optimisation: GetSafeNormal on a zero vector returns zero, but dividing by
	// Speed below would not, and a stationary ship must feel no drag at all rather than a NaN.
	if (Speed <= UE_DOUBLE_SMALL_NUMBER)
	{
		return FVector::ZeroVector;
	}

	// Drag equals full thrust at TerminalSpeed when the air is at its thickest, which is what makes
	// that number mean what its name says. Quadratic, so half the terminal speed costs a quarter of
	// the thrust and the ship still accelerates freely at low speed.
	const double Ratio = Speed / TerminalSpeed;

	const double Magnitude = ThrustAcceleration * Density * Ratio * Ratio;

	return -(Velocity / Speed) * Magnitude;
}
