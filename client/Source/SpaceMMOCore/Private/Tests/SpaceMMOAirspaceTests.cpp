#include "Misc/AutomationTest.h"
#include "SpaceMMOAirspace.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace SpaceMMOAirspaceTests
{
	/**
	 * A city shaped like Borlash, standing where up is well away from every world axis.
	 *
	 * Tilted on purpose. Borlash itself stands near the Capital's pole, where up is within two degrees
	 * of world Z, so a zone tested there would pass with the rotation dropped entirely -- and the next
	 * city will not stand on a pole.
	 */
	FSystemCoordinate At(const FSpaceMMOAirspaceZone& Zone, const FVector& LocalKilometres);

	FSpaceMMOAirspaceZone Borlash()
	{
		FSpaceMMOAirspaceZone Zone;
		Zone.Key = TEXT("station_capital_hub");
		Zone.Name = TEXT("Borlash");
		Zone.Origin = FSystemCoordinate(FVector(-7000.0, 4300.0, 2900.0));
		Zone.Rotation = FRotationMatrix::MakeFromZ(FVector(-0.8, 0.5, 0.33).GetSafeNormal()).ToQuat();
		Zone.FloorKilometres = 0.0003;
		Zone.NoFlyRadiusKilometres = 0.215;
		Zone.NoFlyCeilingKilometres = 3.0;
		Zone.PlatformHalfWidthKilometres = 0.272;

		// Its four pads, where the build script puts them: fourteen metres out along each diagonal from
		// a dock at a corner, twelve across.
		for (const FVector2D Corner : {FVector2D(1, 1), FVector2D(-1, 1), FVector2D(-1, -1), FVector2D(1, -1)})
		{
			FSpaceMMOAirspaceBerth Berth;
			Berth.Pad = At(Zone, FVector(Corner.X * 0.2699, Corner.Y * 0.2699, 0.0));
			Berth.PadRadiusKilometres = 0.012;

			Zone.Berths.Add(Berth);
		}

		return Zone;
	}

	/** A point given in the city's own frame -- across, across, up from its floor -- in system axes. */
	FSystemCoordinate At(const FSpaceMMOAirspaceZone& Zone, const FVector& LocalKilometres)
	{
		return FSystemCoordinate(
			Zone.Origin.Kilometres
			+ Zone.Rotation.RotateVector(
				FVector(LocalKilometres.X, LocalKilometres.Y, LocalKilometres.Z + Zone.FloorKilometres)));
	}

	FVector Up(const FSpaceMMOAirspaceZone& Zone)
	{
		return Zone.Rotation.RotateVector(FVector::UpVector);
	}

	/** A ship's hull, as SpaceMMOShipPawn.h has it. */
	constexpr double Hull = 0.002;

	/** Just past an edge: clear of it, and by no more than a centimetre. */
	bool JustPast(const double Value, const double Edge)
	{
		return Value > Edge && Value < Edge + 0.00001;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOAirspaceNoFlyIsACylinderTest,
	"SpaceMMO.Airspace.NoFlyIsACylinderOverTheCity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOAirspaceNoFlyIsACylinderTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMOAirspaceTests;

	const FSpaceMMOAirspaceZone Zone = Borlash();
	const double Radius = Zone.NoFlyRadiusKilometres;
	const double Ceiling = Zone.NoFlyCeilingKilometres;

	// The round-trip first, because every other assertion here leans on it.
	const FVector Local(0.1, -0.05, 0.4);
	TestTrue(TEXT("A point placed in the city's frame reads back where it was put"),
		FSpaceMMOAirspace::ToLocal(Zone, At(Zone, Local)).Equals(Local, 1e-9));

	TestTrue(TEXT("Over the square, low down, is closed"),
		FSpaceMMOAirspace::IsInNoFly(Zone, At(Zone, FVector(0.0, 0.0, 0.05)), Hull));

	TestTrue(TEXT("Just inside the edge, just under the top, is closed"),
		FSpaceMMOAirspace::IsInNoFly(Zone, At(Zone, FVector(0.0, Radius * 0.95, Ceiling * 0.95)), Hull));

	TestFalse(TEXT("Beyond the edge is open"),
		FSpaceMMOAirspace::IsInNoFly(Zone, At(Zone, FVector(Radius * 1.1, 0.0, 0.05)), Hull));

	TestFalse(TEXT("Above the top is open"),
		FSpaceMMOAirspace::IsInNoFly(Zone, At(Zone, FVector(0.0, 0.0, Ceiling * 1.1)), Hull));

	// The city's corners are outside the cylinder: the docks stand there, and ships must reach them.
	TestFalse(TEXT("A corner dock is open air"),
		FSpaceMMOAirspace::IsInNoFly(Zone, At(Zone, FVector(0.27, 0.27, 0.01)), Hull));

	// A ship is its hull, not its centre: one whose middle is outside but whose side is in is in.
	TestTrue(TEXT("A hull overlapping the edge is in"),
		FSpaceMMOAirspace::IsInNoFly(Zone, At(Zone, FVector(Radius + Hull * 0.5, 0.0, 0.05)), Hull));

	// Measured along the city's up, not world Z: the same height above the floor straight up along
	// system Z would be a different point entirely, and one well clear of the cylinder.
	TestFalse(TEXT("World Z is not up here"),
		FSpaceMMOAirspace::IsInNoFly(
			Zone, FSystemCoordinate(Zone.Origin.Kilometres + FVector(0.0, 0.0, 1.0)), Hull));

	FSpaceMMOAirspaceZone Open = Zone;
	Open.NoFlyRadiusKilometres = 0.0;
	TestFalse(TEXT("A zone with no radius closes nothing"),
		FSpaceMMOAirspace::IsInNoFly(Open, At(Open, FVector(0.0, 0.0, 0.05)), Hull));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOAirspaceComesOutTheWayItWentInTest,
	"SpaceMMO.Airspace.ShipComesOutTheWayItWentIn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOAirspaceComesOutTheWayItWentInTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMOAirspaceTests;

	const FSpaceMMOAirspaceZone Zone = Borlash();
	const FVector ZoneUp = Up(Zone);
	const double Radius = Zone.NoFlyRadiusKilometres;
	const double Ceiling = Zone.NoFlyCeilingKilometres;

	// Descending onto the city from above, a metre through the top, drifting sideways as it comes.
	{
		FSystemCoordinate Position = At(Zone, FVector(0.05, 0.02, Ceiling - 0.001));
		const FVector Sideways = Zone.Rotation.RotateVector(FVector(0.0, 3000.0, 0.0));
		FVector Velocity = Sideways - (ZoneUp * 5000.0);

		TestTrue(TEXT("A ship through the top is moved"),
			FSpaceMMOAirspace::ResolveNoFly(Zone, Hull, Position, Velocity));

		const FVector After = FSpaceMMOAirspace::ToLocal(Zone, Position);

		TestTrue(TEXT("It goes back up, to just past the top plus its hull"),
			JustPast(After.Z, Ceiling + Hull));
		TestTrue(TEXT("It is not shoved sideways"),
			FVector2D(After.X, After.Y).Equals(FVector2D(0.05, 0.02), 1e-9));
		TestTrue(TEXT("Its descent is stopped"),
			FMath::Abs(FVector::DotProduct(Velocity, ZoneUp)) < 1e-6);
		TestTrue(TEXT("Its drift is kept, so it slides over the top rather than sticking"),
			Velocity.Equals(Sideways, 1e-6));
		TestFalse(TEXT("And it is out"), FSpaceMMOAirspace::IsInNoFly(Zone, Position, Hull));
	}

	// Flying low into the side, a metre through the wall of air, with some speed along it.
	{
		FSystemCoordinate Position = At(Zone, FVector(Radius - 0.001, 0.0, 0.03));
		const FVector Along = Zone.Rotation.RotateVector(FVector(0.0, 2000.0, 0.0));
		FVector Velocity = Along + Zone.Rotation.RotateVector(FVector(-4000.0, 0.0, 0.0));

		TestTrue(TEXT("A ship through the side is moved"),
			FSpaceMMOAirspace::ResolveNoFly(Zone, Hull, Position, Velocity));

		const FVector After = FSpaceMMOAirspace::ToLocal(Zone, Position);

		TestTrue(TEXT("It goes back out sideways, to just past the edge plus its hull"),
			JustPast(After.X, Radius + Hull) && FMath::Abs(After.Y) < 1e-9);
		TestFalse(TEXT("And it is out"), FSpaceMMOAirspace::IsInNoFly(Zone, Position, Hull));
		TestTrue(TEXT("At the height it was flying"),
			FMath::IsNearlyEqual(After.Z, 0.03, 1e-9));
		TestTrue(TEXT("Its inward speed is gone and its speed along the edge kept"),
			Velocity.Equals(Along, 1e-6));
	}

	// Leaving already: inside, but heading out. Nothing of that should be taken away.
	{
		FSystemCoordinate Position = At(Zone, FVector(Radius - 0.001, 0.0, 0.03));
		const FVector Outward = Zone.Rotation.RotateVector(FVector(4000.0, 0.0, 0.0));
		FVector Velocity = Outward;

		FSpaceMMOAirspace::ResolveNoFly(Zone, Hull, Position, Velocity);

		TestTrue(TEXT("A ship already leaving keeps all its speed"), Velocity.Equals(Outward, 1e-6));
	}

	// On the axis there is no "out" sideways. It must still leave, and somewhere finite.
	{
		FSystemCoordinate Position = At(Zone, FVector(0.0, 0.0, 0.1));
		FVector Velocity = FVector::ZeroVector;

		FSpaceMMOAirspace::ResolveNoFly(Zone, Hull, Position, Velocity);

		TestFalse(TEXT("A ship on the axis is still put outside"),
			FSpaceMMOAirspace::IsInNoFly(Zone, Position, Hull) || Position.Kilometres.ContainsNaN());
	}

	// And outside, nothing happens at all.
	{
		const FSystemCoordinate Start = At(Zone, FVector(0.4, 0.0, 0.03));
		FSystemCoordinate Position = Start;
		FVector Velocity = Zone.Rotation.RotateVector(FVector(-4000.0, 0.0, 0.0));
		const FVector Before = Velocity;

		TestFalse(TEXT("A ship outside is not moved"),
			FSpaceMMOAirspace::ResolveNoFly(Zone, Hull, Position, Velocity));
		TestTrue(TEXT("Nor slowed"),
			Position.Kilometres.Equals(Start.Kilometres, 0.0) && Velocity.Equals(Before, 0.0));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOAirspaceNoStepOutOverThePlatformTest,
	"SpaceMMO.Airspace.NoStepOutOverThePlatform",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOAirspaceNoStepOutOverThePlatformTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMOAirspaceTests;

	const FSpaceMMOAirspaceZone Zone = Borlash();
	const double Half = Zone.PlatformHalfWidthKilometres;

	TestFalse(TEXT("Not in the middle of the city"),
		FSpaceMMOAirspace::AllowsStepOut(Zone, At(Zone, FVector(0.05, 0.05, 0.002))));

	// Outside the no-fly cylinder, so a ship can be here -- but still over the platform. A pilot at
	// a dock docks; they do not leave the ship standing on the pad (Joe, 2 October).
	TestFalse(TEXT("Not at a corner dock, which is still the platform"),
		FSpaceMMOAirspace::AllowsStepOut(Zone, At(Zone, FVector(Half * 0.95, Half * 0.95, 0.002))));

	TestTrue(TEXT("Off the platform's edge, on open ground, is allowed"),
		FSpaceMMOAirspace::AllowsStepOut(Zone, At(Zone, FVector(Half * 1.05, 0.0, 0.002))));

	TestTrue(TEXT("Past a corner is allowed"),
		FSpaceMMOAirspace::AllowsStepOut(Zone, At(Zone, FVector(-Half * 1.05, -Half * 1.05, 0.002))));

	// A square, not a circle: midway along an edge is as far out as the corner is diagonally.
	TestFalse(TEXT("The platform is square"),
		FSpaceMMOAirspace::AllowsStepOut(Zone, At(Zone, FVector(Half * 0.7, -Half * 0.7, 0.002))));

	FSpaceMMOAirspaceZone NoPlatform = Zone;
	NoPlatform.PlatformHalfWidthKilometres = 0.0;
	TestTrue(TEXT("A zone with no platform refuses nobody"),
		FSpaceMMOAirspace::AllowsStepOut(NoPlatform, At(NoPlatform, FVector(0.0, 0.0, 0.002))));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOAirspaceBerthIsThePadTest,
	"SpaceMMO.Airspace.BerthIsThePadNotTheCity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOAirspaceBerthIsThePadTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMOAirspaceTests;

	const FSpaceMMOAirspaceZone Zone = Borlash();
	const FVector ZoneUp = Up(Zone);

	// Borlash's north-east pad, roughly: fourteen metres out along the diagonal from a dock at the
	// corner, twelve across.
	const FSystemCoordinate Pad = At(Zone, FVector(0.27, 0.27, 0.0));
	const double PadRadius = 0.012;
	const FVector Across = Zone.Rotation.RotateVector(FVector(1.0, 0.0, 0.0));

	auto Ship = [&](const double AcrossKm, const double UpKm)
	{
		return FSystemCoordinate(Pad.Kilometres + (Across * AcrossKm) + (ZoneUp * UpKm));
	};

	TestTrue(TEXT("Resting on the pad is at the berth"),
		FSpaceMMOAirspace::IsAtBerth(Pad, ZoneUp, PadRadius, Ship(0.0, 0.002)));

	TestTrue(TEXT("Overhanging its edge onto the dock's rim is at the berth"),
		FSpaceMMOAirspace::IsAtBerth(Pad, ZoneUp, PadRadius,
			Ship(PadRadius + FSpaceMMOAirspace::BerthReachKilometres * 0.5, 0.002)));

	TestFalse(TEXT("Past the rim is not"),
		FSpaceMMOAirspace::IsAtBerth(Pad, ZoneUp, PadRadius,
			Ship(PadRadius + FSpaceMMOAirspace::BerthReachKilometres * 1.5, 0.002)));

	TestTrue(TEXT("Coming in to land above it is at the berth"),
		FSpaceMMOAirspace::IsAtBerth(Pad, ZoneUp, PadRadius,
			Ship(0.0, FSpaceMMOAirspace::BerthHeightKilometres * 0.9)));

	TestFalse(TEXT("High overhead is not"),
		FSpaceMMOAirspace::IsAtBerth(Pad, ZoneUp, PadRadius,
			Ship(0.0, FSpaceMMOAirspace::BerthHeightKilometres * 1.5)));

	// Under the platform is not a berth either. Height is measured along the city's up, so a check
	// done along world Z, or with the sign dropped, lets this one through.
	TestFalse(TEXT("Underneath it is not"),
		FSpaceMMOAirspace::IsAtBerth(Pad, ZoneUp, PadRadius,
			Ship(0.0, -FSpaceMMOAirspace::BerthHeightKilometres * 0.5)));

	// And the city itself, which is what this exists to refuse.
	TestFalse(TEXT("Over the city square is not a berth"),
		FSpaceMMOAirspace::IsAtBerth(Pad, ZoneUp, PadRadius, At(Zone, FVector(0.0, 0.0, 0.002))));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOAirspacePilotSetDownOnTheDockTest,
	"SpaceMMO.Airspace.PilotIsSetDownOnTheDock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOAirspacePilotSetDownOnTheDockTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMOAirspaceTests;

	const FSpaceMMOAirspaceZone Zone = Borlash();
	const FVector ZoneUp = Up(Zone);

	const FSystemCoordinate Pad = At(Zone, FVector(0.27, 0.27, 0.0));
	const FVector Outward = Zone.Rotation.RotateVector(FVector(1.0, 1.0, 0.0).GetSafeNormal());
	const double PadRadius = 0.012;

	const FSystemCoordinate Arrival =
		FSpaceMMOAirspace::PilotArrivalBesidePad(Pad, Outward, ZoneUp, PadRadius);

	const FVector LocalPad = FSpaceMMOAirspace::ToLocal(Zone, Pad);
	const FVector LocalArrival = FSpaceMMOAirspace::ToLocal(Zone, Arrival);

	// Inboard: between the pad and the city, on the dock -- not off the platform's corner, which is
	// open air above a drop.
	TestTrue(TEXT("The pilot is set down on the city side of the pad"),
		FVector2D(LocalArrival.X, LocalArrival.Y).Size() < FVector2D(LocalPad.X, LocalPad.Y).Size());

	// Off the pad, so not standing where their own ship would be put.
	const double FromPad = FVector2D(LocalArrival.X - LocalPad.X, LocalArrival.Y - LocalPad.Y).Size();
	TestTrue(TEXT("Just clear of the pad's edge"), FromPad > PadRadius && FromPad < PadRadius + 0.003);

	// Above the floor, so the character settles onto it rather than starting inside it.
	const double Lift = LocalArrival.Z - LocalPad.Z;
	TestTrue(TEXT("A little above the dock's floor"), Lift > 0.0 && Lift < 0.002);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOAirspaceMessagesTest,
	"SpaceMMO.Airspace.MessagesReadAsApproved",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOAirspaceMessagesTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMOAirspaceTests;

	// Word for word, because Joe approved these words on 2 October (task 169) and a change to them is
	// a change to the interface: it goes back to him before it goes in, and this is what notices.
	TestEqual(TEXT("Flying into the airspace"),
		FSpaceMMOAirspace::ClosedAirspaceMessage(Borlash()),
		FString(TEXT("Borlash's airspace is closed. Dock at one of its four corner docking stations.")));

	TestEqual(TEXT("Pressing dock away from a dock"),
		FSpaceMMOAirspace::DockAtBerthsMessage(TEXT("Borlash"), 4),
		FString(TEXT("Ships dock at Borlash's four corner docking stations, not over the city.")));

	TestEqual(TEXT("Stepping out over the city"),
		FSpaceMMOAirspace::NoStepOutMessage(TEXT("G")),
		FString(TEXT("You can't step out here. Land at a docking station and press G to dock.")));

	TestEqual(TEXT("Stepping out on a landing pad"),
		FSpaceMMOAirspace::PadStepOutMessage(TEXT("G")),
		FString(TEXT("Ships can't be left on a landing pad. Press G to dock.")));

	TestEqual(TEXT("Counts past the words are numbers"), FSpaceMMOAirspace::CountWord(12), FString(TEXT("12")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOAirspaceStepOutRefusalTest,
	"SpaceMMO.Airspace.StepOutRefusalSaysWhereYouAre",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOAirspaceStepOutRefusalTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMOAirspaceTests;

	const FSpaceMMOAirspaceZone Zone = Borlash();
	const FVector ZoneUp = Up(Zone);

	TestEqual(TEXT("Every pad counts as four"), Zone.Berths.Num(), 4);

	// On each pad, resting: told ships are not left there, not sent to a docking station it is on.
	for (const FSpaceMMOAirspaceBerth& Berth : Zone.Berths)
	{
		const FSystemCoordinate Resting(Berth.Pad.Kilometres + ZoneUp * Hull);

		TestTrue(TEXT("A ship resting on a pad is at a berth"), FSpaceMMOAirspace::IsAtAnyBerth(Zone, Resting));
		TestEqual(TEXT("On a pad, the pad sentence"),
			FSpaceMMOAirspace::StepOutRefusal(Zone, Resting, TEXT("G")),
			FSpaceMMOAirspace::PadStepOutMessage(TEXT("G")));
	}

	// Over the platform between docks, and over the square: the sentence that says where to go.
	for (const FVector Local : {FVector(0.26, 0.0, 0.002), FVector(0.05, 0.05, 0.002)})
	{
		const FSystemCoordinate Over = At(Zone, Local);

		TestFalse(TEXT("Off the pads is not a berth"), FSpaceMMOAirspace::IsAtAnyBerth(Zone, Over));
		TestEqual(TEXT("Off the pads, the over-the-city sentence"),
			FSpaceMMOAirspace::StepOutRefusal(Zone, Over, TEXT("G")),
			FSpaceMMOAirspace::NoStepOutMessage(TEXT("G")));
	}

	// Measured along the city's up: a ship high above a pad is not resting on it.
	const FSystemCoordinate HighAbove(Zone.Berths[0].Pad.Kilometres + ZoneUp * 0.5);
	TestFalse(TEXT("Half a kilometre over a pad is not on it"), FSpaceMMOAirspace::IsAtAnyBerth(Zone, HighAbove));

	return true;
}

#endif
