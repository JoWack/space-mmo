#include "Misc/AutomationTest.h"
#include "SpaceMMOBackendProtocol.h"
#include "SpaceMMOPlayerController.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * What crosses the wire for a market, and what may back an order.
 *
 * Separated from the panel tests, which went when the book stopped being lines of text and became
 * rows with buttons on them (task 119). These three outlived that: they are about parsing, the order
 * body, and which holdings can back a sale, none of which changed.
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOMarketOffersOnlyWhatAStationHoldsTest,
	"SpaceMMO.Market.OffersOnlyWhatAStationHolds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOMarketOffersOnlyWhatAStationHoldsTest::RunTest(const FString& Parameters)
{
	// One stack of each kind, with the real wire numbers. The first version of this filter used a
	// made-up numbering in which a hangar was 1, so it matched ship holds, found none, and the key
	// that cycles holdings did nothing at all. Every other test still passed, because they built
	// their inputs by hand and never went near the value the server actually sends.
	TArray<FBackendInventoryItem> Holdings;

	auto Add = [&Holdings](const TCHAR* Name, const EBackendInventoryKind Kind, const int32 Quantity)
	{
		FBackendInventoryItem Item;
		Item.Name = Name;
		Item.Kind = Kind;
		Item.Quantity = Quantity;
		Holdings.Add(Item);
	};

	Add(TEXT("Pocket Lint"), EBackendInventoryKind::CharacterCarried, 5);
	Add(TEXT("Ferrite Plate"), EBackendInventoryKind::StationHangar, 20);
	Add(TEXT("Cargo Ore"), EBackendInventoryKind::ShipHold, 40);
	Add(TEXT("Empty Slot"), EBackendInventoryKind::StationHangar, 0);

	const TArray<FBackendInventoryItem> Sellable =
		ASpaceMMOPlayerController::FilterSellable(Holdings);

	TestEqual(TEXT("Only the stocked hangar stack"), Sellable.Num(), 1);
	TestEqual(TEXT("And it is the right one"), Sellable[0].Name, FString(TEXT("Ferrite Plate")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOMarketParsesABookTest,
	"SpaceMMO.Market.ParsesABook",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOMarketParsesABookTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"([
		{ "orderId": 7, "side": 1, "priceMinorUnits": 1100, "quantityRemaining": 24, "isYours": true },
		{ "orderId": 8, "side": 0, "priceMinorUnits": 900, "quantityRemaining": 5 },
		{ "orderId": 0, "side": 0, "priceMinorUnits": 900, "quantityRemaining": 5 }
	])");

	TArray<FBackendBookEntry> Book;

	TestTrue(TEXT("Parsed"), FSpaceMMOBackendProtocol::ParseBook(Json, Book));

	// The zero-id entry is dropped: a book is a list of things to act on, and an order with no id
	// cannot be bought from.
	TestEqual(TEXT("Kept the actionable ones"), Book.Num(), 2);

	TestEqual(TEXT("Id"), Book[0].OrderId, static_cast<int64>(7));
	TestEqual(TEXT("Price survives as minor units"), Book[0].PriceMinorUnits, static_cast<int64>(1100));
	TestEqual(TEXT("Quantity"), Book[0].QuantityRemaining, 24);

	// Side 1 is Sell. Mapping it the other way would show every ask as a bid, and a buy key would
	// then take the wrong side of the book.
	TestEqual(
		TEXT("Side"),
		static_cast<int32>(Book[0].Side),
		static_cast<int32>(EBackendOrderSide::Sell));

	// Whose it is, which is what greys out the button. Absent means somebody else's rather than
	// unknown: an anonymous read of a public book has no owner to compare against.
	TestTrue(TEXT("Own order marked"), Book[0].bIsYours);
	TestFalse(TEXT("Absent means not yours"), Book[1].bIsYours);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOMarketBuildsAnOrderBodyTest,
	"SpaceMMO.Market.BuildsAnOrderBody",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOMarketBuildsAnOrderBodyTest::RunTest(const FString& Parameters)
{
	const FString Body = FSpaceMMOBackendProtocol::MakePlaceOrderBody(
		11, 1, 3, EBackendOrderSide::Sell, 123456789012LL, 10);

	TestTrue(TEXT("Names the character"), Body.Contains(TEXT("\"characterId\":11")));
	TestTrue(TEXT("Names the item"), Body.Contains(TEXT("\"itemDefId\":3")));
	TestTrue(TEXT("Sell is side 1"), Body.Contains(TEXT("\"side\":1")));
	TestTrue(TEXT("Quantity"), Body.Contains(TEXT("\"quantity\":10")));

	// Prices cross the wire as int64 minor units and never as a decimal. A price that round-trips
	// through a float comes back as something very slightly different, and in a market that
	// difference is eventually a credit somebody is owed.
	TestTrue(
		TEXT("A large price survives exactly"),
		Body.Contains(TEXT("\"limitPriceMinorUnits\":123456789012")));

	return true;
}

#endif
