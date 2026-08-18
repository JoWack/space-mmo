#include "SpaceMMOStationOverlay.h"

#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "SpaceMMOBackendClient.h"
#include "SpaceMMOBackendLog.h"
#include "SpaceMMOBackendProtocol.h"
#include "SpaceMMOInventoryScreen.h"
#include "SpaceMMOPlayerController.h"

void USpaceMMOTextRow::SetLine(const FString& Line)
{
	if (LineText != nullptr)
	{
		LineText->SetText(FText::FromString(Line));
	}
}

namespace
{
	/** A price, or a dash where none exists. Zero is a legal price, so blank must not mean zero. */
	FString PriceOrDash(const bool bHas, const int64 MinorUnits)
	{
		return bHas
			? FString::Printf(TEXT("%s cr"), *FSpaceMMOBackendProtocol::FormatCredits(MinorUnits))
			: FString(TEXT("—"));
	}
}

void USpaceMMOMarketRow::SetRow(const FSpaceMMOMarketRowText& InRow)
{
	Row = InRow;

	auto Set = [](UTextBlock* Block, const FString& Value)
	{
		if (Block != nullptr)
		{
			Block->SetText(FText::FromString(Value));
		}
	};

	Set(NameText, Row.Name);
	Set(SellText, Row.Sell);
	Set(BuyText, Row.Buy);
	Set(QuantityText, Row.Quantity);

	bTraded = Row.bTraded;
}

FReply USpaceMMOMarketRow::NativeOnMouseButtonDown(
	const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (USpaceMMOStationOverlay* Overlay = OwningOverlay.Get())
	{
		Overlay->SelectMarketItem(Row.ItemDefId);

		return FReply::Handled();
	}

	return FReply::Unhandled();
}

void USpaceMMOBookRow::SetRow(const FSpaceMMOBookRowText& InRow)
{
	Row = InRow;

	auto Set = [](UTextBlock* Block, const FString& Value)
	{
		if (Block != nullptr)
		{
			Block->SetText(FText::FromString(Value));
		}
	};

	Set(HeadingText, Row.Heading);
	Set(QuantityText, Row.Quantity);
	Set(PriceText, Row.Price);
	Set(ActionText, Row.ActionLabel);

	bIsHeading = Row.bIsHeading;
	bCanTake = Row.bCanTake;
}

void USpaceMMOBookRow::Take()
{
	if (USpaceMMOStationOverlay* Overlay = OwningOverlay.Get(); Overlay != nullptr && bCanTake)
	{
		Overlay->TakeBookOrder(Row.OrderId);
	}
}

