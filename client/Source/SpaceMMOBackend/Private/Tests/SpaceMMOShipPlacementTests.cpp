#include "Misc/AutomationTest.h"

#include "SpaceMMOBackendTypes.h"
#include "SpaceMMOPlayerController.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FBackendActiveShip Hull(const int64 Id = 5)
	{
		FBackendActiveShip Ship;

		Ship.HullItemInstanceId = Id;
		Ship.Name = TEXT("Shuttle");

		return Ship;
	}
}

/**
 * What happens to a hull when the backend describes it, decided without a world.
 *
 * <strong>These exist because the impure version was wrong and nothing caught it.</strong> Task 155
 * made summoning deliberately not record a position — the simulation places the pawn and then says
 * where it went — and then gated placement on the position already existing. The two rules are
 * circular: nothing could ever be placed. The summon reported success, said the ship was waiting
 * outside, and produced no ship, twice, on two separate playtests.
 *
 * Placement itself needs a world, a station actor and terrain, and cannot run headless. The
 * <em>decision</em> is four branches over a struct, which is exactly the part that was wrong.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOShipPlacementSummonBringsItOutTest,
	"SpaceMMO.Ships.PlacementSummonBringsItOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOShipPlacementSummonBringsItOutTest::RunTest(const FString& Parameters)
{
	// The exact state a freshly summoned hull is in: moved into this station's hangar, with no
	// position, because nothing has placed a pawn for it yet.
	FBackendActiveShip Summoned = Hull();
	Summoned.StationId = 1;
	Summoned.bDeployed = false;

	TestEqual(
		TEXT("Summoning a hull in a hangar brings it out beside the station"),
		ASpaceMMOPlayerController::DecideShipPlacement(Summoned, true),
		ESpaceMMOShipPlacement::BesideStation);

	// The same row, on a sign-in rather than a summon. Nothing was standing anywhere, so nothing is
	// put back -- this is what stops a docked ship reappearing outside after a restart.
	TestEqual(
		TEXT("...and signing in leaves it in the hangar"),
		ASpaceMMOPlayerController::DecideShipPlacement(Summoned, false),
		ESpaceMMOShipPlacement::Nothing);

	// A hull crated in a hold has no station to stand beside, so even a summon can do nothing.
	FBackendActiveShip Crated = Hull();
	Crated.StationId = 0;

	TestEqual(
		TEXT("A hull in a hold has nowhere to be summoned to"),
		ASpaceMMOPlayerController::DecideShipPlacement(Crated, true),
		ESpaceMMOShipPlacement::Nothing);

	return true;
}

/**
 * A ship left standing somewhere comes back there, and one being flown is left alone.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOShipPlacementRestoresWhereItStoodTest,
	"SpaceMMO.Ships.PlacementRestoresWhereItStood",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOShipPlacementRestoresWhereItStoodTest::RunTest(const FString& Parameters)
{
	// Landed on a hillside, stepped out of, and logged off beside. Its hangar is still the station
	// it was summoned from, which is exactly why the station cannot be what decides this.
	FBackendActiveShip Parked = Hull();
	Parked.StationId = 1;
	Parked.bDeployed = true;
	Parked.PositionKilometres = FVector(-18.25, 4.5, -3.75);

	TestEqual(
		TEXT("A ship left standing comes back where it stood"),
		ASpaceMMOPlayerController::DecideShipPlacement(Parked, false),
		ESpaceMMOShipPlacement::AtRecordedPosition);

	// Asking for it while it is out there recalls it, rather than leaving it on the hillside. The
	// backend has already moved its hangar to the station being stood at by the time this is asked.
	TestEqual(
		TEXT("...and summoning it recalls it to the station instead"),
		ASpaceMMOPlayerController::DecideShipPlacement(Parked, true),
		ESpaceMMOShipPlacement::BesideStation);

	// Being flown wins over everything: the pawn already exists, and placing a second one is what
	// put two ships at the dock (task 152).
	FBackendActiveShip Flying = Hull();
	Flying.StationId = 1;
	Flying.bAboard = true;
	Flying.bDeployed = true;

	TestEqual(
		TEXT("A ship being flown is adopted, never placed again"),
		ASpaceMMOPlayerController::DecideShipPlacement(Flying, false),
		ESpaceMMOShipPlacement::AdoptFlyingPawn);

	TestEqual(
		TEXT("...even when a summon arrives for it"),
		ASpaceMMOPlayerController::DecideShipPlacement(Flying, true),
		ESpaceMMOShipPlacement::AdoptFlyingPawn);

	// Owning no ship at all, which is most of the opening.
	FBackendActiveShip None;

	TestEqual(
		TEXT("No hull means nothing to place"),
		ASpaceMMOPlayerController::DecideShipPlacement(None, true),
		ESpaceMMOShipPlacement::Nothing);

	return true;
}

#endif
