#include "Misc/AutomationTest.h"
#include "SpaceMMOStationOverlay.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FBackendBookEntry Entry(
		const int64 OrderId,
		const EBackendOrderSide Side,
		const int64 Price,
		const int32 Remaining,
		const bool bMine = false)
	{
		FBackendBookEntry E;
		E.OrderId = OrderId;
		E.Side = Side;
		E.PriceMinorUnits = Price;
		E.QuantityRemaining = Remaining;
		E.bIsYours = bMine;

		return E;
	}

	const FSpaceMMOBookRowText* RowFor(
		const TArray<FSpaceMMOBookRowText>& Rows, const int64 OrderId)
	{
		return Rows.FindByPredicate(
			[OrderId](const FSpaceMMOBookRowText& R) { return R.OrderId == OrderId; });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOBookOffersTheVerbThePlayerWouldUseTest,
	"SpaceMMO.HUD.BookOffersTheVerbThePlayerWouldUse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOBookOffersTheVerbThePlayerWouldUseTest::RunTest(const FString& Parameters)
{
	const TArray<FBackendBookEntry> Book{
		Entry(1, EBackendOrderSide::Sell, 1250, 20),
		Entry(2, EBackendOrderSide::Sell, 1000, 4),
		Entry(3, EBackendOrderSide::Buy, 2000, 100),
	};

	const TArray<FSpaceMMOBookRowText> Rows = USpaceMMOStationOverlay::BuildBookRows(Book, true);

	// A resting sell order is something you buy from. Labelling the button with the order's own side
	// would tell somebody they were selling when they were about to spend credits.
	const FSpaceMMOBookRowText* Ask = RowFor(Rows, 2);
	const FSpaceMMOBookRowText* Bid = RowFor(Rows, 3);

	TestTrue(TEXT("Both rows built"), Ask != nullptr && Bid != nullptr);
	TestEqual(TEXT("Buy from a seller"), Ask->ActionLabel, FString(TEXT("Buy 4")));
	TestEqual(TEXT("Sell to a buyer"), Bid->ActionLabel, FString(TEXT("Sell 100")));

	// Quantity and price in their own columns, and the price in credits rather than hundredths.
	TestEqual(TEXT("Quantity alone"), Ask->Quantity, FString(TEXT("4")));
	TestEqual(TEXT("Price in credits"), Ask->Price, FString(TEXT("10.00 cr")));

	// Cheapest ask leads its side, because that is the price a buyer would actually get.
	const int32 Cheap = Rows.IndexOfByPredicate(
		[](const FSpaceMMOBookRowText& R) { return R.OrderId == 2; });

	const int32 Dear = Rows.IndexOfByPredicate(
		[](const FSpaceMMOBookRowText& R) { return R.OrderId == 1; });

	TestTrue(TEXT("Cheapest ask first"), Cheap < Dear);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOBookWillNotOfferYourOwnOrderTest,
	"SpaceMMO.HUD.BookWillNotOfferYourOwnOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOBookWillNotOfferYourOwnOrderTest::RunTest(const FString& Parameters)
{
	// Matching refuses a self-trade, so taking your own order places one that cannot cross it and
	// simply rests. That is how an ask at 0.01 cr and a bid at 20.00 cr came to sit on one book,
	// looking for all the world like a market that had stopped working.
	const TArray<FBackendBookEntry> Book{
		Entry(1, EBackendOrderSide::Sell, 1000, 4, true),
		Entry(2, EBackendOrderSide::Sell, 1200, 4, false),
	};

	const TArray<FSpaceMMOBookRowText> Rows = USpaceMMOStationOverlay::BuildBookRows(Book, true);

	TestFalse(TEXT("Own order cannot be taken"), RowFor(Rows, 1)->bCanTake);
	TestTrue(TEXT("Somebody else's can"), RowFor(Rows, 2)->bCanTake);

	// Still listed, and still priced. It is part of the depth whoever placed it is looking at.
	TestEqual(TEXT("Own order still priced"), RowFor(Rows, 1)->Price, FString(TEXT("10.00 cr")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOBookSaysWhenASideIsEmptyTest,
	"SpaceMMO.HUD.BookSaysWhenASideIsEmpty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOBookSaysWhenASideIsEmptyTest::RunTest(const FString& Parameters)
{
	// The state of every item until somebody trades it, which is most of the catalogue. A gap under
	// a heading reads as a request that failed rather than as nobody selling.
	const TArray<FSpaceMMOBookRowText> Rows =
		USpaceMMOStationOverlay::BuildBookRows(TArray<FBackendBookEntry>(), true);

	int32 Empties = 0;

	for (const FSpaceMMOBookRowText& Row : Rows)
	{
		TestTrue(TEXT("Nothing to take"), Row.bIsHeading);

		if (Row.Heading.Contains(TEXT("none")))
		{
			++Empties;
		}
	}

	TestEqual(TEXT("Both sides speak"), Empties, 2);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOBookWaitingIsNotAnEmptyBookTest,
	"SpaceMMO.HUD.BookWaitingIsNotAnEmptyBook",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOBookWaitingIsNotAnEmptyBookTest::RunTest(const FString& Parameters)
{
	// Clicking a row leaves a frame or two before the answer arrives, and during it the client still
	// holds the previous item's orders. Showing an empty book meanwhile invites somebody to conclude
	// no market exists for a thing that is being actively traded.
	const TArray<FSpaceMMOBookRowText> Waiting =
		USpaceMMOStationOverlay::BuildBookRows(TArray<FBackendBookEntry>(), false);

	TestEqual(TEXT("One line while waiting"), Waiting.Num(), 1);
	TestTrue(TEXT("Says it is waiting"), Waiting[0].Heading.Contains(TEXT("loading")));

	// And nothing offering a trade, because there is nothing yet to trade against.
	TestTrue(TEXT("Nothing to take"), Waiting[0].bIsHeading);
	TestFalse(TEXT("No button"), Waiting[0].bCanTake);

	// Loaded and empty says the opposite thing, in words.
	const TArray<FSpaceMMOBookRowText> Loaded =
		USpaceMMOStationOverlay::BuildBookRows(TArray<FBackendBookEntry>(), true);

	TestTrue(TEXT("Empty book speaks"), Loaded.Num() > 1);

	return true;
}

#endif