TArray<FSpaceMMOBookRowText> USpaceMMOStationOverlay::BuildBookRows(
	const TArray<FBackendBookEntry>& Book, const bool bLoaded)
{
	if (!bLoaded)
	{
		FSpaceMMOBookRowText Waiting;
		Waiting.bIsHeading = true;
		Waiting.Heading = TEXT("loading...");

		return { Waiting };
	}

	TArray<FBackendBookEntry> Asks = Book.FilterByPredicate(
		[](const FBackendBookEntry& E) { return E.Side == EBackendOrderSide::Sell; });

	TArray<FBackendBookEntry> Bids = Book.FilterByPredicate(
		[](const FBackendBookEntry& E) { return E.Side == EBackendOrderSide::Buy; });

	// Cheapest ask and richest bid first: each side leads with the price nearest the spread, which
	// is the one somebody deciding whether to trade would actually get.
	Asks.Sort([](const FBackendBookEntry& A, const FBackendBookEntry& B)
		{ return A.PriceMinorUnits < B.PriceMinorUnits; });

	Bids.Sort([](const FBackendBookEntry& A, const FBackendBookEntry& B)
		{ return A.PriceMinorUnits > B.PriceMinorUnits; });

	TArray<FSpaceMMOBookRowText> Rows;

	auto AppendSide = [&Rows](
		const TCHAR* Heading,
		const TCHAR* Verb,
		const TArray<FBackendBookEntry>& Entries)
	{
		FSpaceMMOBookRowText HeadingRow;
		HeadingRow.bIsHeading = true;
		HeadingRow.Heading = Heading;

		Rows.Add(HeadingRow);

		if (Entries.Num() == 0)
		{
			// Worded rather than left blank. An empty gap under a heading reads as a request that
			// failed, and "nobody is selling this" is a thing a buyer needs told.
			FSpaceMMOBookRowText Empty;
			Empty.bIsHeading = true;
			Empty.Heading = TEXT("   none");

			Rows.Add(Empty);

			return;
		}

		for (const FBackendBookEntry& Entry : Entries)
		{
			FSpaceMMOBookRowText Row;
			Row.OrderId = Entry.OrderId;
			Row.Quantity = ASpaceMMOPlayerController::GroupDigits(Entry.QuantityRemaining);

			Row.Price = FString::Printf(
				TEXT("%s cr"), *FSpaceMMOBackendProtocol::FormatCredits(Entry.PriceMinorUnits));

			// Sized to this row, because taking it fills exactly this much.
			Row.ActionLabel = FString::Printf(
				TEXT("%s %s"), Verb, *ASpaceMMOPlayerController::GroupDigits(Entry.QuantityRemaining));

			// Not your own. Matching refuses a self-trade, so taking one would place an order that
			// cannot cross and simply rests -- which is how an ask at 0.01 and a bid at 20.00 came
			// to sit on one book looking like a broken market.
			Row.bCanTake = !Entry.bIsYours;

			Rows.Add(Row);
		}
	};

	// A resting sell order is something you buy from, and a resting buy order is something you sell
	// into. The verb on the button is what the player would be doing, not what the order is.
	AppendSide(TEXT("SELLING"), TEXT("Buy"), Asks);
	AppendSide(TEXT("BUYING"), TEXT("Sell"), Bids);

	return Rows;
}

void USpaceMMOStationOverlay::TakeBookOrder(const int64 OrderId)
{
	ASpaceMMOPlayerController* Controller = Cast<ASpaceMMOPlayerController>(GetOwningPlayer());

	USpaceMMOBackendClient* Client = MarketClient();

	if (Controller == nullptr || Client == nullptr)
	{
		return;
	}

	const FBackendBookEntry* Entry = Client->GetBook().FindByPredicate(
		[OrderId](const FBackendBookEntry& E) { return E.OrderId == OrderId; });

	if (Entry == nullptr || Entry->bIsYours || Entry->QuantityRemaining <= 0)
	{
		return;
	}

	// The book on screen might be another item's if a request is still in flight. Trading against
	// the wrong one would spend real credits on something the player never looked at.
	if (Client->GetBookItemDefId() != SelectedItemDefId || SelectedItemDefId == 0)
	{
		Controller->ShowTransientMessage(TEXT("Book still loading"), ESpaceMMOMessageTone::Warning);

		return;
	}

	// A limit at this order's price rather than a market order. It crosses immediately, and if
	// somebody takes it first this rests rather than chasing the price -- which is what a market
	// order would do and is never what anybody meant.
	Client->PlaceOrder(
		Controller->GetCharacterId(),
		Controller->DockedStationId(),
		SelectedItemDefId,
		Entry->Side == EBackendOrderSide::Sell ? EBackendOrderSide::Buy : EBackendOrderSide::Sell,
		Entry->PriceMinorUnits,
		Entry->QuantityRemaining);
}

void USpaceMMOMyOrderRow::SetRow(const FSpaceMMOMyOrderRowText& InRow)
{
	Row = InRow;

	auto Set = [](UTextBlock* Block, const FString& Value)
	{
		if (Block != nullptr)
		{
			Block->SetText(FText::FromString(Value));
		}
	};

	Set(SideText, Row.Side);
	Set(ItemText, Row.Item);
	Set(QuantityText, Row.Quantity);
	Set(PriceText, Row.Price);
	Set(StationText, Row.Station);

	bElsewhere = Row.bElsewhere;
}

