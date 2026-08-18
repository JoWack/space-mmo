#include "Misc/AutomationTest.h"
#include "SpaceMMOStationOverlay.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FBackendMyOrder MakeOrder(
		const int64 OrderId,
		const int32 StationId,
		const TCHAR* StationName,
		const EBackendOrderSide Side,
		const int64 Price,
		const int32 Remaining,
		const int64 Escrow,
		const int32 Reserved)
	{
		FBackendMyOrder Order;
		Order.OrderId = OrderId;
		Order.StationId = StationId;
		Order.StationName = StationName;
		Order.ItemName = TEXT("Ferrite Ore");
		Order.Side = Side;
		Order.PriceMinorUnits = Price;
		Order.QuantityRemaining = Remaining;
		Order.EscrowedMinorUnits = Escrow;
		Order.ReservedQuantity = Reserved;

		return Order;
	}
}

/**
 * Task 119. An order placed at the wrong price used to rest until it expired, because nothing
 * listed it and nothing could withdraw it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOMyOrdersMarksTheOnesElsewhereTest,
	"SpaceMMO.HUD.MyOrdersMarkTheOnesElsewhere",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOMyOrdersMarksTheOnesElsewhereTest::RunTest(const FString& Parameters)
{
	const TArray<FBackendMyOrder> Orders{
		MakeOrder(1, 7, TEXT("Capital Trading Hub"), EBackendOrderSide::Sell, 1, 11, 0, 11),
		MakeOrder(2, 9, TEXT("DeepDock"), EBackendOrderSide::Buy, 2000, 100, 200000, 0),
	};

	const TArray<FSpaceMMOMyOrderRowText> Rows =
		USpaceMMOStationOverlay::BuildMyOrderRows(Orders, 7);

	TestEqual(TEXT("Both listed"), Rows.Num(), 2);

	// Listed wherever they rest, and the far one marked rather than hidden. Scoping this to the
	// station being stood in would hide exactly the order somebody opened the tab to find.
	TestFalse(TEXT("Here is not marked"), Rows[0].bElsewhere);
	TestTrue(TEXT("Elsewhere is marked"), Rows[1].bElsewhere);
	TestEqual(TEXT("Names where"), Rows[1].Station, FString(TEXT("DeepDock")));

	// The player's words, not the book's.
	TestEqual(TEXT("Sell side"), Rows[0].Side, FString(TEXT("SELL")));
	TestEqual(TEXT("Buy side"), Rows[1].Side, FString(TEXT("BUY")));

	// The order this whole task exists for: one minor unit is 0.01 cr, not 1.
	TestEqual(TEXT("Price in credits"), Rows[0].Price, FString(TEXT("0.01 cr")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOMyOrdersFooterNamesWhatIsHeldTest,
	"SpaceMMO.HUD.MyOrdersFooterNamesWhatIsHeld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOMyOrdersFooterNamesWhatIsHeldTest::RunTest(const FString& Parameters)
{
	// Why a forgotten order matters: a sell order holds goods and a buy order holds credits, and
	// both are out of reach until it fills or is withdrawn. Without this the player finds out by
	// coming up short somewhere else entirely.
	const TArray<FBackendMyOrder> Orders{
		MakeOrder(1, 7, TEXT("Here"), EBackendOrderSide::Sell, 100, 16, 0, 16),
		MakeOrder(2, 7, TEXT("Here"), EBackendOrderSide::Buy, 2000, 100, 120500, 0),
	};

	const FString Footer = USpaceMMOStationOverlay::BuildMyOrdersFooter(Orders);

	TestTrue(TEXT("Counts them"), Footer.Contains(TEXT("2 resting")));
	TestTrue(TEXT("Names the credits"), Footer.Contains(TEXT("1,205.00 cr locked")));
	TestTrue(TEXT("Names the goods"), Footer.Contains(TEXT("16 units reserved")));

	// Nothing resting is a state worth wording, not a blank line that reads as a failed request.
	TestEqual(
		TEXT("Empty speaks"),
		USpaceMMOStationOverlay::BuildMyOrdersFooter(TArray<FBackendMyOrder>()),
		FString(TEXT("Nothing resting")));

	// And a player with only sell orders is not told that nothing is escrowed.
	const TArray<FBackendMyOrder> SellOnly{
		MakeOrder(1, 7, TEXT("Here"), EBackendOrderSide::Sell, 100, 16, 0, 16),
	};

	TestFalse(
		TEXT("No zero halves"),
		USpaceMMOStationOverlay::BuildMyOrdersFooter(SellOnly).Contains(TEXT("locked")));

	return true;
}

#endif
