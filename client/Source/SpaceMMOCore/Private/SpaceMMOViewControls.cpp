#include "SpaceMMOViewControls.h"

double FViewZoom::Stepped(
	const double Current,
	const double Notches,
	const double Minimum,
	const double Maximum,
	const double Fraction)
{
	// A range that is backwards, or bounds that are not positive, would otherwise produce a camera
	// somewhere arbitrary. Staying put is the one answer that cannot be wrong.
	if (!(Maximum > Minimum) || Minimum <= 0.0)
	{
		return Current;
	}

	const double Safe = FMath::Clamp(Fraction, 0.0, 0.9);

	// Each notch scales by the same proportion, so five notches in and five back out land where they
	// started rather than drifting -- which a repeated addition and subtraction would not.
	const double Scaled = Current * FMath::Pow(1.0 - Safe, Notches);

	return FMath::Clamp(Scaled, Minimum, Maximum);
}

FRotator FViewOrbit::Recentred(
	const FRotator& Current, const double DeltaSeconds, const double Seconds)
{
	if (DeltaSeconds <= 0.0)
	{
		return Current;
	}

	// Anything at or below zero is a caller asking for no easing at all.
	if (Seconds <= 0.0)
	{
		return FRotator::ZeroRotator;
	}

	// The short way round. GetNormalized brings each axis into -180..180, so a camera swung past
	// the back of the character returns through the back rather than all the way round the front.
	const FRotator Shortest = Current.GetNormalized();

	// Exponential decay, framerate-independent: the same fraction of the remaining angle every
	// second however often that second is sampled.
	//
	// Five time constants rather than three, and a degree of slack rather than a tenth, because the
	// first attempt at this did not arrive. Three left five per cent of the swing outstanding at the
	// time it claimed to be finished -- four and a half degrees off a ninety degree orbit, which is
	// a camera that visibly settles late and then creeps. The test asked whether it was home when it
	// said it would be, and it was not.
	constexpr double TimeConstants = 5.0;

	const double Remaining = FMath::Exp(-TimeConstants * DeltaSeconds / Seconds);

	FRotator Eased = Shortest * Remaining;

	// A degree is below anything anybody can see, and it is a threshold the decay above actually
	// reaches within Seconds. Letting it run instead keeps a camera fractionally off-centre forever.
	constexpr double ArrivedDegrees = 1.0;

	if (FMath::Abs(Eased.Yaw) < ArrivedDegrees && FMath::Abs(Eased.Pitch) < ArrivedDegrees
		&& FMath::Abs(Eased.Roll) < ArrivedDegrees)
	{
		return FRotator::ZeroRotator;
	}

	return Eased;
}

void FThirdPersonView::Wheel(
	const double Notches, const double Minimum, const double Maximum, const double Fraction)
{
	ArmTargetCentimetres =
		FViewZoom::Stepped(ArmTargetCentimetres, Notches, Minimum, Maximum, Fraction);
}

void FThirdPersonView::Swing(
	const double YawDegrees, const double PitchDegrees, const double MaxPitchDegrees)
{
	if (!bOrbiting)
	{
		return;
	}

	Orbit.Yaw += YawDegrees;

	// Clamped rather than wrapped, for the reason the ordinary look is: past vertical the view rolls
	// over and every movement after that reads inverted.
	Orbit.Pitch = FMath::Clamp(
		Orbit.Pitch + PitchDegrees, -MaxPitchDegrees, MaxPitchDegrees);
}

void FThirdPersonView::Advance(const double DeltaSeconds, const double ReturnSeconds)
{
	// Held means held. A view that crept back while somebody was still looking would fight them.
	if (bOrbiting)
	{
		return;
	}

	Orbit = FViewOrbit::Recentred(Orbit, DeltaSeconds, ReturnSeconds);
}

FVector FThirdPersonView::ShoulderAt(
	const FVector& AtReference,
	const double ArmCentimetres,
	const double ReferenceArmCentimetres)
{
	// A reference of nothing names no ratio, and dividing by it would put the camera somewhere
	// arbitrary. The authored offset is the one answer that cannot be wrong.
	if (ReferenceArmCentimetres <= 0.0 || ArmCentimetres <= 0.0)
	{
		return AtReference;
	}

	return AtReference * (ArmCentimetres / ReferenceArmCentimetres);
}
