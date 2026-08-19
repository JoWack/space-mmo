#include "SpaceMMOPlanetTerrain.h"

namespace
{
	/** Smoothstep. Zero first derivative at both ends, so adjacent noise cells meet without a crease. */
	double Smooth(const double T)
	{
		return T * T * (3.0 - (2.0 * T));
	}

	/**
	 * Floor toward negative infinity, as an integer.
	 *
	 * FMath::FloorToInt64 is correct for negatives, which C-style truncation is not — and terrain
	 * is sampled on both sides of the origin on every axis, so getting this wrong puts a visible
	 * discontinuity through the middle of every planet.
	 */
	int64 FloorToInt64(const double Value)
	{
		return static_cast<int64>(FMath::FloorToDouble(Value));
	}
}

uint64 FPlanetTerrain::Mix(uint64 Value)
{
	// SplitMix64's finaliser, matching SpaceMMO.Domain's SplitMix64 exactly. Terrain does not
	// currently cross the language boundary, but generation is meant to be reproducible from a
	// seed on either side (ADR-0002), and using the same mixer keeps that option open at no cost.
	Value += 0x9E3779B97F4A7C15ULL;
	Value = (Value ^ (Value >> 30)) * 0xBF58476D1CE4E5B9ULL;
	Value = (Value ^ (Value >> 27)) * 0x94D049BB133111EBULL;

	return Value ^ (Value >> 31);
}

double FPlanetTerrain::LatticeValue(const int64 X, const int64 Y, const int64 Z, const uint64 Seed)
{
	// Each coordinate is folded in with a different odd multiplier so that (1,2,3) and (3,2,1) do
	// not collide — a symmetric hash produces terrain with visible diagonal mirror symmetry.
	uint64 Hash = Seed;
	Hash = Mix(Hash ^ (static_cast<uint64>(X) * 0x9E3779B97F4A7C15ULL));
	Hash = Mix(Hash ^ (static_cast<uint64>(Y) * 0xC2B2AE3D27D4EB4FULL));
	Hash = Mix(Hash ^ (static_cast<uint64>(Z) * 0x165667B19E3779F9ULL));

	// Top 53 bits, which is exactly what a double represents without loss.
	return static_cast<double>(Hash >> 11) / static_cast<double>(1ULL << 53);
}

double FPlanetTerrain::ValueNoise(const FVector& Point, const uint64 Seed)
{
	const int64 X0 = FloorToInt64(Point.X);
	const int64 Y0 = FloorToInt64(Point.Y);
	const int64 Z0 = FloorToInt64(Point.Z);

	const double Fx = Smooth(Point.X - static_cast<double>(X0));
	const double Fy = Smooth(Point.Y - static_cast<double>(Y0));
	const double Fz = Smooth(Point.Z - static_cast<double>(Z0));

	// Trilinear blend of the eight lattice corners surrounding the point.
	double Result = 0.0;

	for (int32 Corner = 0; Corner < 8; ++Corner)
	{
		const int64 Dx = Corner & 1;
		const int64 Dy = (Corner >> 1) & 1;
		const int64 Dz = (Corner >> 2) & 1;

		const double Weight =
			(Dx ? Fx : 1.0 - Fx) * (Dy ? Fy : 1.0 - Fy) * (Dz ? Fz : 1.0 - Fz);

		Result += Weight * LatticeValue(X0 + Dx, Y0 + Dy, Z0 + Dz, Seed);
	}

	return Result;
}