void USpaceMMOMyOrderRow::Cancel()
{
	if (USpaceMMOStationOverlay* Overlay = OwningOverlay.Get())
	{
		Overlay->CancelRestingOrder(Row.OrderId);
	}
}

TArray<FSpaceMMOMyOrderRowText> USpaceMMOStationOverlay::BuildMyOrderRows(
	const TArray<FBackendMyOrder>& Orders, const int32 HereStationId)
{
	TArray<FSpaceMMOMyOrderRowText> Rows;

	Rows.Reserve(Orders.Num());

	for (const FBackendMyOrder& Order : Orders)
	{
		FSpaceMMOMyOrderRowText Row;
		Row.OrderId = Order.OrderId;

		// The player's words rather than the book's. The domain says ask and bid throughout and is
		// right to, but somebody looking at their own orders is asking "am I buying or selling this".
		Row.Side = Order.Side == EBackendOrderSide::Sell ? TEXT("SELL") : TEXT("BUY");
		Row.Item = Order.ItemName;

		Row.Quantity = ASpaceMMOPlayerController::GroupDigits(Order.QuantityRemaining);

		Row.Price = FString::Printf(
			TEXT("%s cr"), *FSpaceMMOBackendProtocol::FormatCredits(Order.PriceMinorUnits));

		Row.Station = Order.StationName;

		// Marked rather than hidden or filtered out. An order somewhere else is the one a player has
		// forgotten, which is the whole reason this list is not scoped to where they are standing.
		Row.bElsewhere = HereStationId != 0 && Order.StationId != HereStationId;

		Rows.Add(Row);
	}

	return Rows;
}

FString USpaceMMOStationOverlay::BuildMyOrdersFooter(const TArray<FBackendMyOrder>& Orders)
{
	if (Orders.Num() == 0)
	{
		return TEXT("Nothing resting");
	}

	int64 Locked = 0;
	int32 Reserved = 0;

	for (const FBackendMyOrder& Order : Orders)
	{
		Locked += Order.EscrowedMinorUnits;
		Reserved += Order.ReservedQuantity;
	}

	FString Line = FString::Printf(
		TEXT("%d resting"), Orders.Num());

	// Only the halves that are actually holding something. A player with no buy orders does not
	// need telling that nothing is escrowed, and a line of zeroes buries the figure that matters.
	if (Locked > 0)
	{
		Line += FString::Printf(
			TEXT(" · %s cr locked"), *FSpaceMMOBackendProtocol::FormatCredits(Locked));
	}

	if (Reserved > 0)
	{
		Line += FString::Printf(
			TEXT(" · %s units reserved"), *ASpaceMMOPlayerController::GroupDigits(Reserved));
	}

	return Line;
}

void USpaceMMOStationOverlay::CancelRestingOrder(const int64 OrderId)
{
	ASpaceMMOPlayerController* Controller = Cast<ASpaceMMOPlayerController>(GetOwningPlayer());

	USpaceMMOBackendClient* Client = MarketClient();

	if (Controller == nullptr || Client == nullptr || OrderId <= 0)
	{
		return;
	}

	// No confirmation. Nothing is destroyed -- the escrow returns to the balance and the goods to
	// the hangar -- so the worst a misclick costs is queue position, and a prompt on every
	// withdrawal would be noise on the common case.
	Client->CancelOrder(Controller->GetCharacterId(), OrderId);
}

