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
	const double Altitude = AltitudeKilometres(Planet, Position);
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
