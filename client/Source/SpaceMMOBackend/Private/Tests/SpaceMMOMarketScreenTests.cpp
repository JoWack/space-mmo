#include "Misc/AutomationTest.h"
#include "SpaceMMOStationOverlay.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Tests for the market screen's wording.
 *
 * What is worth checking here is not visual: whether an item nobody trades is distinguishable from
 * one selling at nothing, and whether the catalogue still lists what has never been traded — which
 * is the whole point of task 105's fix, and the only way a buy order can be placed for something
 * nobody is selling.
 */

namespace
{
	FBackendMarketListing Traded(
		const TCHAR* Name, const int64 Ask, const int64 Bid, const int32 Quantity)
	{
		FBackendMarketListing Listing;
		Listing.ItemDefId = 4;
		Listing.Name = Name;
		Listing.ItemKey = Name;
		Listing.BestAskMinorUnits = Ask;
		Listing.bHasAsk = true;
		Listing.BestBidMinorUnits = Bid;
		Listing.bHasBid = true;
		Listing.QuantityForSale = Quantity;

		return Listing;
	}

	FBackendMarketListing Untraded(const TCHAR* Name)
	{
		FBackendMarketListing Listing;
		Listing.ItemDefId = 9;
		Listing.Name = Name;
		Listing.ItemKey = Name;

		return Listing;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOMarketListsWhatNobodyTradesTest,
	"SpaceMMO.HUD.MarketListsWhatNobodyTrades",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOMarketListsWhatNobodyTradesTest::RunTest(const FString& Parameters)
{
	const TArray<FSpaceMMOMarketRowText> Rows = USpaceMMOStationOverlay::BuildMarketRows(
		{ Traded(TEXT("Ferrite Ore"), 2000, 1800, 230), Untraded(TEXT("Silicate Dust")) });

	TestEqual(TEXT("Both listed"), Rows.Num(), 2);

	TestEqual(TEXT("Ask is priced"), Rows[0].Sell, FString(TEXT("20.00 cr")));
	TestEqual(TEXT("Bid is priced"), Rows[0].Buy, FString(TEXT("18.00 cr")));
	TestEqual(TEXT("Quantity is grouped"), Rows[0].Quantity, FString(TEXT("230")));
	TestTrue(TEXT("And it is traded"), Rows[0].bTraded);

	// The row that fixes 105: an item nobody sells is still findable, so a buy order can be placed
	// for it. Listing only what is for sale would leave a buyer unable to discover the item at all.
	TestEqual(TEXT("Untraded is still listed"), Rows[1].Name, FString(TEXT("Silicate Dust")));
	TestFalse(TEXT("But marked untraded"), Rows[1].bTraded);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOMarketNoPriceIsNotAZeroPriceTest,
	"SpaceMMO.HUD.MarketNoPriceIsNotAZeroPrice",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOMarketNoPriceIsNotAZeroPriceTest::RunTest(const FString& Parameters)
{
	// Zero is a legal price. An item selling at nothing and an item nobody offers must not read the
	// same, or a player cannot tell a giveaway from an absence.
	const TArray<FSpaceMMOMarketRowText> Rows = USpaceMMOStationOverlay::BuildMarketRows(
		{ Traded(TEXT("Free Sample"), 0, 0, 5), Untraded(TEXT("Silicate Dust")) });

	TestEqual(TEXT("Zero shows as a price"), Rows[0].Sell, FString(TEXT("0.00 cr")));
	TestNotEqual(TEXT("And nothing does not"), Rows[1].Sell, Rows[0].Sell);
	TestTrue(TEXT("Nothing shows as a dash"), Rows[1].Sell.Contains(TEXT("—")));

	// Nothing for sale leaves the quantity blank rather than showing a zero, which would read as a
	// stock level somebody is maintaining rather than as an absence.
	TestTrue(TEXT("No quantity where none is for sale"), Rows[1].Quantity.IsEmpty());
	TestEqual(TEXT("But a real one is shown"), Rows[0].Quantity, FString(TEXT("5")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