TArray<FSpaceMMOMarketRowText> USpaceMMOStationOverlay::BuildMarketRows(
	const TArray<FBackendMarketListing>& Listings)
{
	TArray<FSpaceMMOMarketRowText> Rows;

	Rows.Reserve(Listings.Num());

	for (const FBackendMarketListing& Listing : Listings)
	{
		FSpaceMMOMarketRowText Row;
		Row.ItemDefId = Listing.ItemDefId;
		Row.Name = Listing.Name;
		Row.Sell = PriceOrDash(Listing.bHasAsk, Listing.BestAskMinorUnits);
		Row.Buy = PriceOrDash(Listing.bHasBid, Listing.BestBidMinorUnits);

		// Blank rather than "0" where nothing is for sale: a zero in a quantity column reads as a
		// stock level somebody is maintaining, rather than as an absence.
		Row.Quantity = Listing.QuantityForSale > 0
			? ASpaceMMOPlayerController::GroupDigits(Listing.QuantityForSale)
			: FString();

		// Whether anybody trades this here at all. Most of the catalogue will not be, and those rows
		// are there to be found and ordered against rather than read.
		Row.bTraded = Listing.bHasAsk || Listing.bHasBid;

		Rows.Add(Row);
	}

	return Rows;
}

void USpaceMMOStationOverlay::SelectMarketItem(const int32 ItemDefId)
{
	SelectedItemDefId = ItemDefId;

	if (MarketListingRows != nullptr)
	{
		for (UWidget* Child : MarketListingRows->GetAllChildren())
		{
			if (USpaceMMOMarketRow* Row = Cast<USpaceMMOMarketRow>(Child))
			{
				Row->bSelected = Row->GetRow().ItemDefId == ItemDefId;
			}
		}
	}

	// The book is fetched per item, so choosing a row is what makes one appear. Asked for rather
	// than filtered locally: depth changes whenever anyone trades, and a stale book is a price a
	// player would act on.
	const ASpaceMMOPlayerController* Controller =
		Cast<ASpaceMMOPlayerController>(GetOwningPlayer());

	if (USpaceMMOBackendClient* Client = MarketClient())
	{
		Client->FetchBook(Controller != nullptr ? Controller->DockedStationId() : 0, ItemDefId);
	}
}

void USpaceMMOStationOverlay::SetMarketSearch(const FString& Search)
{
	if (MarketSearch == Search)
	{
		return;
	}

	MarketSearch = Search;

	RefreshMarketListings();
}

USpaceMMOBackendClient* USpaceMMOStationOverlay::MarketClient() const
{
	const UGameInstance* GameInstance = GetGameInstance();

	return GameInstance != nullptr
		? GameInstance->GetSubsystem<USpaceMMOBackendClient>()
		: nullptr;
}

void USpaceMMOStationOverlay::RefreshMarketListings()
{
	const ASpaceMMOPlayerController* Controller =
		Cast<ASpaceMMOPlayerController>(GetOwningPlayer());

	if (USpaceMMOBackendClient* Client = MarketClient(); Client != nullptr && Controller != nullptr)
	{
		Client->FetchMarketListings(Controller->DockedStationId(), MarketSearch);
	}
}

FString USpaceMMOStationOverlay::RefuseSellDrop(
	const FSpaceMMOInventoryLine& Line, const bool bInCatalogue)
{
	if (Line.bIsHeading)
	{
		return TEXT("Drag a stack, not a whole container");
	}

	// An instance is one object with its own condition -- a hull, a mining laser -- and the book
	// moves quantities rather than objects. Listing one would be offering something no order could
	// fill. This is the same line the catalogue is drawn on.
	if (Line.InstanceId != 0 || Line.ItemDefId == 0 || Line.Quantity <= 0)
	{
		return TEXT("The market only trades stackable goods");
	}

	// The rule the API enforces rather than a guess at it, and the same one that dims the row.


	// The rule the API enforces rather than a guess at it, and the same one that dims the row.
	if (!Line.bReachable)
	{
		return TEXT("Those goods are at another station");
	}

	if (!bInCatalogue)
	{
		return TEXT("Nothing here trades that");
	}

	return FString();
}

