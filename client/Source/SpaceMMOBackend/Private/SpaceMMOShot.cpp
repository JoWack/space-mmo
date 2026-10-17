#include "SpaceMMOShot.h"

namespace
{
	/** Where a ray from the origin first meets a sphere, or negative. Direction is unit length. */
	double RayMeetsSphere(const FVector& Direction, const FVector& Centre, const double Radius)
	{
		const double B = FVector::DotProduct(Direction, -Centre);
		const double C = Centre.SizeSquared() - Radius * Radius;
		const double H = B * B - C;

		if (H < 0.0)
		{
			return -1.0;
		}

		const double Root = FMath::Sqrt(H);
		const double Near = -B - Root;

		// A ray starting inside the sphere meets it at once.
		return Near >= 0.0 ? Near : (-B + Root >= 0.0 ? 0.0 : -1.0);
	}
}

double SpaceMMO::Shot::RayMeetsCapsule(
	const FVector& InDirection, const FVector& AxisBottom, const FVector& AxisTop, const double Radius)
{
	const FVector Direction = InDirection.GetSafeNormal();

	if (Direction.IsNearlyZero() || Radius <= 0.0)
	{
		return -1.0;
	}

	double Best = -1.0;

	auto Consider = [&Best](const double T)
	{
		if (T >= 0.0 && (Best < 0.0 || T < Best))
		{
			Best = T;
		}
	};

	// The rounded ends.
	Consider(RayMeetsSphere(Direction, AxisBottom, Radius));
	Consider(RayMeetsSphere(Direction, AxisTop, Radius));

	// The straight side: an infinite cylinder about the axis, kept only between the ends.
	const FVector Axis = AxisTop - AxisBottom;
	const double AxisLength = Axis.Size();

	if (AxisLength > UE_DOUBLE_SMALL_NUMBER)
	{
		const FVector A = Axis / AxisLength;

		// Everything perpendicular to the axis: the ray, and the way from the bottom of the axis to the origin.
		const FVector DirectionAcross = Direction - A * FVector::DotProduct(Direction, A);
		const FVector Start = -AxisBottom;
		const FVector StartAcross = Start - A * FVector::DotProduct(Start, A);

		const double QA = DirectionAcross.SizeSquared();
		const double QB = 2.0 * FVector::DotProduct(DirectionAcross, StartAcross);
		const double QC = StartAcross.SizeSquared() - Radius * Radius;

		if (QA > UE_DOUBLE_SMALL_NUMBER)
		{
			const double Discriminant = QB * QB - 4.0 * QA * QC;

			if (Discriminant >= 0.0)
			{
				const double Root = FMath::Sqrt(Discriminant);

				for (const double T : {(-QB - Root) / (2.0 * QA), (-QB + Root) / (2.0 * QA)})
				{
					const double Along = FVector::DotProduct(Start + Direction * T, A);

					if (Along >= 0.0 && Along <= AxisLength)
					{
						Consider(T < 0.0 && QC <= 0.0 ? 0.0 : T);
					}
				}
			}
		}
	}

	return Best;
}

EBackendHitZone SpaceMMO::Shot::ZoneAt(const double HeightFraction)
{
	if (HeightFraction >= HeadBottomFraction)
	{
		return EBackendHitZone::Head;
	}

	return HeightFraction < LegsTopFraction ? EBackendHitZone::Legs : EBackendHitZone::Body;
}

EBackendHitFacing SpaceMMO::Shot::FacingOf(
	const FVector& TargetForward, const FVector& TargetUp, const FVector& ShotDirection)
{
	const FVector Up = TargetUp.GetSafeNormal();

	// Along the ground, both of them: a shot from a hill above somebody still came from their front or back.
	const FVector Facing = (TargetForward - Up * FVector::DotProduct(TargetForward, Up)).GetSafeNormal();
	const FVector Travel = (ShotDirection - Up * FVector::DotProduct(ShotDirection, Up)).GetSafeNormal();

	if (Facing.IsNearlyZero() || Travel.IsNearlyZero())
	{
		// Straight down on somebody: no side to speak of, and front is the one that changes nothing.
		return EBackendHitFacing::Front;
	}

	const double Along = FVector::DotProduct(Facing, Travel);
	const double Diagonal = UE_DOUBLE_HALF_SQRT_2;

	if (Along >= Diagonal)
	{
		return EBackendHitFacing::Back;
	}

	return Along <= -Diagonal ? EBackendHitFacing::Front : EBackendHitFacing::Side;
}

bool SpaceMMO::Shot::Resolve(
	const FVector& FromKilometres,
	const FVector& InDirection,
	const double RangeMetres,
	const TArrayView<const FSpaceMMOShotTarget> Targets,
	FSpaceMMOShotHit& OutHit)
{
	const FVector Direction = InDirection.GetSafeNormal();

	if (Direction.IsNearlyZero() || RangeMetres <= 0.0)
	{
		return false;
	}

	bool bFound = false;

	for (const FSpaceMMOShotTarget& Target : Targets)
	{
		// Relative to the muzzle and in metres, so the arithmetic is done near zero rather than at system scale.
		const FVector Feet = (Target.FeetKilometres - FromKilometres) * 1000.0;
		const FVector Up = Target.Up.GetSafeNormal();
		const double Radius = Target.RadiusMetres;

		// The capsule's axis runs from a radius above the feet to a radius below the top of the head.
		const FVector Bottom = Feet + Up * Radius;
		const FVector Top = Feet + Up * FMath::Max(Radius, Target.HeightMetres - Radius);

		const double T = RayMeetsCapsule(Direction, Bottom, Top, Radius);

		if (T < 0.0 || T > RangeMetres || (bFound && T >= OutHit.DistanceMetres))
		{
			continue;
		}

		const FVector Point = Direction * T;
		const double Height = FVector::DotProduct(Point - Feet, Up);

		OutHit.Index = Target.Index;
		OutHit.DistanceMetres = T;
		OutHit.PointKilometres = FromKilometres + Point / 1000.0;
		OutHit.Zone = ZoneAt(Target.HeightMetres > 0.0 ? Height / Target.HeightMetres : 0.5);
		OutHit.Facing = FacingOf(Target.Forward, Up, Direction);

		bFound = true;
	}

	return bFound;
}
