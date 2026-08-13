#include "Misc/AutomationTest.h"
#include "SpaceMMOPanelTestHelpers.h"
#include "SpaceMMOBackendProtocol.h"
#include "SpaceMMOPlayerController.h"

#if WITH_DEV_AUTOMATION_TESTS

// Shared, because a unity build can put two of these files in one translation
// unit, where two anonymous namespaces are the same namespace and a second copy
// of a helper is a redefinition.
using SpaceMMOPanelTests::AnyLineContains;
using SpaceMMOPanelTests::IndexOfLineContaining;

namespace
{
	FBackendBookEntry MakeEntry(
		const int64 OrderId,
		const EBackendOrderSide Side,
		const int64 PriceMinorUnits,
		const int32 Quantity)
	{
		FBackendBookEntry Entry;
		Entry.OrderId = OrderId;
		Entry.Side = Side;
		Entry.PriceMinorUnits = PriceMinorUnits;
		Entry.QuantityRemaining = Quantity;

		return Entry;
	}

	int32 LineWith(const TArray<FString>& Lines, const FString& Fragment)
	{
		return Lines.IndexOfByPredicate(
			[&Fragment](const FString& Line) { return Line.Contains(Fragment); });
	}

}

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
	FSpaceMMOMarketSortsTowardTheSpreadTest,
	"SpaceMMO.Market.SortsTowardTheSpread",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOMarketSortsTowardTheSpreadTest::RunTest(const FString& Parameters)
{
	// Deliberately out of order, and in the order the API happens to return: side then price
	// ascending, which puts the worst bid first.
	const TArray<FBackendBookEntry> Book{
		MakeEntry(1, EBackendOrderSide::Buy, 300, 5),
		MakeEntry(2, EBackendOrderSide::Buy, 900, 5),
		MakeEntry(3, EBackendOrderSide::Sell, 2500, 5),
		MakeEntry(4, EBackendOrderSide::Sell, 1100, 5),
	};

	const TArray<FString> Lines =
		ASpaceMMOPlayerController::BuildMarketPanel(TEXT("Ferrite Plate"), Book, 1000);

	const int32 Asks = LineWith(Lines, TEXT("asks:"));
	const int32 Bids = LineWith(Lines, TEXT("bids:"));

	TestTrue(TEXT("Both sides shown"), Asks != INDEX_NONE && Bids != INDEX_NONE);

	// The cheapest ask is what a buyer pays and the richest bid is what a seller gets, so each side
	// leads with the price nearest the spread. Printing them in book order would put the least
	// relevant number first on both sides, which is backwards for anyone deciding whether to trade.
	TestTrue(
		TEXT("Cheapest ask first"),
		Lines[Asks].Find(TEXT("11.00")) < Lines[Asks].Find(TEXT("25.00")));

	TestTrue(
		TEXT("Richest bid first"),
		Lines[Bids].Find(TEXT("9.00")) < Lines[Bids].Find(TEXT("3.00")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOMarketSpeaksWithAnEmptyBookTest,
	"SpaceMMO.Market.SpeaksWithAnEmptyBook",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOMarketSpeaksWithAnEmptyBookTest::RunTest(const FString& Parameters)
{
	// The state of every item until somebody lists one, and the state a new market starts in. A
	// blank row here reads as a failed request rather than as an empty book.
	const TArray<FString> Lines = ASpaceMMOPlayerController::BuildMarketPanel(
		TEXT("Ferrite Plate"), TArray<FBackendBookEntry>(), 1000);

	TestTrue(TEXT("Names the item"), AnyLineContains(Lines, TEXT("Ferrite Plate")));
	TestTrue(TEXT("Shows the listing price"), AnyLineContains(Lines, TEXT("10.00 cr")));
	TestTrue(TEXT("Says asks are empty"), AnyLineContains(Lines, TEXT("asks: none")));
	TestTrue(TEXT("Says bids are empty"), AnyLineContains(Lines, TEXT("bids: none")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOMarketSaysWhenNothingIsSellableTest,
	"SpaceMMO.Market.SaysWhenNothingIsSellable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOMarketSaysWhenNothingIsSellableTest::RunTest(const FString& Parameters)
{
	// A player whose goods are all in their ship's hold rather than at the station. They own
	// plenty; none of it can back an order, and the panel has to say which rather than showing an
	// empty book for an item it never named.
	const TArray<FString> Lines =
		ASpaceMMOPlayerController::BuildMarketPanel(FString(), TArray<FBackendBookEntry>(), 0);

	TestTrue(TEXT("Explains why"), AnyLineContains(Lines, TEXT("nothing here a station can sell")));
	TestFalse(TEXT("Shows no book"), AnyLineContains(Lines, TEXT("asks:")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOMarketParsesABookTest,
	"SpaceMMO.Market.ParsesABook",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOMarketParsesABookTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"([
		{ "orderId": 7, "side": 1, "priceMinorUnits": 1100, "quantityRemaining": 24 },
		{ "orderId": 0, "side": 0, "priceMinorUnits": 900, "quantityRemaining": 5 }
	])");

	TArray<FBackendBookEntry> Book;

	TestTrue(TEXT("Parsed"), FSpaceMMOBackendProtocol::ParseBook(Json, Book));

	// The zero-id entry is dropped: a book is a list of things to act on, and an order with no id
	// cannot be bought from.
	TestEqual(TEXT("Kept the actionable one"), Book.Num(), 1);

	TestEqual(TEXT("Id"), Book[0].OrderId, static_cast<int64>(7));
	TestEqual(TEXT("Price survives as minor units"), Book[0].PriceMinorUnits, static_cast<int64>(1100));
	TestEqual(TEXT("Quantity"), Book[0].QuantityRemaining, 24);

	// Side 1 is Sell. Mapping it the other way would show every ask as a bid, and a buy key would
	// then take the wrong side of the book.
	TestEqual(
		TEXT("Side"),
		static_cast<int32>(Book[0].Side),
		static_cast<int32>(EBackendOrderSide::Sell));

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