bool USpaceMMOStationOverlay::NativeOnDrop(
	const FGeometry& Geometry, const FDragDropEvent& Event, UDragDropOperation* Operation)
{
	const USpaceMMOInventoryDrag* Drag = Cast<USpaceMMOInventoryDrag>(Operation);

	// Only onto the market. The other tabs have their own meaning for a drop or none at all, and
	// swallowing it here would lose the stack somewhere the player was not looking.
	if (Drag == nullptr || !bMarketTab)
	{
		return false;
	}

	ASpaceMMOPlayerController* Controller = Cast<ASpaceMMOPlayerController>(GetOwningPlayer());

	const USpaceMMOBackendClient* Client = MarketClient();

	if (Controller == nullptr || Client == nullptr)
	{
		return false;
	}

	const bool bInCatalogue = Client->GetMarketListings().ContainsByPredicate(
		[&Drag](const FBackendMarketListing& L) { return L.ItemDefId == Drag->Line.ItemDefId; });

	if (const FString Refusal = RefuseSellDrop(Drag->Line, bInCatalogue); !Refusal.IsEmpty())
	{
		// Said rather than swallowed. A refused drop and a missed one look identical.
		Controller->ShowTransientMessage(Refusal, ESpaceMMOMessageTone::Warning);

		return true;
	}

	// The same two steps a player would take by hand: pick the item, then open the sell prompt --
	// which is where the price is decided, because a drop says what and how many but never at what.
	SelectMarketItem(Drag->Line.ItemDefId);

	OpenOrderPrompt(true, Drag->Line.Quantity);

	return true;
}

void USpaceMMOStationOverlay::BeginOrder(const bool bSell)
{
	// Nothing dragged, so the prompt starts on everything they could back.
	OpenOrderPrompt(bSell, 0);
}

void USpaceMMOStationOverlay::OpenOrderPrompt(const bool bSell, const int32 StartingQuantity)
{
	const USpaceMMOBackendClient* Client = MarketClient();

	if (Client == nullptr || SelectedItemDefId == 0)
	{
		return;
	}

	const FBackendMarketListing* Listing = Client->GetMarketListings().FindByPredicate(
		[this](const FBackendMarketListing& L) { return L.ItemDefId == SelectedItemDefId; });

	if (Listing == nullptr)
	{
		return;
	}

	// What they could actually back a sell order with: goods in this station's hangar and goods in
	// their pockets, because the server moves the pocketed ones across before it fills. Counting
	// only the hangar greyed Sell out for somebody standing in the station holding the ore, which
	// reads as a broken button rather than as a rule.
	int32 Held = 0;

	const ASpaceMMOPlayerController* Controller =
		Cast<ASpaceMMOPlayerController>(GetOwningPlayer());

	const int32 Station = Controller != nullptr ? Controller->DockedStationId() : 0;

	for (const FBackendInventoryItem& Stack : Client->GetInventory())
	{
		if (Stack.ItemDefId != SelectedItemDefId)
		{
			continue;
		}

		const bool bInThisHangar =
			Stack.Kind == EBackendInventoryKind::StationHangar && Stack.StationId == Station;

		// Pockets travel with the character, so they count wherever they are docked. A hold or
		// another station's hangar does not: the server would not reach into either.
		const bool bInPockets = Stack.Kind == EBackendInventoryKind::CharacterCarried;

		if (bInThisHangar || bInPockets)
		{
			Held += Stack.Quantity;
		}
	}

	PendingOrderItemDefId = SelectedItemDefId;

	// Selling matches what others are asking; buying matches what others are bidding. Each side is
	// suggested the price it would actually have to beat.
	const bool bHasMarket = bSell ? Listing->bHasAsk : Listing->bHasBid;

	const int64 MarketPrice = bSell ? Listing->BestAskMinorUnits : Listing->BestBidMinorUnits;

	// What the prompt opens on: the stack that was dragged, clamped to what could actually back the
	// order, or everything when it was opened from a button. Dragging 120 out of a hangar while
	// carrying 40 more offers 120 and lets it be raised -- the drag said which stack, not a limit.
	const int32 Starting = StartingQuantity > 0 ? FMath::Min(StartingQuantity, Held) : Held;

	// Sell side only. A standing order buys raw material; it does not sell any, so there is no
	// guaranteed price to pay when placing a buy order.
	OnOrderRequested(
		Listing->Name,
		bSell,
		Held > 0,
		Held,
		Starting,
		MarketPrice,
		bHasMarket,
		Listing->GuaranteedPriceMinorUnits,
		bSell && Listing->bHasGuaranteed);
}