double FPlanetTerrain::FractalNoise(
	const FPlanetTerrainConfig& Terrain, const FVector& UnitDirection)
{
	const int32 Octaves = FMath::Clamp(Terrain.Octaves, 1, 12);

	double Frequency = FMath::Max(Terrain.BaseFrequency, UE_DOUBLE_SMALL_NUMBER);
	double Amplitude = 1.0;
	double Sum = 0.0;
	double TotalAmplitude = 0.0;

	for (int32 Octave = 0; Octave < Octaves; ++Octave)
	{
		// Each octave gets its own seed offset, so octaves are independent rather than the same
		// field at different scales — otherwise features line up and the terrain looks tiled.
		const uint64 OctaveSeed =
			Mix(static_cast<uint64>(Terrain.Seed) + static_cast<uint64>(Octave) * 0x51ED270B);

		Sum += Amplitude * ValueNoise(UnitDirection * Frequency, OctaveSeed);
		TotalAmplitude += Amplitude;

		Frequency *= FMath::Max(Terrain.Lacunarity, UE_DOUBLE_SMALL_NUMBER);
		Amplitude *= FMath::Clamp(Terrain.Gain, 0.0, 1.0);
	}

	// Normalised by the amplitude actually used, so changing the octave count alters the detail
	// without also changing the overall height of the planet.
	return TotalAmplitude > 0.0 ? Sum / TotalAmplitude : 0.0;
}

double FPlanetTerrain::ElevationKilometres(
	const FPlanetTerrainConfig& Terrain, const FVector& Direction)
{
	// A zero direction has no "up" to have terrain along. Returning the sea floor is the only
	// answer that cannot produce a NaN downstream.
	if (Direction.IsNearlyZero())
	{
		return 0.0;
	}

	const double Elevation =
		FractalNoise(Terrain, Direction.GetSafeNormal()) * Terrain.MaxElevationKilometres;

	return FMath::Clamp(Elevation, 0.0, Terrain.MaxElevationKilometres);
}

double FPlanetTerrain::SurfaceRadiusKilometres(
	const FPlanetConfig& Planet,
	const FPlanetTerrainConfig& Terrain,
	const FVector& Direction)
{
	return Planet.RadiusKilometres + ElevationKilometres(Terrain, Direction);
}

FSystemCoordinate FPlanetTerrain::SurfacePosition(
	const FPlanetConfig& Planet,
	const FPlanetTerrainConfig& Terrain,
	const FVector& Direction)
{
	// Normalised here rather than trusted from the caller. The API normalises on load and the
	// content validator rejects a zero vector, but this is also called with raw offsets from a
	// player's position, and a direction of any length other than one would scale the whole
	// surface radius — putting the result kilometres off the ground with nothing looking wrong.
	const FVector Unit = Direction.GetSafeNormal();

	if (Unit.IsNearlyZero())
	{
		// A zero direction names no point on the sphere. Returning the centre is the one answer
		// that is obviously wrong on sight, rather than a plausible-looking point on the equator.
		return Planet.Centre;
	}

	FSystemCoordinate Position;
	Position.Kilometres =
		Planet.Centre.Kilometres + (Unit * SurfaceRadiusKilometres(Planet, Terrain, Unit));

	return Position;
}

double FPlanetTerrain::AltitudeAboveGroundKilometres(
	const FPlanetConfig& Planet,
	const FPlanetTerrainConfig& Terrain,
	const FSystemCoordinate& Position)
{
	const FVector Offset = Position.Kilometres - Planet.Centre.Kilometres;

	return Offset.Size() - SurfaceRadiusKilometres(Planet, Terrain, Offset);
}

