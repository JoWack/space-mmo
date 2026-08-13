#include "Misc/AutomationTest.h"
#include "SpaceMMOFlightReadout.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Tests for the flight readout's wording.
 *
 * The formatter is a pure function for exactly this reason: a HUD is otherwise only checkable by
 * looking at it, and the parts worth checking here are not visual — which altitude is shown, what
 * happens with no planet to orbit, and whether the debug figures stay out of the pilot's line.
 */

namespace
{
	FSpaceMMOFlightReadoutInputs LandedOnTheCapital()
	{
		FSpaceMMOFlightReadoutInputs Inputs;
		Inputs.SystemPosition = FSystemCoordinate(FVector(39.685, -1.002, -0.039));
		Inputs.SpeedCentimetresPerSecond = 18400.0;
		Inputs.OrbitalSpeedCentimetresPerSecond = 44300.0;
		Inputs.GroundAltitudeKilometres = 0.0;
		Inputs.SphereAltitudeKilometres = 0.34;
		Inputs.Proximity = EPlanetProximity::Surface;
		Inputs.WorldLocationCentimetres = FVector(168529.8, -100152.3, -3854.3);
		Inputs.RebaseCount = 0;

		return Inputs;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOFlightReadoutSpeaksMetresTest,
	"SpaceMMO.HUD.FlightReadoutSpeaksMetres",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOFlightReadoutSpeaksMetresTest::RunTest(const FString& Parameters)
{
	const FSpaceMMOFlightReadoutText Text =
		USpaceMMOFlightReadout::Build(LandedOnTheCapital());

	// 18,400 cm/s is 184 m/s. It read "0.184 km/s" before, which is not a number anybody compares
	// against anything at a glance -- and comparing it against orbital speed is why it is on screen.
	TestEqual(TEXT("Speed in metres per second"), Text.Speed, FString(TEXT("184 m/s")));
	// Bare numbers, because labels live in the Widget Blueprint where they can be reworded and
	// restyled without a rebuild. C++ says what the value is; the designer says what it is called.
	TestEqual(TEXT("Orbital alongside it"), Text.Orbital, FString(TEXT("443 m/s")));
	TestTrue(TEXT("And it means something here"), Text.bHasOrbital);

	// FSystemCoordinate::ToString already ends in "km"; adding another produced "km km" on screen.
	TestEqual(
		TEXT("System position is not doubly united"),
		Text.SystemPosition,
		FString(TEXT("(39.685, -1.002, -0.039) km")));

	TestEqual(TEXT("Proximity band"), Text.Proximity, FString(TEXT("SURFACE")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOFlightReadoutShowsHeightAboveGroundTest,
	"SpaceMMO.HUD.FlightReadoutShowsHeightAboveGround",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOFlightReadoutShowsHeightAboveGroundTest::RunTest(const FString& Parameters)
{
	// Landed: zero above the ground while sitting 0.34 km above the sphere the ground stands on.
	// The pilot's altitude is the first one, and showing the second is what made
	// "Altitude 0.34 km | SURFACE" look like a contradiction while both halves were true.
	const FSpaceMMOFlightReadoutText Landed =
		USpaceMMOFlightReadout::Build(LandedOnTheCapital());

	TestEqual(TEXT("Landed reads zero"), Landed.Altitude, FString(TEXT("0 m")));

	TestFalse(
		TEXT("The sphere figure is not in the pilot's line"),
		Landed.Altitude.Contains(TEXT("0.34")));

	TestTrue(TEXT("It is in the debug line"), Landed.Debug.Contains(TEXT("0.34")));

	// Metres near the ground, kilometres once away from it: landing happens in the last few metres
	// and "0.00 km" cannot show it, while a transfer happens over tens of kilometres and "48000 m"
	// is not read as a distance.
	FSpaceMMOFlightReadoutInputs Climbing = LandedOnTheCapital();
	Climbing.GroundAltitudeKilometres = 0.4;

	TestEqual(TEXT("Below a kilometre, metres"),
		USpaceMMOFlightReadout::Build(Climbing).Altitude, FString(TEXT("400 m")));

	Climbing.GroundAltitudeKilometres = 48.0;

	TestEqual(TEXT("Above a kilometre, kilometres"),
		USpaceMMOFlightReadout::Build(Climbing).Altitude, FString(TEXT("48.00 km")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOFlightReadoutSaysNothingAboutOrbitingNothingTest,
	"SpaceMMO.HUD.FlightReadoutSaysNothingAboutOrbitingNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOFlightReadoutSaysNothingAboutOrbitingNothingTest::RunTest(const FString& Parameters)
{
	// Deep space, between bodies. "orbital 0 m/s" would read as a ship already in orbit of nothing.
	FSpaceMMOFlightReadoutInputs Adrift = LandedOnTheCapital();
	Adrift.OrbitalSpeedCentimetresPerSecond = 0.0;
	Adrift.Proximity = EPlanetProximity::Orbital;

	const FSpaceMMOFlightReadoutText Text = USpaceMMOFlightReadout::Build(Adrift);

	TestTrue(TEXT("Orbital speed is omitted entirely"), Text.Orbital.IsEmpty());

	// The flag a Blueprint binds a label's visibility to, so nothing is left hanging over an empty
	// value.
	TestFalse(TEXT("And says so"), Text.bHasOrbital);
	TestEqual(TEXT("Speed still reported"), Text.Speed, FString(TEXT("184 m/s")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