void USpaceMMOStationOverlay::CancelOrder()
{
	PendingOrderItemDefId = 0;
}

FString USpaceMMOStationOverlay::CreditsText(const int64 MinorUnits)
{
	return FSpaceMMOBackendProtocol::FormatCredits(MinorUnits);
}

void USpaceMMOStationOverlay::ConfirmOrderInCredits(
	const bool bSell, const FString& Price, const int32 Quantity)
{
	int64 MinorUnits = 0;

	if (!FSpaceMMOBackendProtocol::ParseCredits(Price, MinorUnits) || MinorUnits <= 0)
	{
		// Said rather than swallowed. A price box that quietly does nothing is indistinguishable
		// from a dead button, and the pending order is deliberately left standing so the player can
		// correct the number rather than starting again.
		if (ASpaceMMOPlayerController* Controller = Cast<ASpaceMMOPlayerController>(GetOwningPlayer()))
		{
			Controller->ShowTransientMessage(
				FString::Printf(TEXT("'%s' is not a price"), *Price),
				ESpaceMMOMessageTone::Warning);
		}

		return;
	}

	ConfirmOrder(bSell, MinorUnits, Quantity);
}

void USpaceMMOStationOverlay::ConfirmOrder(
	const bool bSell, const int64 PriceMinorUnits, const int32 Quantity)
{
	const int32 ItemDefId = PendingOrderItemDefId;

	// Cleared first, so a prompt that somehow answers twice cannot place two orders.
	CancelOrder();

	ASpaceMMOPlayerController* Controller =
		Cast<ASpaceMMOPlayerController>(GetOwningPlayer());

	USpaceMMOBackendClient* Client = MarketClient();

	if (Controller == nullptr || Client == nullptr || ItemDefId == 0 || Quantity <= 0)
	{
		return;
	}

	// Floored at a minor unit rather than sent as typed. Asks are floored server-side anyway
	// (economy-design §: "the lowest a price can go is 0.01 cr"), and an order at zero would be
	// refused after the player had already been told it was placed.
	const int64 Price = FMath::Max(PriceMinorUnits, 1LL);

	Client->PlaceOrder(
		Controller->GetCharacterId(),
		Controller->DockedStationId(),
		ItemDefId,
		bSell ? EBackendOrderSide::Sell : EBackendOrderSide::Buy,
		Price,
		Quantity);

	Controller->ShowTransientMessage(
		FString::Printf(
			TEXT("%s %d at %s cr"),
			bSell ? TEXT("Selling") : TEXT("Buying"),
			Quantity,
			*FSpaceMMOBackendProtocol::FormatCredits(Price)),
		ESpaceMMOMessageTone::Positive);
}

void USpaceMMOStationOverlay::SetTab(const ESpaceMMOStationTab Tab)
{
	ActiveTab = Tab;

	bMarketTab = Tab == ESpaceMMOStationTab::Market;
	bIndustryTab = Tab == ESpaceMMOStationTab::Industry;
	bQuestsTab = Tab == ESpaceMMOStationTab::Quests;
	bMyOrdersTab = Tab == ESpaceMMOStationTab::MyOrders;

	// Asked for again every time the tab is opened, not once ever. A bool that latched was exactly
	// the bug the catalogue had at a different station: the list is only wrong while somebody is
	// looking at a stale copy of it, and opening the tab is when they start.
	if (bMyOrdersTab)
	{
		bRequestedMyOrders = false;
	}
}

