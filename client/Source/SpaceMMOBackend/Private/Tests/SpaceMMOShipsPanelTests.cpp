#include "Misc/AutomationTest.h"

#include "SpaceMMOBackendTypes.h"
#include "SpaceMMOStationOverlay.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FBackendItemInstance Hull(
		const int64 Id, const TCHAR* Name, const int32 StationId, const int32 Condition = 100)
	{
		FBackendItemInstance Instance;

		Instance.Id = Id;
		Instance.Name = Name;
		Instance.StationId = StationId;
		Instance.Condition = Condition;
		Instance.Category = EBackendItemCategory::Hull;
		Instance.Kind = EBackendInventoryKind::StationHangar;

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
		USpaceMMOStationOverlay::BuildShipRows(Owned, 5, true, 0);

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
	const TArray<FBackendItemInstance> Owned = { Hull(1, TEXT("Shuttle"), 5) };

	// Standing at the shipyard where it already is, and flying it.
	const TArray<FSpaceMMOShipRowText> Flying =
		USpaceMMOStationOverlay::BuildShipRows(Owned, 5, true, 1);

	TestTrue(TEXT("The active ship is marked"), Flying[0].bIsActive);
	TestFalse(TEXT("...and there is nothing to summon"), Flying[0].bCanSummon);
	TestEqual(TEXT("...because it is already here"), Flying[0].Refusal, FString(TEXT("Already here")));
	TestEqual(TEXT("Where it is reads plainly"), Flying[0].Where, FString(TEXT("Here")));

	// Same hull, but the player is at a market.
	const TArray<FSpaceMMOShipRowText> AtMarket =
		USpaceMMOStationOverlay::BuildShipRows(Owned, 7, false, 1);

	TestFalse(TEXT("A market summons nothing"), AtMarket[0].bCanSummon);

	TestEqual(
		TEXT("...and says so, because that one is worth walking to fix"),
		AtMarket[0].Refusal,
		FString(TEXT("Not a shipyard")));

	TestEqual(
		TEXT("A hull left elsewhere says where it is"),
		AtMarket[0].Where,
		FString(TEXT("At another station")));

	// At a shipyard, with the ship parked somewhere else. The one case that can act.
	const TArray<FSpaceMMOShipRowText> Summonable =
		USpaceMMOStationOverlay::BuildShipRows(Owned, 7, true, 1);

	TestTrue(TEXT("A ship elsewhere can be brought to a shipyard"), Summonable[0].bCanSummon);
	TestTrue(TEXT("...with no reason to show"), Summonable[0].Refusal.IsEmpty());

	// Owned, never flown, standing at a shipyard: the questline's payoff.
	const TArray<FSpaceMMOShipRowText> Fresh =
		USpaceMMOStationOverlay::BuildShipRows(Owned, 5, true, 0);

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
