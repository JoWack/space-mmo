#include "SpaceMMOStationMarkers.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Tests for which stations get a chevron (task 160).
 *
 * <strong>The near-side test is the one worth writing carefully.</strong> "Is it on the half facing
 * me" is the obvious rule and it is wrong: from 40 km above a 20 km world the visible cap reaches
 * only 70.5 degrees from the sub-point, not 90. A station at 80 degrees is over the limb and a
 * hemisphere test would draw a confident chevron pointing through the planet at it. Every case
 * below that could pass against the naive rule is marked as such.
 */

namespace
{
	constexpr double PlanetRadius = 20.0;

	FSpaceMMOMarkerBody CapitalBody()
	{
		FSpaceMMOMarkerBody Body;
		Body.Id = 5;
		Body.Centre = FSystemCoordinate(0.0, 0.0, 0.0);
		Body.RadiusKilometres = PlanetRadius;
		Body.bValid = true;

		return Body;
	}

	/** A point on the body's surface, at an angle from the direction the viewer sits in. */
	FSystemCoordinate OnSurfaceAtDegrees(const double Degrees)
	{
		const double Radians = FMath::DegreesToRadians(Degrees);

		return FSystemCoordinate(
			PlanetRadius * FMath::Cos(Radians), PlanetRadius * FMath::Sin(Radians), 0.0);
	}

	FSpaceMMOMarkerStation OnBody(const int32 Id, const int32 BodyId, const FSystemCoordinate& Where)
	{
		FSpaceMMOMarkerStation Station;
		Station.Id = Id;
		Station.BodyId = BodyId;
		Station.bOnBody = true;
		Station.Position = Where;

		return Station;
	}

