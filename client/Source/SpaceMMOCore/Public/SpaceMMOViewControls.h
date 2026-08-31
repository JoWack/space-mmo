#pragma once

#include "CoreMinimal.h"

/**
 * How a third-person camera zooms and swings, as arithmetic.
 *
 * <strong>A client concern, and that is a rule rather than a note.</strong> `design-bible.md` §8:
 * "the camera is a client concern only — it must never affect server-side validation, which is why
 * interaction range is checked against the pawn, never the camera." Nothing here reaches FWalkInput,
 * nothing here is replicated, and nothing here is consulted by anything that decides what a player
 * can reach.
 *
 * Pure, so the feel of it can be pinned down without a world: the awkward cases are a wheel notch at
 * the end of the range and a camera easing back across the wrap at 180 degrees, and both are
 * arithmetic that is easy to get subtly wrong and tedious to judge by eye.
 */
struct SPACEMMOCORE_API FViewZoom
{
	/**
	 * Where a wheel notch leaves the camera arm, in centimetres.
	 *
	 * <strong>Multiplicative, not a fixed step.</strong> A fixed number of centimetres per notch is
	 * either unusably coarse when the camera is close or unusably slow when it is far, because what
	 * a person perceives is the proportion the view changed by. A constant fraction feels the same
	 * at both ends of the range.
	 *
	 * @param Current   The arm length now.
	 * @param Notches   Wheel notches, positive to zoom in.
	 * @param Minimum   Closest the camera may come.
	 * @param Maximum   Furthest it may go.
	 * @param Fraction  How much of the current length one notch is worth, 0..1.
	 */
	static double Stepped(
		double Current, double Notches, double Minimum, double Maximum, double Fraction);
};

/**
 * The camera swing that Alt holds, and lets go of.
 */
struct SPACEMMOCORE_API FViewOrbit
{
	/**
	 * Eases an orbit back to sitting behind the pawn.
	 *
	 * <strong>By the short way round.</strong> A camera swung to 190 degrees has to come back
	 * through 180, not the other 170 degrees the long way, and the difference is a view that spins
	 * a full turn every time somebody looks behind them. Normalising is the whole reason this is a
	 * function and not an FMath call at the call site.
	 *
	 * Exponential rather than linear, so it arrives without a stop: a constant rate lands on zero
	 * and halts, which reads as the camera being snapped back rather than released.
	 *
	 * @param Current       Where the orbit is now, in degrees.
	 * @param DeltaSeconds  This frame.
	 * @param Seconds       Roughly how long the whole return should take.
	 */
	static FRotator Recentred(const FRotator& Current, double DeltaSeconds, double Seconds);
};

/**
 * What a third-person view remembers: how far out it is, and how far it has been swung.
 *
 * <strong>Shared because a character and a ship are the same problem.</strong> Both hang a spring
 * arm off a pawn, both zoom on the wheel and both orbit on Alt, and the only thing that differs is
 * what the mouse would otherwise have been doing -- turning a body, or yawing a hull. Writing it
 * twice would be two places for the feel of it to drift apart, which is the shape of bug this
 * project keeps paying for.
 *
 * Holds no components and touches no world: the pawns own their booms and read this to drive them.
 */
struct SPACEMMOCORE_API FThirdPersonView
{
	/** How far back the camera is heading, in centimetres. The boom eases toward it. */
	double ArmTargetCentimetres = 0.0;

	/** Whether the orbit key is held. While it is, the mouse swings the view instead of the pawn. */
	bool bOrbiting = false;

	/** How far the view is swung from sitting behind the pawn. */
	FRotator Orbit = FRotator::ZeroRotator;

	/** Takes a wheel notch, positive to zoom in. */
	void Wheel(double Notches, double Minimum, double Maximum, double Fraction);

	/** Takes mouse movement while the orbit key is held. Pitch is clamped short of vertical. */
	void Swing(double YawDegrees, double PitchDegrees, double MaxPitchDegrees);

	/** Eases the swing back to centre when the key is not held. */
	void Advance(double DeltaSeconds, double ReturnSeconds);
};