FVector FPlanetTerrain::SurfaceNormal(
	const FPlanetConfig& Planet,
	const FPlanetTerrainConfig& Terrain,
	const FVector& Direction,
	const double SampleAngleDegrees)
{
	const FVector Up = Direction.GetSafeNormal();

	if (Up.IsNearlyZero())
	{
		return FVector::UpVector;
	}

	// A tangent frame to sample across. The reference axis is whichever the direction points along
	// least, so the cross product never degenerates at the poles.
	const FVector Reference =
		FMath::Abs(Up.Z) < 0.9 ? FVector(0.0, 0.0, 1.0) : FVector(1.0, 0.0, 0.0);

	const FVector Tangent = FVector::CrossProduct(Reference, Up).GetSafeNormal();
	const FVector Bitangent = FVector::CrossProduct(Up, Tangent).GetSafeNormal();

	const double Offset = FMath::Tan(FMath::DegreesToRadians(
		FMath::Clamp(SampleAngleDegrees, 0.0005, 5.0)));

	// Four samples rather than three: a central difference either side is symmetric, so a constant
	// slope produces exactly the right answer instead of one biased toward the sample point.
	auto SurfacePoint = [&](const FVector& Offsets)
	{
		const FVector SampleDirection = (Up + Offsets).GetSafeNormal();

		return SampleDirection * SurfaceRadiusKilometres(Planet, Terrain, SampleDirection);
	};

	const FVector AlongTangent =
		SurfacePoint(Tangent * Offset) - SurfacePoint(Tangent * -Offset);

	const FVector AlongBitangent =
		SurfacePoint(Bitangent * Offset) - SurfacePoint(Bitangent * -Offset);

	const FVector Normal = FVector::CrossProduct(AlongTangent, AlongBitangent).GetSafeNormal();

	// Flat ground makes the cross product vanish, and a slope steep enough to flip it would be
	// an overhang, which a height field cannot represent anyway. Radial is right in both cases.
	if (Normal.IsNearlyZero() || FVector::DotProduct(Normal, Up) <= 0.0)
	{
		return Up;
	}

	return Normal;
}

FGroundContact FPlanetTerrain::ResolveContact(
	const FPlanetConfig& Planet,
	const FPlanetTerrainConfig& Terrain,
	const FSystemCoordinate& Position,
	const FVector& Velocity,
	const double ContactRadiusKilometres,
	const double ToleranceKilometres,
	const bool bWasOnGround)
{
	FGroundContact Contact;
	Contact.Position = Position;
	Contact.Velocity = Velocity;

	const FVector Offset = Position.Kilometres - Planet.Centre.Kilometres;
	const double Distance = Offset.Size();

	// At the exact centre there is no direction to be pushed along. Nothing sensible can be done,
	// and inventing one would produce a NaN.
	if (Distance < UE_DOUBLE_SMALL_NUMBER)
	{
		return Contact;
	}

	const FVector Up = Offset / Distance;

	Contact.SurfaceNormal = SurfaceNormal(Planet, Terrain, Up);

	const double GroundRadius = SurfaceRadiusKilometres(Planet, Terrain, Up);
	const double Floor = GroundRadius + FMath::Max(0.0, ContactRadiusKilometres);

	const double Gap = Distance - Floor;

	// Deliberately a speed threshold and not just a positive sign.
	//
	// On a curved surface the normal tilts to follow whatever is walking across it, so a purely
	// tangential velocity always has a small positive component along the current normal. Testing
	// dot > 0 therefore reports every walking step as leaving the ground — which it did, on 599 of
	// 600 frames. Half a metre per second is far below a jump and far above that artefact.
	constexpr double MinimumSeparationSpeed = 50.0;

	const bool bLeaving =
		FVector::DotProduct(Velocity, Contact.SurfaceNormal) > MinimumSeparationSpeed;

	// Wider to leave than to arrive.
	//
	// A single threshold cannot survive speed. At 738 m/s a ship crosses twelve metres of ground per
	// frame, and this terrain rises and falls by up to 0.33 m across that distance — more than the
	// twenty centimetres that decide contact, on 45 frames out of 60. So the state oscillated, four
	// times in 0.36 s in a real flight, and every one of those transitions was honest arithmetic on
	// a rule that had no memory.
	//
	// Ten times the capture band clears the worst step by a comfortable margin without being large
	// enough to hide anything a player would notice. Deliberate departures do not depend on it: they
	// are caught below by separation speed instead.
	constexpr double ReleaseToleranceMultiplier = 10.0;

	const double CaptureTolerance = FMath::Max(0.0, ToleranceKilometres);

	const double ReleaseTolerance =
		bWasOnGround ? CaptureTolerance * ReleaseToleranceMultiplier : CaptureTolerance;

	// Clear of the ground, or genuinely climbing away from it. The second case is what lets a jump
	// happen at all: without it the tolerance band would drag a rising character straight back down.
	if (Gap > ReleaseTolerance || (Gap > 0.0 && bLeaving))
	{
		return Contact;
	}

	Contact.bOnGround = true;

	// Placed exactly on the floor rather than nudged above it. A bias would make a resting object
	// hover, and hovering is indistinguishable from a bug at any altitude a player can see.
	Contact.Position = FSystemCoordinate(Planet.Centre.Kilometres + (Up * Floor));

	const double IntoGround = FVector::DotProduct(Velocity, Contact.SurfaceNormal);

	// Only motion into the surface is cancelled. Removing all of it would freeze a ship the moment
	// it touched anything, and reflecting it would make a landing a bounce.
	if (IntoGround < 0.0)
	{
		Contact.ImpactSpeed = -IntoGround;
		Contact.Velocity = Velocity - (Contact.SurfaceNormal * IntoGround);
	}

	return Contact;
}

