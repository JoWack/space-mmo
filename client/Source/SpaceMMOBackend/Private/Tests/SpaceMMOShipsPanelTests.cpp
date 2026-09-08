#include "Misc/AutomationTest.h"

#include "SpaceMMOBackendTypes.h"
#include "SpaceMMOStationOverlay.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FBackendItemInstance Hull(
		const int64 Id,
		const TCHAR* Name,
		const int32 StationId,
		const bool bDeployed = false,
		const int32 Condition = 100)
	{
		FBackendItemInstance Instance;

		Instance.Id = Id;
		Instance.Name = Name;
		Instance.StationId = StationId;
		Instance.Condition = Condition;
		Instance.Category = EBackendItemCategory::Hull;
		Instance.Kind = EBackendInventoryKind::StationHangar;

		// <strong>Defaulting to false is deliberate.</strong> A hull sitting in a hangar row is put
		// away, and the pre-155 tests all assumed the opposite without ever saying so -- which is
		// exactly the assumption that broke when docking started removing pawns.
		Instance.bDeployed = bDeployed;

		return Instance;
	}
}

/**
 * The Ships tab lists hulls, and only hulls.
 *
 * <strong>By category, never by key.</strong> `hull_shuttle` and `shuttle_hull_section` are one
 * prefix match away from listing a component as a ship, and both are already shipped — which is why
 * the wire carries a category at all.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOShipsPanelListsHullsTest,
	"SpaceMMO.Ships.PanelListsHulls",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOShipsPanelListsHullsTest::RunTest(const FString& Parameters)
{
	FBackendItemInstance Section;
	Section.Id = 9;
	Section.Name = TEXT("Shuttle Hull Section");
	Section.Category = EBackendItemCategory::Component;

	FBackendItemInstance Laser;
	Laser.Id = 10;
	Laser.Name = TEXT("Crude Mining Laser");
	Laser.Category = EBackendItemCategory::Tool;

	const TArray<FBackendItemInstance> Owned = { Section, Hull(1, TEXT("Shuttle"), 5), Laser };

	const TArray<FSpaceMMOShipRowText> Rows =
		USpaceMMOStationOverlay::BuildShipRows(Owned, 5, true, 0, false);

	TestEqual(TEXT("Only the hull is a ship"), Rows.Num(), 1);
	TestEqual(TEXT("...and it is the shuttle"), Rows[0].Name, FString(TEXT("Shuttle")));
	TestEqual(TEXT("Condition is worded"), Rows[0].Condition, FString(TEXT("100%")));

	return true;
}

/**
 * A row says where a hull is, and why the button is off.
 *
 * <strong>A reason rather than a disabled control.</strong> "Summon", greyed, with nothing beside it
 * is the interface telling somebody they are wrong without saying about what — and the two reasons
 * are not equivalent: one sends a player walking somewhere and the other means there is nothing to
 * do.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOShipsPanelSaysWhyNotTest,
	"SpaceMMO.Ships.PanelSaysWhyNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOShipsPanelSaysWhyNotTest::RunTest(const FString& Parameters)
{
	// Docked at station 5, where the hull's hangar is, and it has been put away.
	const TArray<FBackendItemInstance> Stowed = { Hull(1, TEXT("Shuttle"), 5) };

	// <strong>The case task 155 exists for, and the one that was wrong.</strong> A ship you have
	// just docked is in this station's hangar with no pawn outside, and the panel used to read the
	// station alone and answer "Already here" -- refusing the only control that could bring it back.
	const TArray<FSpaceMMOShipRowText> InHangar =
		USpaceMMOStationOverlay::BuildShipRows(Stowed, 5, true, 1, false);

	TestTrue(TEXT("The active ship is marked"), InHangar[0].bIsActive);
	TestTrue(TEXT("A ship in the hangar can be summoned out of it"), InHangar[0].bCanSummon);
	TestTrue(TEXT("...with no reason to show"), InHangar[0].Refusal.IsEmpty());
	TestEqual(TEXT("...and it says where it is"), InHangar[0].Where, FString(TEXT("In the hangar")));

	// <strong>Even at a market.</strong> Fetching back what you parked here is not the same act as
	// having one brought, and a gate that refused would leave a player on foot at a trading hub with
	// their only ship locked in the building in front of them.
	const TArray<FSpaceMMOShipRowText> HangarAtMarket =
		USpaceMMOStationOverlay::BuildShipRows(Stowed, 5, false, 1, false);

	TestTrue(
		TEXT("A ship parked at a market comes back out of that market"),
		HangarAtMarket[0].bCanSummon);

	// The same hull, standing outside this station.
	const TArray<FBackendItemInstance> Outside = { Hull(1, TEXT("Shuttle"), 5, true) };

	const TArray<FSpaceMMOShipRowText> OnTheApron =
		USpaceMMOStationOverlay::BuildShipRows(Outside, 5, true, 1, false);

	TestFalse(TEXT("A ship already outside summons nothing"), OnTheApron[0].bCanSummon);
	TestEqual(TEXT("...and says why"), OnTheApron[0].Refusal, FString(TEXT("Already out")));
	TestEqual(TEXT("...and where"), OnTheApron[0].Where, FString(TEXT("Outside")));

	// Sitting in it.
	const TArray<FSpaceMMOShipRowText> Flying =
		USpaceMMOStationOverlay::BuildShipRows(Outside, 5, true, 1, true);

	TestFalse(TEXT("There is nothing to summon while flying it"), Flying[0].bCanSummon);
	TestEqual(TEXT("...because it is already yours"), Flying[0].Refusal, FString(TEXT("Already yours")));
	TestEqual(TEXT("...and the row says so"), Flying[0].Where, FString(TEXT("You are flying it")));

	// Left on a hillside: deployed, but nowhere near the station being stood at.
	const TArray<FSpaceMMOShipRowText> Adrift =
		USpaceMMOStationOverlay::BuildShipRows(Outside, 7, true, 1, false);

	TestTrue(TEXT("A ship left out there can be recovered"), Adrift[0].bCanSummon);
	TestEqual(TEXT("...and says it is away"), Adrift[0].Where, FString(TEXT("Parked away")));

	// Stowed at another station, standing at a market: the one that sends somebody walking.
	const TArray<FSpaceMMOShipRowText> AtMarket =
		USpaceMMOStationOverlay::BuildShipRows(Stowed, 7, false, 1, false);

	TestFalse(TEXT("A market brings nothing in"), AtMarket[0].bCanSummon);

	TestEqual(
		TEXT("...and says so, because that one is worth walking to fix"),
		AtMarket[0].Refusal,
		FString(TEXT("Not a shipyard")));

	TestEqual(
		TEXT("A hull left elsewhere says where it is"),
		AtMarket[0].Where,
		FString(TEXT("At another station")));

	// At a shipyard, with the ship stowed somewhere else. The one case that can act.
	const TArray<FSpaceMMOShipRowText> Summonable =
		USpaceMMOStationOverlay::BuildShipRows(Stowed, 7, true, 1, false);

	TestTrue(TEXT("A ship elsewhere can be brought to a shipyard"), Summonable[0].bCanSummon);
	TestTrue(TEXT("...with no reason to show"), Summonable[0].Refusal.IsEmpty());

	// Owned, never flown, standing at a shipyard: the questline's payoff.
	const TArray<FSpaceMMOShipRowText> Fresh =
		USpaceMMOStationOverlay::BuildShipRows(Stowed, 5, true, 0, false);

	TestTrue(TEXT("A hull nobody has flown can be summoned"), Fresh[0].bCanSummon);
	TestFalse(TEXT("...and is not marked active"), Fresh[0].bIsActive);

	return true;
}

/**
 * The empty tab says what to do next.
 *
 * Having no ship is the ordinary state for most of the opening, and it is the state ADR-0012
 * deliberately creates. "No ships" on its own is a dead end wearing a label.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOShipsPanelFooterTest,
	"SpaceMMO.Ships.PanelFooter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOShipsPanelFooterTest::RunTest(const FString& Parameters)
{
	FBackendItemInstance Laser;
	Laser.Category = EBackendItemCategory::Tool;

	const FString Empty = USpaceMMOStationOverlay::BuildShipsFooter({ Laser }, true);

	TestTrue(
		TEXT("Owning no hull says what would get you one"),
		Empty.Contains(TEXT("Craft")));

	const TArray<FBackendItemInstance> One = { Hull(1, TEXT("Shuttle"), 5) };

	TestTrue(
		TEXT("At a shipyard, the footer just counts"),
		USpaceMMOStationOverlay::BuildShipsFooter(One, true).Contains(TEXT("1 ship")));

	TestTrue(
		TEXT("Elsewhere it says where summoning happens, which is the question it prompts"),
		USpaceMMOStationOverlay::BuildShipsFooter(One, false).Contains(TEXT("spaceport")));

	return true;
}


/**
 * Which stations hand over ships, by the names the world endpoint sends.
 *
 * <strong>A copy of a server rule, and knowingly so.</strong> The kind arrives as a string, so this
 * is the client's opinion rather than the rule itself — the server refuses regardless. The failure
 * that matters is the asymmetric one: offering a button that comes back refused merely costs a
 * sentence, while greying a button the server would have honoured strands somebody at a shipyard
 * with a ship they cannot call. Both names are asserted rather than one, for that reason.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOShipsPanelKnowsAShipyardTest,
	"SpaceMMO.Ships.PanelKnowsAShipyard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOShipsPanelKnowsAShipyardTest::RunTest(const FString& Parameters)
{
	TestTrue(
		TEXT("A spaceport hands over ships"),
		USpaceMMOStationOverlay::StationHandlesShips(TEXT("Spaceport")));

	TestTrue(
		TEXT("...and so does the capital"),
		USpaceMMOStationOverlay::StationHandlesShips(TEXT("Capital")));

	TestFalse(
		TEXT("A market does not"),
		USpaceMMOStationOverlay::StationHandlesShips(TEXT("TradingHub")));

	TestFalse(
		TEXT("Nor a house"), USpaceMMOStationOverlay::StationHandlesShips(TEXT("Housing")));

	TestFalse(
		TEXT("Nor a bar"), USpaceMMOStationOverlay::StationHandlesShips(TEXT("Social")));

	// Docked nowhere. An empty kind must not read as a shipyard, or the tab offers a summon to
	// somebody standing on a planet.
	TestFalse(
		TEXT("Docked nowhere is not a shipyard"),
		USpaceMMOStationOverlay::StationHandlesShips(FString()));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
