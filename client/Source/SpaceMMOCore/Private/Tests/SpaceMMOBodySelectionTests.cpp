#include "SpaceMMOBodySelection.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Tests for choosing which body a position belongs to.
 *
 * <strong>These exist because the rule they test did not.</strong> Ten lookups across the pawns,
 * the station actor and the deposit subsystem took the first planet the actor iterator returned and
 * called it "the planet", which was right only because the scene held exactly one. Two of them
 * carried comments describing a nearest-planet rule that was nowhere in the project (task 157).
 *
 * The shipped system draws every body at one radius, so a test written against equal bodies would
 * pass with either rule and constrain nothing. The bodies below are deliberately different sizes.
 */

namespace
{
	FPlanetConfig BodyAt(const FVector& Centre, const double RadiusKilometres)
	{
		FPlanetConfig Body;
		Body.Centre = FSystemCoordinate(Centre);
		Body.RadiusKilometres = RadiusKilometres;

		return Body;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOBodySelectionNearestTest,
	"SpaceMMO.Bodies.NearestIsTheOneUnderfoot",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOBodySelectionNearestTest::RunTest(const FString& Parameters)
{
	// Two worlds at the distances the shipped content authors: a hundred and ninety kilometres
	// apart, both drawn at 20 km.
	const TArray<FPlanetConfig> Bodies =
	{
		BodyAt(FVector(60.0, 0.0, 0.0), 20.0),
		BodyAt(FVector(-120.0, 60.0, 0.0), 20.0),
	};

	// Standing on the first: 20 km from its centre, which is its surface.
	TestEqual(
		TEXT("The body underfoot is the near one"),
		FSpaceMMOBodySelection::IndexOfNearest(Bodies, FSystemCoordinate(80.0, 0.0, 0.0)),
		0);

	// And on the second.
	TestEqual(
		TEXT("The body underfoot is the other one when standing there"),
		FSpaceMMOBodySelection::IndexOfNearest(Bodies, FSystemCoordinate(-100.0, 60.0, 0.0)),
		1);

	// Nothing to choose from is not zero. INDEX_NONE is what every caller checks, and returning
	// the first index of an empty array would index off the end of it.
	TestEqual(
		TEXT("No bodies at all"),
		FSpaceMMOBodySelection::IndexOfNearest({}, FSystemCoordinate(0.0, 0.0, 0.0)),
		INDEX_NONE);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOBodySelectionSurfaceTest,
	"SpaceMMO.Bodies.NearestIsBySurfaceNotCentre",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOBodySelectionSurfaceTest::RunTest(const FString& Parameters)
{
	// The case that tells the two rules apart: a planet with a moon.
	//
	// Standing on the planet, the moon's *centre* is nearer than the planet's centre -- it has to
	// be, because the planet's centre is a planet-radius away and the moon is not. So a
	// centre-distance rule says the character is on the moon while the ground under their feet
	// belongs to the planet. Surface distance gets it right.
	//
	// Every body is drawn at 20 km today, so this configuration cannot occur in the shipped scene.
	// It is here because a test using equal bodies passes against either rule and proves nothing,
	// and because the ship pawn already sums gravity over every body specifically so that a planet
	// and its moon work.
	const TArray<FPlanetConfig> Bodies =
	{
		BodyAt(FVector(0.0, 0.0, 0.0), 400.0),
		BodyAt(FVector(450.0, 0.0, 0.0), 10.0),
	};

	// On the planet's surface, on the side facing the moon.
	const FSystemCoordinate Standing(400.0, 0.0, 0.0);

	// Stated rather than assumed, so this fails loudly if somebody edits the numbers above into a
	// configuration that no longer discriminates. It has already earned that: the first version of
	// this test had the geometry backwards and both rules agreed.
	const double ToPlanetCentre = (Standing.Kilometres - Bodies[0].Centre.Kilometres).Size();
	const double ToMoonCentre = (Standing.Kilometres - Bodies[1].Centre.Kilometres).Size();

	TestTrue(
		TEXT("The moon's centre really is nearer, or this proves nothing"),
		ToMoonCentre < ToPlanetCentre);

	TestEqual(
		TEXT("The body underfoot is the planet, by surface"),
		FSpaceMMOBodySelection::IndexOfNearest(Bodies, Standing),
		0);

	// And the altitudes it decided on, so a failure says which way it was wrong.
	TestTrue(
		TEXT("Standing on the planet reads as zero altitude"),
		FMath::IsNearlyZero(FSpaceMMOBodySelection::AltitudeAbove(Bodies[0], Standing), 1e-9));

	TestTrue(
		TEXT("And well above the moon"),
		FSpaceMMOBodySelection::AltitudeAbove(Bodies[1], Standing) > 30.0);

	// The moon is still the answer when standing on it, or the rule would simply be "the biggest".
	TestEqual(
		TEXT("The body underfoot is the moon when standing on the moon"),
		FSpaceMMOBodySelection::IndexOfNearest(Bodies, FSystemCoordinate(460.0, 0.0, 0.0)),
		1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOBodySelectionInsideTest,
	"SpaceMMO.Bodies.InsideABodyIsThatBody",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOBodySelectionInsideTest::RunTest(const FString& Parameters)
{
	// Below the surface is a state the ship reaches every time it lands slightly into a hill, and
	// the answer has to stay the body it is inside rather than flipping to a neighbour. A rule
	// using absolute distance from the surface would do exactly that.
	const TArray<FPlanetConfig> Bodies =
	{
		BodyAt(FVector(0.0, 0.0, 0.0), 20.0),
		BodyAt(FVector(45.0, 0.0, 0.0), 20.0),
	};

	// Half a kilometre under the first body's surface, on the side facing the second.
	const FSystemCoordinate Buried(19.5, 0.0, 0.0);

	TestEqual(
		TEXT("Underground still belongs to the body it is underground in"),
		FSpaceMMOBodySelection::IndexOfNearest(Bodies, Buried),
		0);

	TestTrue(
		TEXT("And its altitude there is negative"),
		FSpaceMMOBodySelection::AltitudeAbove(Bodies[0], Buried) < 0.0);

	return true;
}

#endif
