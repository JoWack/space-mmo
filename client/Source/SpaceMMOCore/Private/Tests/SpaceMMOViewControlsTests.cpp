#include "Misc/AutomationTest.h"

#include "SpaceMMOViewControls.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * A wheel notch means the same thing wherever the camera is.
 *
 * <strong>Which is why it is a proportion and not a number of centimetres.</strong> A fixed step is
 * either unusably coarse at the near end of the range or unusably slow at the far end, because what
 * anybody perceives is how much the view changed by, not how far the camera moved.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOViewZoomStepsByProportionTest,
	"SpaceMMO.View.ZoomStepsByProportion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOViewZoomStepsByProportionTest::RunTest(const FString& Parameters)
{
	constexpr double Minimum = 150.0;
	constexpr double Maximum = 900.0;
	constexpr double Fraction = 0.15;

	const double In = FViewZoom::Stepped(400.0, 1.0, Minimum, Maximum, Fraction);

	TestEqual(TEXT("One notch in takes fifteen per cent off"), In, 340.0, 0.001);

	const double Out = FViewZoom::Stepped(400.0, -1.0, Minimum, Maximum, Fraction);

	TestTrue(TEXT("And one notch out moves the other way"), Out > 400.0);

	// The property that actually makes it a proportion: a notch is worth more when the camera is
	// further out. Everything else here passes just as happily for a fixed number of centimetres --
	// which a mutation run proved, by replacing the whole thing with "subtract 60" and watching this
	// test stay green. Fifteen per cent of the four hundred it was started from is sixty, and a
	// fixed step reverses exactly as cleanly as a proportional one.
	const double FromNear = 400.0 - FViewZoom::Stepped(400.0, 1.0, Minimum, Maximum, Fraction);
	const double FromFar = 800.0 - FViewZoom::Stepped(800.0, 1.0, Minimum, Maximum, Fraction);

	TestEqual(
		TEXT("A notch from twice as far out moves twice as far"), FromFar, FromNear * 2.0, 0.001);

	// And going in and back out lands where it started, which is what stops repeated zooming from
	// drifting.
	const double ThereAndBack =
		FViewZoom::Stepped(
			FViewZoom::Stepped(400.0, 3.0, Minimum, Maximum, Fraction),
			-3.0, Minimum, Maximum, Fraction);

	TestEqual(TEXT("Three notches in and three back out is where it started"),
		ThereAndBack, 400.0, 0.001);

	// The ends of the range hold, however hard somebody spins the wheel.
	TestEqual(
		TEXT("Winding all the way in stops at the near limit"),
		FViewZoom::Stepped(400.0, 50.0, Minimum, Maximum, Fraction), Minimum, 0.001);

	TestEqual(
		TEXT("...and all the way out at the far one"),
		FViewZoom::Stepped(400.0, -50.0, Minimum, Maximum, Fraction), Maximum, 0.001);

	// Nonsense bounds leave the camera alone rather than putting it somewhere arbitrary.
	TestEqual(
		TEXT("A backwards range moves nothing"),
		FViewZoom::Stepped(400.0, 1.0, 900.0, 150.0, Fraction), 400.0, 0.001);

	return true;
}

/**
 * Letting go of Alt brings the camera back the short way.
 *
 * <strong>The wrap is the whole test.</strong> A camera swung to 190 degrees is 170 degrees from
 * home in one direction and 190 in the other, and easing the wrong way spins the view most of a
 * full turn every time somebody looks behind themselves.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOViewOrbitReturnsTheShortWayTest,
	"SpaceMMO.View.OrbitReturnsTheShortWay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOViewOrbitReturnsTheShortWayTest::RunTest(const FString& Parameters)
{
	constexpr double Frame = 1.0 / 60.0;
	constexpr double Seconds = 0.4;

	// Swung past the back of the character. Normalised this is -170, and the eased result must move
	// toward zero from there rather than climbing through 200, 250, 300.
	const FRotator Swung(0.0, 190.0, 0.0);

	const FRotator Eased = FViewOrbit::Recentred(Swung, Frame, Seconds);

	TestTrue(
		TEXT("A camera swung past the back comes home the short way"),
		Eased.Yaw < 0.0 && Eased.Yaw > -170.0);

	// Ordinary case: closer than it was, and not past.
	const FRotator Half = FViewOrbit::Recentred(FRotator(0.0, 60.0, 0.0), Frame, Seconds);

	TestTrue(TEXT("An ordinary swing eases toward zero"), Half.Yaw > 0.0 && Half.Yaw < 60.0);

	// Pitch comes home too, not just yaw.
	const FRotator Pitched = FViewOrbit::Recentred(FRotator(30.0, 0.0, 0.0), Frame, Seconds);

	TestTrue(TEXT("Pitch is eased as well"), Pitched.Pitch > 0.0 && Pitched.Pitch < 30.0);

	// Arrives, rather than approaching forever, and by the time it said it would.
	//
	// This is the assertion that caught the first attempt: at three time constants the camera still
	// had four and a half degrees of a ninety degree swing left when its time was up, then crept the
	// rest of the way. "Eases back over 0.4 s" has to mean it is there at 0.4 s.
	FRotator Running(0.0, 90.0, 0.0);

	for (int32 Frames = 0; Frames < static_cast<int32>(Seconds * 60.0); ++Frames)
	{
		Running = FViewOrbit::Recentred(Running, Frame, Seconds);
	}

	TestTrue(
		TEXT("And is home by the time it said it would be"),
		Running.IsNearlyZero());

	// The other half of "eases": it must not simply snap. Half way through, a ninety degree swing
	// should still be visibly off-centre, or the duration is decoration.
	FRotator Partway(0.0, 90.0, 0.0);

	for (int32 Frames = 0; Frames < static_cast<int32>(Seconds * 30.0); ++Frames)
	{
		Partway = FViewOrbit::Recentred(Partway, Frame, Seconds);
	}

	TestTrue(
		TEXT("...and is still on its way at the halfway point, rather than snapping"),
		FMath::Abs(Partway.Yaw) > 2.0);

	// Frame-rate independence: a big step and many small ones covering the same time end up in
	// about the same place, or the camera returns at a speed that depends on the frame rate.
	FRotator Small(0.0, 90.0, 0.0);

	for (int32 Frames = 0; Frames < 10; ++Frames)
	{
		Small = FViewOrbit::Recentred(Small, 0.01, Seconds);
	}

	const FRotator Large = FViewOrbit::Recentred(FRotator(0.0, 90.0, 0.0), 0.1, Seconds);

	TestEqual(
		TEXT("Ten small steps land where one big one does"), Small.Yaw, Large.Yaw, 0.5);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
