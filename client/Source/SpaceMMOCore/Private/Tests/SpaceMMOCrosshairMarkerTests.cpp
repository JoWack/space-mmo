#include "Misc/AutomationTest.h"

#include "SpaceMMOCrosshairMarker.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The marker sits where the ship is going, and screen Y runs down.
 *
 * <strong>The sign on the vertical is most of why this is a function.</strong> Screen space runs
 * downward and a camera's up runs up, and getting that backwards looks perfectly correct in every
 * still frame: centred when flying straight, off to the right when drifting right, and wrong only
 * while climbing.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOCrosshairMarkerSitsWhereTheShipIsGoingTest,
	"SpaceMMO.Crosshair.MarkerSitsWhereTheShipIsGoing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOCrosshairMarkerSitsWhereTheShipIsGoingTest::RunTest(const FString& Parameters)
{
	constexpr double Focal = 800.0;
	constexpr double MaxRadius = 400.0;

	FVector2D Offset;

	// Straight ahead is dead centre, which is what makes the marker mean anything at all: sitting on
	// the reticle is the ship going where it is pointing.
	TestTrue(
		TEXT("Flying straight puts the marker on the reticle"),
		FCrosshairMarker::ScreenOffset(1.0, 0.0, 0.0, Focal, MaxRadius, Offset));

	TestTrue(TEXT("...at the centre"), Offset.IsNearlyZero());

	TestTrue(
		TEXT("Drifting right is answered"),
		FCrosshairMarker::ScreenOffset(1.0, 0.25, 0.0, Focal, MaxRadius, Offset));

	TestEqual(TEXT("...to the right, by the focal length times the ratio"), Offset.X, 200.0, 0.001);
	TestEqual(TEXT("...and neither up nor down"), Offset.Y, 0.0, 0.001);

	// Climbing moves the marker *up* the screen, which is negative Y. This is the assertion a still
	// frame cannot make.
	TestTrue(
		TEXT("Climbing is answered"),
		FCrosshairMarker::ScreenOffset(1.0, 0.0, 0.25, Focal, MaxRadius, Offset));

	TestTrue(TEXT("...by moving the marker up the screen, which is -Y"), Offset.Y < 0.0);

	// Angle matters and speed does not: a fast ship and a slow one going the same way put the marker
	// in the same place, or it would slide with the throttle.
	FVector2D Slow;
	FVector2D Fast;

	FCrosshairMarker::ScreenOffset(1.0, 0.25, 0.0, Focal, MaxRadius, Slow);
	FCrosshairMarker::ScreenOffset(40.0, 10.0, 0.0, Focal, MaxRadius, Fast);

	TestEqual(TEXT("Speed does not move the marker, only direction"), Fast.X, Slow.X, 0.001);

	return true;
}

/**
 * Going somewhere the screen cannot show still says which way to turn.
 *
 * A ship in this model routinely travels backwards -- turning to face a station while still carrying
 * the velocity that got you there is the ordinary way to arrive. The marker has to keep meaning
 * something through that, and a projected point does not: a direction behind the camera lands
 * mirrored, so the pilot is told to turn exactly the wrong way.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOCrosshairMarkerPinsWhatItCannotShowTest,
	"SpaceMMO.Crosshair.MarkerPinsWhatItCannotShow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOCrosshairMarkerPinsWhatItCannotShowTest::RunTest(const FString& Parameters)
{
	constexpr double Focal = 800.0;
	constexpr double MaxRadius = 400.0;

	FVector2D Offset;

	// Behind and to the right. Turning right brings the velocity round, so the marker belongs on the
	// right -- which is the side a naive projection would not put it on.
	TestTrue(
		TEXT("A direction behind the camera is still answered"),
		FCrosshairMarker::ScreenOffset(-1.0, 0.5, 0.0, Focal, MaxRadius, Offset));

	TestTrue(TEXT("...pinned to the side it actually lies on"), Offset.X > 0.0);

	TestEqual(TEXT("...at the edge"), Offset.Size(), MaxRadius, 0.001);

	// Straight out of the back names no side at all, and picking one would send a pilot turning in a
	// direction nothing chose.
	TestFalse(
		TEXT("Straight backwards is not answered"),
		FCrosshairMarker::ScreenOffset(-1.0, 0.0, 0.0, Focal, MaxRadius, Offset));

	// A hard sideways drift would otherwise put the marker off the edge of the screen, where it has
	// stopped answering the question it exists for.
	TestTrue(
		TEXT("A wild angle is answered"),
		FCrosshairMarker::ScreenOffset(0.2, 4.0, 0.0, Focal, MaxRadius, Offset));

	TestEqual(
		TEXT("...pinned to the edge rather than lost off it"), Offset.Size(), MaxRadius, 0.001);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