	FSpaceMMOMarkerStation InDeepSpace(const int32 Id, const FSystemCoordinate& Where)
	{
		FSpaceMMOMarkerStation Station;
		Station.Id = Id;
		Station.bOnBody = false;
		Station.Position = Where;

		return Station;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOStationNearSideTest,
	"SpaceMMO.Stations.NearSideIsTheLimbNotTheHemisphere",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOStationNearSideTest::RunTest(const FString& Parameters)
{
	const FSpaceMMOMarkerBody Body = CapitalBody();

	// 40 km up, so the visible cap is acos(20/60) = 70.5 degrees from the sub-point.
	const FSystemCoordinate Viewer(60.0, 0.0, 0.0);

	TestTrue(
		TEXT("The sub-point is visible"),
		FSpaceMMOStationMarkers::FacesViewer(OnSurfaceAtDegrees(0.0), Body.Centre, Viewer));

	TestTrue(
		TEXT("Well inside the cap is visible"),
		FSpaceMMOStationMarkers::FacesViewer(OnSurfaceAtDegrees(60.0), Body.Centre, Viewer));

	// The case that discriminates. A hemisphere test says visible (it is 80 degrees, not 90); the
	// limb test says hidden, because the cap stops at 70.5.
	TestFalse(
		TEXT("Eighty degrees is over the limb from this altitude, though still on the near half"),
		FSpaceMMOStationMarkers::FacesViewer(OnSurfaceAtDegrees(80.0), Body.Centre, Viewer));

	TestFalse(
		TEXT("The antipode is hidden"),
		FSpaceMMOStationMarkers::FacesViewer(OnSurfaceAtDegrees(180.0), Body.Centre, Viewer));

	// And the cap really does widen with altitude, or the rule would be a constant angle wearing a
	// dot product. From 380 km up, acos(20/400) = 87.1 degrees, so 80 is now inside it.
	TestTrue(
		TEXT("The same point is visible from higher up"),
		FSpaceMMOStationMarkers::FacesViewer(
			OnSurfaceAtDegrees(80.0), Body.Centre, FSystemCoordinate(400.0, 0.0, 0.0)));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOStationOcclusionTest,
	"SpaceMMO.Stations.ABodyHidesWhatIsBehindIt",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOStationOcclusionTest::RunTest(const FString& Parameters)
{
	const FSpaceMMOMarkerBody Body = CapitalBody();

	const FSystemCoordinate Viewer(60.0, 0.0, 0.0);

	// Straight through the middle of the planet.
	TestTrue(
		TEXT("A dock on the far side is hidden"),
		FSpaceMMOStationMarkers::IsHiddenBehind(
			FSystemCoordinate(-60.0, 0.0, 0.0), Viewer, Body.Centre, PlanetRadius));

	// Past the planet and genuinely clear of it: the sight line's closest approach to the centre is
	// 33 km, comfortably outside a 20 km body.
	TestFalse(
		TEXT("A dock beyond the planet but well off to one side is visible"),
		FSpaceMMOStationMarkers::IsHiddenBehind(
			FSystemCoordinate(-60.0, 80.0, 0.0), Viewer, Body.Centre, PlanetRadius));

	// And only just clipping it is still hidden, which is the case worth pinning: this position
	// looks "off to one side" and is not. Its sight line passes 18.97 km from the centre of a 20 km
	// world, so a chevron there would point through the planet. The first version of this test
	// asserted the opposite and the rule was right.
	TestTrue(
		TEXT("A dock that only looks clear of the limb is still hidden"),
		FSpaceMMOStationMarkers::IsHiddenBehind(
			FSystemCoordinate(-60.0, 40.0, 0.0), Viewer, Body.Centre, PlanetRadius));

	// The clamping cases, which an unclamped line-versus-sphere test gets wrong. Both of these
	// have the planet on the *line* through viewer and station, but not on the segment between
	// them -- so nothing is actually in the way.
	TestFalse(
		TEXT("A planet behind the viewer hides nothing"),
		FSpaceMMOStationMarkers::IsHiddenBehind(
			FSystemCoordinate(200.0, 0.0, 0.0), Viewer, Body.Centre, PlanetRadius));

	TestFalse(
		TEXT("A planet beyond the station hides nothing"),
		FSpaceMMOStationMarkers::IsHiddenBehind(
			FSystemCoordinate(40.0, 0.0, 0.0), Viewer, Body.Centre, PlanetRadius));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOStationOrbitalSelectionTest,
	"SpaceMMO.Stations.OrbitShowsTheNearSideAndDeepSpace",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOStationOrbitalSelectionTest::RunTest(const FString& Parameters)
{
	const FSpaceMMOMarkerBody Body = CapitalBody();

	const FSystemCoordinate Viewer(60.0, 0.0, 0.0);

	const TArray<FSpaceMMOMarkerStation> Stations =
	{
		OnBody(1, 5, OnSurfaceAtDegrees(10.0)),    // near side of this body
		OnBody(2, 5, OnSurfaceAtDegrees(170.0)),   // far side of this body
		OnBody(3, 9, FSystemCoordinate(-120.0, 60.0, 0.0)), // another world entirely
		InDeepSpace(4, FSystemCoordinate(30.0, 12.0, 4.0)), // Deepdock, in the clear
		InDeepSpace(5, FSystemCoordinate(-60.0, 0.0, 0.0)), // behind the planet
	};

	TArray<int32> Marked;
	int32 Named = INDEX_NONE;

	FSpaceMMOStationMarkers::Select(
		Stations, Viewer, EPlanetProximity::Orbital, Body, false, Marked, Named);

	TestEqual(TEXT("Two stations are shown"), Marked.Num(), 2);
	TestTrue(TEXT("The near-side station"), Marked.Contains(0));
	TestTrue(TEXT("And the deep-space dock in the clear"), Marked.Contains(3));

	TestFalse(TEXT("Not the far side of this body"), Marked.Contains(1));
	TestFalse(TEXT("Not a station on another world"), Marked.Contains(2));
	TestFalse(TEXT("Not a dock behind the planet"), Marked.Contains(4));

	// Deepdock at (30,12,4) is 32.6 km away; the near-side station is 40 km. The readout names the
	// closer, which is the deep-space one -- so this also proves the naming is by distance rather
	// than by the order they were listed in.
	TestEqual(TEXT("The readout names the closest shown"), Named, 3);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOStationCloseInSelectionTest,
	"SpaceMMO.Stations.CloseInShowsOnlyThisBody",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOStationCloseInSelectionTest::RunTest(const FString& Parameters)
{
	const FSpaceMMOMarkerBody Body = CapitalBody();

	// Standing on the surface.
	const FSystemCoordinate Viewer = OnSurfaceAtDegrees(0.0);

	// <strong>The station on another body is deliberately the nearer one.</strong> In the shipped
	// layout that cannot happen -- worlds are 180 km apart and your own body's station is at most
	// half a circumference away -- so a test using real positions would pass against a rule that
	// simply took the closest and never checked the body at all. These numbers are chosen so that
	// only a rule gating on the body gets it right.
	const TArray<FSpaceMMOMarkerStation> Stations =
	{
		OnBody(1, 5, OnSurfaceAtDegrees(90.0)),                     // this body, 28 km away
		OnBody(2, 9, FSystemCoordinate(21.0, 0.0, 0.0)),            // another body, 1 km away
		InDeepSpace(3, FSystemCoordinate(20.5, 0.0, 0.0)),          // deep space, 0.5 km away
	};

	for (const EPlanetProximity Close :
		{ EPlanetProximity::Surface, EPlanetProximity::Atmospheric })
	{
		TArray<int32> Marked;
		int32 Named = INDEX_NONE;

		FSpaceMMOStationMarkers::Select(Stations, Viewer, Close, Body, false, Marked, Named);

		TestEqual(TEXT("Exactly one station is shown close in"), Marked.Num(), 1);
		TestEqual(TEXT("And it is the one on this body, not the nearer one"), Named, 0);
	}

	// Nothing on this body at all is a working state, and it draws nothing rather than falling
	// back to whatever else is around.
	const TArray<FSpaceMMOMarkerStation> Elsewhere =
	{
		OnBody(2, 9, FSystemCoordinate(21.0, 0.0, 0.0)),
	};

	TArray<int32> Marked;
	int32 Named = INDEX_NONE;

	FSpaceMMOStationMarkers::Select(
		Elsewhere, Viewer, EPlanetProximity::Surface, Body, false, Marked, Named);

	TestEqual(TEXT("Nothing on this body means nothing drawn"), Marked.Num(), 0);
	TestEqual(TEXT("And nothing named"), Named, INDEX_NONE);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOStationDockedTest,
	"SpaceMMO.Stations.DockedDrawsNothing",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOStationDockedTest::RunTest(const FString& Parameters)
{
	const FSpaceMMOMarkerBody Body = CapitalBody();

	const TArray<FSpaceMMOMarkerStation> Stations =
	{
		OnBody(1, 5, OnSurfaceAtDegrees(0.0)),
	};

	// In both modes, because "hidden while docked" has to beat whichever rule would otherwise
	// apply rather than being a special case of one of them.
	for (const EPlanetProximity Any :
		{ EPlanetProximity::Orbital, EPlanetProximity::Surface })
	{
		TArray<int32> Marked;
		int32 Named = INDEX_NONE;

		FSpaceMMOStationMarkers::Select(
			Stations, OnSurfaceAtDegrees(0.0), Any, Body, true, Marked, Named);

		TestEqual(TEXT("Docked draws no chevrons"), Marked.Num(), 0);
		TestEqual(TEXT("And names nothing"), Named, INDEX_NONE);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOStationLineTest,
	"SpaceMMO.Stations.TheReadoutLineSaysWhatToDo",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOStationLineTest::RunTest(const FString& Parameters)
{
	// The exact case that produced task 160: 266 m from a station that docks within 100.
	TestEqual(
		TEXT("Out of range, and it says what the range is"),
		FSpaceMMOStationLine::Format(TEXT("Terra Outpost"), 0.266, 0.1, true, TEXT("Terra")),
		TEXT("Terra Outpost  266 m  ·  dock at 100 m"));

	// Inside the ring. The point of the line is that a pilot should never have to press G to find
	// out, which is what it was reduced to.
	TestEqual(
		TEXT("In range, and it says so"),
		FSpaceMMOStationLine::Format(TEXT("Terra Outpost"), 0.084, 0.1, true, TEXT("Terra")),
		TEXT("Terra Outpost  84 m  ·  READY"));

	// Exactly at the ring is inside it, matching ASpaceMMOStationActor::IsWithinDockingRange rather
	// than being a hair stricter -- a line reading "dock at 100 m" at exactly 100 m, next to a key
	// that works, is the sort of disagreement that gets reported as a docking bug.
	TestEqual(
		TEXT("Exactly at the ring is in range"),
		FSpaceMMOStationLine::Format(TEXT("Terra Outpost"), 0.1, 0.1, true, TEXT("Terra")),
		TEXT("Terra Outpost  100 m  ·  READY"));

	// Far enough that metres stop meaning anything, and the world is the useful fact.
	TestEqual(
		TEXT("Far away, on a body, it names the world"),
		FSpaceMMOStationLine::Format(TEXT("Terra Outpost"), 118.0, 0.1, true, TEXT("Terra")),
		TEXT("Terra Outpost  118 km  ·  at Terra"));

	// And a deep-space dock says so, because "which planet do I fly to" has the answer "none" and
	// a pilot heading for a planet expecting to land there needs telling.
	TestEqual(
		TEXT("Far away, in deep space, it says so"),
		FSpaceMMOStationLine::Format(TEXT("Deepdock"), 33.0, 0.5, false, FString()),
		TEXT("Deepdock  33 km  ·  deep space"));

	// Nothing to name draws nothing. A row reading "Station: none" is dead pixels on most frames
	// of a game that is mostly flying between things.
	TestTrue(
		TEXT("No station is an empty line, not a word"),
		FSpaceMMOStationLine::Format(FString(), 1.0, 0.1, true, TEXT("Terra")).IsEmpty());

	// The chevron's label and the readout switch units at the same distance, or the two would
	// disagree about whether something is far away while sitting on the same screen.
	TestEqual(TEXT("Short, close in"), FSpaceMMOStationLine::Short(0.266), TEXT("266 m"));
	TestEqual(TEXT("Short, far out"), FSpaceMMOStationLine::Short(118.0), TEXT("118 km"));

	TestEqual(
		TEXT("And they change over at the same threshold"),
		FSpaceMMOStationLine::Short(FSpaceMMOStationLine::FarKilometres),
		TEXT("10 km"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOStationNearestPlacedTest,
	"SpaceMMO.Stations.NearestIsOneDefinition",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOStationNearestPlacedTest::RunTest(const FString& Parameters)
{
	// This is what the docking refusal reports, so it deliberately ignores bodies and near sides:
	// "nearest is Terra Outpost at 266 m" is about what is near, not about what may be drawn.
	const TArray<FSpaceMMOMarkerStation> Stations =
	{
		OnBody(1, 5, FSystemCoordinate(0.0, 0.0, 0.0)),
		OnBody(2, 9, FSystemCoordinate(3.0, 4.0, 0.0)),
	};

	double Kilometres = 0.0;

	TestEqual(
		TEXT("The nearer of the two"),
		FSpaceMMOStationMarkers::NearestPlaced(
			Stations, FSystemCoordinate(3.0, 5.0, 0.0), Kilometres),
		1);

	TestEqual(TEXT("And its distance"), Kilometres, 1.0);

	TestEqual(
		TEXT("Nothing to be near is INDEX_NONE, not zero"),
		FSpaceMMOStationMarkers::NearestPlaced({}, FSystemCoordinate(0.0, 0.0, 0.0), Kilometres),
		INDEX_NONE);

	TestEqual(TEXT("And reports no distance"), Kilometres, 0.0);

	return true;
}

#endif
