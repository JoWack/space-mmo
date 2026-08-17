#include "SpaceMMOStationOverlay.h"

#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "SpaceMMOBackendClient.h"
#include "SpaceMMOBackendLog.h"
#include "SpaceMMOBackendProtocol.h"
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

void USpaceMMOStationOverlay::BeginOrder(const bool bSell)
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

	// What they could actually back a sell order with: goods in this station's hangar, because an
	// order is filled from there. Ore in their pockets is not on the market until they put it down.
	int32 Held = 0;

	const ASpaceMMOPlayerController* Controller =
		Cast<ASpaceMMOPlayerController>(GetOwningPlayer());

	const int32 Station = Controller != nullptr ? Controller->DockedStationId() : 0;

	for (const FBackendInventoryItem& Stack : Client->GetInventory())
	{
		if (Stack.ItemDefId == SelectedItemDefId
			&& Stack.Kind == EBackendInventoryKind::StationHangar
			&& Stack.StationId == Station)
		{
			Held += Stack.Quantity;
		}
	}

	PendingOrderItemDefId = SelectedItemDefId;

	// Selling matches what others are asking; buying matches what others are bidding. Each side is
	// suggested the price it would actually have to beat.
	const bool bHasMarket = bSell ? Listing->bHasAsk : Listing->bHasBid;

	const int64 MarketPrice = bSell ? Listing->BestAskMinorUnits : Listing->BestBidMinorUnits;

	// Sell side only. A standing order buys raw material; it does not sell any, so there is no
	// guaranteed price to pay when placing a buy order.
	OnOrderRequested(
		Listing->Name,
		bSell,
		Held > 0,
		Held,
		MarketPrice,
		bHasMarket,
		Listing->GuaranteedPriceMinorUnits,
		bSell && Listing->bHasGuaranteed);
}

void USpaceMMOStationOverlay::CancelOrder()
{
	PendingOrderItemDefId = 0;
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

	if (RowClass == nullptr
		|| (MarketRows == nullptr && IndustryRows == nullptr && QuestRows == nullptr))
	{
		// Warned once rather than per tick: it is a wiring mistake, not an event. Without this a
		// station overlay that opens completely empty is indistinguishable from a station with
		// nothing for sale, no recipes and no quests.
		if (!bWarnedAboutWiring)
		{
			bWarnedAboutWiring = true;

			UE_LOG(LogSpaceMMOBackend, Warning,
				TEXT("HUD: the station overlay shows nothing — %s. Set them in the Widget "
					"Blueprint; MarketRows, IndustryRows and QuestRows are bound by name and "
					"RowClass in Class Defaults."),
				RowClass == nullptr
					? TEXT("no RowClass set")
					: TEXT("none of MarketRows, IndustryRows or QuestRows is bound"));
		}

		return;
	}

	FString StationName;

	TArray<FString> Market;
	TArray<FString> Industry;
	TArray<FString> Quests;

	Controller->GetStationPanels(StationName, Market, Industry, Quests);

	if (StationNameText != nullptr)
	{
		StationNameText->SetText(FText::FromString(StationName));
	}

	// All three are filled rather than only the visible one. They are cheap while unchanged, and a
	// tab that rebuilt on being switched to would flicker on every switch — which is the moment a
	// player is looking straight at it.
	FillPanel(MarketRows, Market, MarketSignature);

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