void USpaceMMOStationOverlay::FillPanel(
	UPanelWidget* Container,
	const TArray<FString>& Lines,
	FString& Signature)
{
	if (Container == nullptr)
	{
		return;
	}

	// Rebuilt only when the wording changed. Industry counts down every second and the market moves
	// whenever anybody trades, so most frames still have nothing new to say.
	const FString Next = FString::Join(Lines, TEXT("\n"));

	if (Next == Signature)
	{
		return;
	}

	Signature = Next;

	Container->ClearChildren();

	for (const FString& Line : Lines)
	{
		USpaceMMOTextRow* Row = CreateWidget<USpaceMMOTextRow>(GetOwningPlayer(), RowClass);

		if (Row == nullptr)
		{
			continue;
		}

		Row->SetLine(Line);

		Container->AddChild(Row);
	}
}

void USpaceMMOStationOverlay::NativeTick(const FGeometry& Geometry, const float DeltaSeconds)
{
	Super::NativeTick(Geometry, DeltaSeconds);

	// Never call SetVisibility on this widget from here — see the note on
	// USpaceMMOFlightReadout::NativeTick. UpdateHudContext owns the Tab toggle.
	const ASpaceMMOPlayerController* Controller =
		Cast<ASpaceMMOPlayerController>(GetOwningPlayer());

	if (Controller == nullptr)
	{
		return;
	}

	if (RowClass == nullptr || (IndustryRows == nullptr && QuestRows == nullptr))
	{
		// Warned once rather than per tick: it is a wiring mistake, not an event. Without this a
		// station overlay that opens completely empty is indistinguishable from a station with
		// nothing for sale, no recipes and no quests.
		if (!bWarnedAboutWiring)
		{
			bWarnedAboutWiring = true;

			UE_LOG(LogSpaceMMOBackend, Warning,
				TEXT("HUD: the station overlay shows nothing — %s. Set them in the Widget "
					"Blueprint; IndustryRows and QuestRows are bound by name and RowClass in "
					"Class Defaults."),
				RowClass == nullptr
					? TEXT("no RowClass set")
					: TEXT("neither IndustryRows nor QuestRows is bound"));
		}

		return;
	}

	FString StationName;

	TArray<FString> Industry;
	TArray<FString> Quests;

	Controller->GetStationPanels(StationName, Industry, Quests);

	if (StationNameText != nullptr)
	{
		StationNameText->SetText(FText::FromString(StationName));
	}

	// All three are filled rather than only the visible one. They are cheap while unchanged, and a
	// tab that rebuilt on being switched to would flicker on every switch — which is the moment a
	// player is looking straight at it.

	// The book under the catalogue. Rows rather than lines, because each one carries a button.
	if (BookRows != nullptr && BookRowClass != nullptr)
	{
		if (const USpaceMMOBackendClient* Client = MarketClient())
		{
			// Only when the book in hand is the selected item's. A fetch is in flight for a frame or
			// two after a click, and rendering the previous item's orders under a new name offers a
			// button that would trade the wrong thing.
			const bool bMine =
				SelectedItemDefId != 0 && Client->GetBookItemDefId() == SelectedItemDefId;

			// Nothing at all until an item is picked; "loading..." once one is, until its book lands.
			const TArray<FSpaceMMOBookRowText> Rows = SelectedItemDefId != 0
				? BuildBookRows(Client->GetBook(), bMine)
				: TArray<FSpaceMMOBookRowText>();

			FString Signature;

			for (const FSpaceMMOBookRowText& Row : Rows)
			{
				Signature += Row.Heading + Row.Quantity + Row.Price + Row.ActionLabel
					+ (Row.bCanTake ? TEXT("+") : TEXT("-")) + TEXT("|");
			}

			if (Signature != BookSignature)
			{
				BookSignature = Signature;

				BookRows->ClearChildren();

				for (const FSpaceMMOBookRowText& Row : Rows)
				{
					USpaceMMOBookRow* Widget =
						CreateWidget<USpaceMMOBookRow>(GetOwningPlayer(), BookRowClass);

					if (Widget == nullptr)
					{
						continue;
					}

					Widget->SetOwningOverlay(this);
					Widget->SetRow(Row);

					BookRows->AddChild(Widget);
				}
			}
		}
	}

	// The resting-order list. Asked for when the tab is first opened rather than on a timer: it
	// changes when the player places or withdraws something, and both of those already refetch.
	if (MyOrderRows != nullptr && MyOrderRowClass != nullptr)
	{
		if (USpaceMMOBackendClient* Client = MarketClient())
		{
			const ASpaceMMOPlayerController* Owner =
				Cast<ASpaceMMOPlayerController>(GetOwningPlayer());

			if (ActiveTab == ESpaceMMOStationTab::MyOrders && !bRequestedMyOrders && Owner != nullptr)
			{
				bRequestedMyOrders = true;

				Client->FetchMyOrders(Owner->GetCharacterId());
			}

			const TArray<FSpaceMMOMyOrderRowText> Rows = BuildMyOrderRows(
				Client->GetMyOrders(), Owner != nullptr ? Owner->DockedStationId() : 0);

			FString Signature;

			for (const FSpaceMMOMyOrderRowText& Row : Rows)
			{
				Signature += Row.Side + Row.Item + Row.Quantity + Row.Price + Row.Station + TEXT("|");
			}

			if (Signature != MyOrdersSignature)
			{
				MyOrdersSignature = Signature;

				MyOrderRows->ClearChildren();

				for (const FSpaceMMOMyOrderRowText& Row : Rows)
				{
					USpaceMMOMyOrderRow* Widget =
						CreateWidget<USpaceMMOMyOrderRow>(GetOwningPlayer(), MyOrderRowClass);

					if (Widget == nullptr)
					{
						continue;
					}

					Widget->SetOwningOverlay(this);
					Widget->SetRow(Row);

					MyOrderRows->AddChild(Widget);
				}

				if (MyOrdersFooterText != nullptr)
				{
					MyOrdersFooterText->SetText(
						FText::FromString(BuildMyOrdersFooter(Client->GetMyOrders())));
				}
			}
		}
	}

	// The catalogue above the book. Asked for once when the overlay first has a station to ask
	// about, and again whenever the search changes -- not every tick, because a request per frame
	// would hammer the API for a list that changes when somebody trades.
	if (MarketListingRows != nullptr && MarketRowClass != nullptr)
	{
		if (USpaceMMOBackendClient* Client = MarketClient())
		{
			const ASpaceMMOPlayerController* Docked =
				Cast<ASpaceMMOPlayerController>(GetOwningPlayer());

			const int32 Station = Docked != nullptr ? Docked->DockedStationId() : 0;

			// Forgotten on undocking, so returning to the same station asks again rather than
			// showing whatever the book looked like before the player went away and traded.
			if (Station != ListedStationId)
			{
				ListedStationId = Station;

				if (Station != 0)
				{
					RefreshMarketListings();
				}
			}

			const TArray<FSpaceMMOMarketRowText> Listings =
				BuildMarketRows(Client->GetMarketListings());

			FString Signature;

			for (const FSpaceMMOMarketRowText& Row : Listings)
			{
				Signature += Row.Name + Row.Sell + Row.Buy + Row.Quantity + TEXT("|");
			}

			if (Signature != ListingSignature)
			{
				ListingSignature = Signature;

				MarketListingRows->ClearChildren();

				for (const FSpaceMMOMarketRowText& Row : Listings)
				{
					USpaceMMOMarketRow* Widget =
						CreateWidget<USpaceMMOMarketRow>(GetOwningPlayer(), MarketRowClass);

					if (Widget == nullptr)
					{
						continue;
					}

					Widget->SetOwningOverlay(this);
					Widget->SetRow(Row);
					Widget->bSelected = Row.ItemDefId == SelectedItemDefId;

					MarketListingRows->AddChild(Widget);
				}
			}
		}
	}
	FillPanel(IndustryRows, Industry, IndustrySignature);
	FillPanel(QuestRows, Quests, QuestSignature);
}