FVector2D FPlanetTerrain::SurfaceUV(const FVector& Direction)
{
	const FVector Unit = Direction.GetSafeNormal();

	const double AbsX = FMath::Abs(Unit.X);
	const double AbsY = FMath::Abs(Unit.Y);
	const double AbsZ = FMath::Abs(Unit.Z);

	// Which cube face this direction points at: the largest component wins, which is the same rule
	// that decides which face a point belongs to when the cube is projected outward.
	double Across = 0.0;
	double Down = 0.0;
	double Largest = 0.0;

	if (AbsX >= AbsY && AbsX >= AbsZ)
	{
		Largest = AbsX;
		Across = Unit.Y;
		Down = Unit.Z;
	}
	else if (AbsY >= AbsZ)
	{
		Largest = AbsY;
		Across = Unit.X;
		Down = Unit.Z;
	}
	else
	{
		Largest = AbsZ;
		Across = Unit.X;
		Down = Unit.Y;
	}

	if (Largest <= UE_DOUBLE_SMALL_NUMBER)
	{
		return FVector2D::ZeroVector;
	}

	// Onto the face, then from -1..1 into 0..1.
	//
	// Deliberately not the inverse of CubeToSphere's warp. That warp exists to even out the area of
	// the tessellation, and undoing it exactly would need a root-solve per vertex for a texture
	// coordinate nobody measures. What matters is that every caller computes the same number from
	// the same direction, which this does whether or not it is the exact inverse.
	return FVector2D(
		((Across / Largest) + 1.0) * 0.5,
		((Down / Largest) + 1.0) * 0.5);
}

FVector FPlanetTerrain::CubeToSphere(const FVector& CubePoint)
{
	const double X2 = CubePoint.X * CubePoint.X;
	const double Y2 = CubePoint.Y * CubePoint.Y;
	const double Z2 = CubePoint.Z * CubePoint.Z;

	// The spherified-cube mapping. A plain normalise also lands on the sphere, but bunches
	// vertices toward the middle of each face and stretches them at the corners, so triangles
	// vary in size by roughly a factor of two across a face. This keeps them far more even, which
	// matters because triangle budget is spent per-triangle regardless of how much area it covers.
	return FVector(
		CubePoint.X * FMath::Sqrt(FMath::Max(0.0, 1.0 - (Y2 * 0.5) - (Z2 * 0.5) + (Y2 * Z2 / 3.0))),
		CubePoint.Y * FMath::Sqrt(FMath::Max(0.0, 1.0 - (Z2 * 0.5) - (X2 * 0.5) + (Z2 * X2 / 3.0))),
		CubePoint.Z * FMath::Sqrt(FMath::Max(0.0, 1.0 - (X2 * 0.5) - (Y2 * 0.5) + (X2 * Y2 / 3.0))));
}
