#pragma once

#include "SpaceMMOPairedPanel.h"
#include "CoreMinimal.h"

#include "SpaceMMOStationOverlay.generated.h"

/** Which half of the station a player is looking at. */
UENUM(BlueprintType)
enum class ESpaceMMOStationTab : uint8
{
	Market,
	Industry,
	Quests,

	/**
	 * What this character has resting on a book, anywhere.
	 *
	 * The one tab that is not about the station being stood in. It is here rather than on its own
	 * screen because it is read while trading and acted on the same way, but the orders it lists may
	 * be anywhere -- see task 119.
	 */
	MyOrders,

	/**
	 * The hulls this character owns, and which of them can be brought here (ADR-0012).
	 *
	 * On the station overlay rather than behind a key, because summoning is a thing you do standing
	 * still at a station, and it belongs where the rest of a station's business is.
	 */
	Ships,
};

/**
 * One hull a character owns, as a row in the Ships tab.
 */
USTRUCT(BlueprintType)
struct SPACEMMOBACKEND_API FSpaceMMOShipRowText
{
	GENERATED_BODY()

	/** The instance id, which is what a summon names. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	int64 HullId = 0;

	/** "Shuttle", "Freighter". */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	FString Name;

	/** "Here", or "At another station" -- where the hull is now. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	FString Where;

	/** Condition as a player reads it, e.g. "100%". */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	FString Condition;

	/**
	 * Why the button is off, or empty when it is on.
	 *
	 * A reason rather than a disabled control with no explanation: "Summon" greyed out with nothing
	 * beside it is the interface telling somebody they are wrong without saying about what.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	FString Refusal;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	bool bCanSummon = false;

	/** True for the ship this character currently flies. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	bool bIsActive = false;
};

/**
 * One item on the market screen, already worded.
 *
 * A row per item in the tradeable catalogue rather than per order: prices are what a player compares,
 * and the book underneath answers "at what depth" once they have picked one.
 */
USTRUCT(BlueprintType)
struct SPACEMMOBACKEND_API FSpaceMMOMarketRowText
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	int32 ItemDefId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	FString Name;

	/** "20.00 cr", or a dash when nobody is selling. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	FString Sell;

	/** "18.00 cr", or a dash when nobody is buying. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	FString Buy;

	/** How much is for sale, or empty when none is. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	FString Quantity;

	/** Whether anybody trades this here at all, for dimming a row nobody has touched. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	bool bTraded = false;
};

/** One market row's widget. Its own so the Blueprint owns the columns. */
UCLASS()
class SPACEMMOBACKEND_API USpaceMMOMarketRow : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetRow(const FSpaceMMOMarketRowText& Row);

	const FSpaceMMOMarketRowText& GetRow() const { return Row; }

	void SetOwningOverlay(class USpaceMMOStationOverlay* Overlay) { OwningOverlay = Overlay; }

	/** Bind a highlight to this. The selected row is the one the book below belongs to. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	bool bSelected = false;

	/** Bind dimming to this: nobody is trading this item here yet. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	bool bTraded = false;

protected:
	virtual FReply NativeOnMouseButtonDown(
		const FGeometry& Geometry, const FPointerEvent& Event) override;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> NameText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> SellText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> BuyText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> QuantityText;

private:
	FSpaceMMOMarketRowText Row;

	TWeakObjectPtr<class USpaceMMOStationOverlay> OwningOverlay;
};

/** One row of the book: either a side heading, or an order somebody could take. */
USTRUCT(BlueprintType)
struct SPACEMMOBACKEND_API FSpaceMMOBookRowText
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	int64 OrderId = 0;

	/** True for "SELLING" and "BUYING". Headings carry no price and no button. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	bool bIsHeading = false;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	FString Heading;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	FString Quantity;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	FString Price;

	/** "Buy 4" or "Sell 100" — sized to this row, because taking it fills exactly this much. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	FString ActionLabel;

	/**
	 * Whether the button does anything.
	 *
	 * False on your own orders. Matching refuses a self-trade, so taking one places an order that
	 * cannot cross and simply rests — a crossed book that looks like a broken market.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	bool bCanTake = false;
};

/** One book row's widget. Its own, because each carries a button. */
UCLASS()
class SPACEMMOBACKEND_API USpaceMMOBookRow : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetRow(const FSpaceMMOBookRowText& Row);

	void SetOwningOverlay(class USpaceMMOStationOverlay* Overlay) { OwningOverlay = Overlay; }

	/** Takes this order. Wire the row's button to it. */
	UFUNCTION(BlueprintCallable, Category = "SpaceMMO|HUD")
	void Take();

	/** Bind the button's visibility to this, and its enabled state to bCanTake. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	bool bIsHeading = false;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	bool bCanTake = false;

protected:
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> HeadingText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> QuantityText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> PriceText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> ActionText;

private:
	FSpaceMMOBookRowText Row;

	TWeakObjectPtr<class USpaceMMOStationOverlay> OwningOverlay;
};

/** One of your resting orders, already worded. */
USTRUCT(BlueprintType)
struct SPACEMMOBACKEND_API FSpaceMMOMyOrderRowText
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	int64 OrderId = 0;

	/** "SELL" or "BUY". */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	FString Side;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	FString Item;

	/** What is left resting, not what was placed. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	FString Quantity;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	FString Price;

	/** Where it rests. Shown on every row, because an order elsewhere is the one worth finding. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	FString Station;

	/** Whether it rests somewhere other than here, for marking the row. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	bool bElsewhere = false;
};

/** One resting order's widget. Its own, because each row carries a cancel. */
UCLASS()
class SPACEMMOBACKEND_API USpaceMMOMyOrderRow : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetRow(const FSpaceMMOMyOrderRowText& Row);

	void SetOwningOverlay(class USpaceMMOStationOverlay* Overlay) { OwningOverlay = Overlay; }

	/** Withdraws this order. Wire the row's cancel button to it. */
	UFUNCTION(BlueprintCallable, Category = "SpaceMMO|HUD")
	void Cancel();

	/** Bind a marker to this: the order rests at a different station. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	bool bElsewhere = false;

protected:
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> SideText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> ItemText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> QuantityText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> PriceText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> StationText;

private:
	FSpaceMMOMyOrderRowText Row;

	TWeakObjectPtr<class USpaceMMOStationOverlay> OwningOverlay;
};

/**
 * One line of text in a panel.
 *
 * Deliberately dumb: the panel builders return finished strings and this renders one. Those builders
 * are pure static functions with headless tests, and that coverage is the only automated check the
 * HUD's wording has — replacing them with structured widgets would throw it away for a nicer shape.
 */
UCLASS()
class SPACEMMOBACKEND_API USpaceMMOTextRow : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Fills the row in. Called by whatever panel owns it. */
	void SetLine(const FString& Line);

protected:
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> LineText;
};

/**
 * The station screen, shown while docked.
 *
 * An overlay rather than a full screen, so the world stays visible behind it and it is obvious you
 * are somewhere rather than in a menu. Opens on docking and toggles with Tab.
 *
 * <strong>Holdings is not here.</strong> It moved to its own <c>I</c> overlay (task 108), along with
 * skills on <c>K</c>: those are about the character and mean the same everywhere, while market,
 * industry and quests are about the station and are place-bound by ADR-0008.
 *
 * <strong>It does not set its own visibility</strong>; see
 * <c>ASpaceMMOPlayerController::UpdateHudContext</c>, and the note on
 * <c>USpaceMMOFlightReadout::NativeTick</c> for why a widget cannot.
 */
UCLASS()
class SPACEMMOBACKEND_API USpaceMMOStationOverlay : public USpaceMMOPairedPanel
{
	GENERATED_BODY()

public:
	/** Which tab is showing. Set by the number keys while the overlay is open. */
	void SetTab(ESpaceMMOStationTab Tab);

	ESpaceMMOStationTab GetTab() const { return ActiveTab; }

	/**
	 * Words the market list. Pure, static and tested without a widget or a server.
	 *
	 * A dash rather than a zero where nobody is trading: zero is a legal price, and a market showing
	 * "0.00 cr" for an item nobody has ever offered is stating a price that does not exist.
	 */
	/**
	 * One owned hull, already worded.
	 *
	 * <strong>Every row is answerable from where the player is standing.</strong> A hull somewhere
	 * else is still listed -- knowing you own a freighter at the capital is the point of a fleet
	 * list -- but the row says where it is and why the button is off, rather than offering an action
	 * that fails.
	 */
	static TArray<FSpaceMMOShipRowText> BuildShipRows(
		const TArray<FBackendItemInstance>& Owned,
		int32 DockedStationId,
		bool bStationHandlesShips,
		int64 ActiveHullId);

	/** What the tab says when there is nothing in it. */
	static FString BuildShipsFooter(
		const TArray<FBackendItemInstance>& Owned, bool bStationHandlesShips);

	static TArray<FSpaceMMOMarketRowText> BuildMarketRows(
		const TArray<FBackendMarketListing>& Listings);

	/**
	 * Words the resting-order list. Pure, static and tested without a widget or a server.
	 *
	 * Quantity is what is left rather than what was placed: a partly filled order is exactly the one
	 * whose original size would mislead somebody deciding whether to withdraw it.
	 */
	static TArray<FSpaceMMOMyOrderRowText> BuildMyOrderRows(
		const TArray<FBackendMyOrder>& Orders, int32 HereStationId);

	/**
	 * The line under the list: how much is resting, and what it is holding.
	 *
	 * The figure that makes a forgotten order matter. Sell orders hold goods and buy orders hold
	 * credits, so an order nobody remembers is stock and money out of the player's reach -- and
	 * without this they would only discover it by coming up short somewhere else.
	 */
	static FString BuildMyOrdersFooter(const TArray<FBackendMyOrder>& Orders);

	/**
	 * Words the book. Pure, static and tested without a widget or a server.
	 *
	 * Asks cheapest first and bids richest first: each side leads with the price nearest the spread,
	 * which is the one somebody deciding whether to trade would actually get.
	 *
	 * <c>bLoaded</c> separates two things that look identical and mean opposites: nobody is trading
	 * this, and nobody has answered yet. A fetch is in flight for a frame or two after a click, and
	 * an empty book shown during it invites somebody to conclude a market does not exist.
	 */
	static TArray<FSpaceMMOBookRowText> BuildBookRows(
		const TArray<FBackendBookEntry>& Book, bool bLoaded);

	/**
	 * Takes a resting order: places the opposite side at its price, for its remaining quantity.
	 *
	 * <strong>It buys the price, not the order.</strong> Matching is price-time priority, so if
	 * somebody else is resting at the same price and got there first, theirs fills instead. Same
	 * goods at the same price — naming one order would be a different market design.
	 */
	UFUNCTION(BlueprintCallable, Category = "SpaceMMO|HUD")
	void TakeBookOrder(int64 OrderId);

	/**
	 * Why a dragged stack cannot be listed here, or empty if it can. Pure, static and tested.
	 *
	 * A reason rather than a bool, because every one of these refusals is something the player did
	 * deliberately and would otherwise watch fail silently — dragging a hull onto a market that
	 * cannot trade it looks exactly like a drop that missed.
	 */
	static FString RefuseSellDrop(
		const struct FSpaceMMOInventoryLine& Line, bool bInCatalogue);

	/** Withdraws one. Called by a row's cancel button. */
	UFUNCTION(BlueprintCallable, Category = "SpaceMMO|HUD")
	void CancelRestingOrder(int64 OrderId);

	/** Picks the item whose book is shown below the list. */
	void SelectMarketItem(int32 ItemDefId);

	/** Which item's book is being shown, or 0 for none. */
	int32 GetSelectedItemDefId() const { return SelectedItemDefId; }

	/** What the search box holds. Set from the Blueprint; refetches when it changes. */
	UFUNCTION(BlueprintCallable, Category = "SpaceMMO|HUD")
	void SetMarketSearch(const FString& Search);

	/**
	 * Opens the order prompt for the selected item.
	 *
	 * Suggestions come with it because they answer different questions: <em>match market</em> is what
	 * others are offering, and may not exist; <em>guaranteed</em> is the standing order that always
	 * will, and exists only for what a faction buys. An item nobody trades and no faction buys has
	 * neither, which is honest — nobody has said what it is worth yet.
	 */
	UFUNCTION(BlueprintCallable, Category = "SpaceMMO|HUD")
	void BeginOrder(bool bSell);

	/**
	 * Called by the Blueprint's order prompt, with the price exactly as the player typed it.
	 *
	 * <strong>Prefer this to ConfirmOrder.</strong> Credits are stored in hundredths so that adding
	 * prices is exact, but that is a storage decision and no part of the interface should be doing
	 * arithmetic around it. Left to the Blueprint, the first order placed went in at a hundredth of
	 * the price intended: no error, a plausible figure, real money.
	 *
	 * A price that is not a number is refused and says so, rather than being placed as zero.
	 */
	UFUNCTION(BlueprintCallable, Category = "SpaceMMO|HUD")
	void ConfirmOrderInCredits(bool bSell, const FString& Price, int32 Quantity);

	/** Renders minor units the way the market shows them, for the prompt's suggestion buttons. */
	UFUNCTION(BlueprintPure, Category = "SpaceMMO|HUD")
	static FString CreditsText(int64 MinorUnits);

	/** Called by the Blueprint's order prompt. Price is in minor units; 100 to the credit. */
	UFUNCTION(BlueprintCallable, Category = "SpaceMMO|HUD")
	void ConfirmOrder(bool bSell, int64 PriceMinorUnits, int32 Quantity);

	UFUNCTION(BlueprintCallable, Category = "SpaceMMO|HUD")
	void CancelOrder();

	/** Bind each tab body's visibility, and each tab button's highlight, to these. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	bool bMarketTab = true;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	bool bIndustryTab = false;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	bool bQuestsTab = false;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	bool bMyOrdersTab = false;

protected:
	virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;

	/**
	 * Accepts a stack dragged out of the inventory screen, and offers to sell it.
	 *
	 * The whole tab rather than the catalogue list alone: a big target is easier to hit and there is
	 * nothing else on this tab a drop could mean. Selecting the item and opening the sell prompt
	 * reuses the path a player would take by hand — a second listing route would be a second place
	 * for the price to be wrong.
	 */
	virtual bool NativeOnDrop(
		const FGeometry& Geometry,
		const FDragDropEvent& Event,
		UDragDropOperation* Operation) override;

	/** Where the player is docked, so the overlay says which station this is. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> StationNameText;

	/**
	 * Asks the Blueprint to show its order prompt.
	 *
	 * @param bCanSell        False when the player holds none of it, so Sell can be greyed rather
	 *                        than offering an order they cannot back.
	 * @param HeldQuantity    The most they could sell.
	 * @param DefaultQuantity The amount to start on — what was dragged, or everything held when the
	 *                        prompt was opened from a button rather than a drop.
	 * @param bHasMarket      Whether a market price exists to suggest at all.
	 * @param bHasGuaranteed  Whether a standing order exists. Sell side only — factions buy raw
	 *                        material, they do not sell it.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "SpaceMMO|HUD")
	void OnOrderRequested(
		const FString& ItemName,
		bool bSell,
		bool bCanSell,
		int32 HeldQuantity,
		int32 DefaultQuantity,
		int64 MarketPriceMinorUnits,
		bool bHasMarket,
		int64 GuaranteedPriceMinorUnits,
		bool bHasGuaranteed);

	/** The book under the catalogue, as rows rather than lines. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UPanelWidget> BookRows;

	/** What one book row looks like. Set this in the Widget Blueprint's class defaults. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SpaceMMO|HUD")
	TSubclassOf<USpaceMMOBookRow> BookRowClass;

	/** The resting-order list, and its summary line. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UPanelWidget> MyOrderRows;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> MyOrdersFooterText;

	/** What one resting-order row looks like. Set this in the Widget Blueprint's class defaults. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SpaceMMO|HUD")
	TSubclassOf<USpaceMMOMyOrderRow> MyOrderRowClass;

	/** The market's item list, above the book. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UPanelWidget> MarketListingRows;

	/** What one market row looks like. Set this in the Widget Blueprint's class defaults. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SpaceMMO|HUD")
	TSubclassOf<USpaceMMOMarketRow> MarketRowClass;

	/** One container per tab. Rows are rebuilt into whichever is showing. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UPanelWidget> IndustryRows;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UPanelWidget> QuestRows;

	/** What one line looks like. Set this in the Widget Blueprint's class defaults. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SpaceMMO|HUD")
	TSubclassOf<USpaceMMOTextRow> RowClass;

private:
	/** Opens the order prompt, starting on a given amount, or on everything when that is zero. */
	void OpenOrderPrompt(bool bSell, int32 StartingQuantity);

	/** The backend subsystem, or null. */
	class USpaceMMOBackendClient* MarketClient() const;

	/** Asks for the catalogue at this station, with whatever the search box holds. */
	void RefreshMarketListings();

	/** Rebuilds one container from its lines, but only when the lines actually changed. */
	void FillPanel(class UPanelWidget* Container, const TArray<FString>& Lines, FString& Signature);

	ESpaceMMOStationTab ActiveTab = ESpaceMMOStationTab::Market;

	/**
	 * What each panel was last built from.
	 *
	 * Industry counts down every second and the market moves whenever anyone trades, so most frames
	 * still have nothing new to say — and tearing down a few dozen widgets on each one would be
	 * waste that shows up as a stutter rather than as a number.
	 */
	FString IndustrySignature;
	FString QuestSignature;

	/** What the listing rows were last built from. */
	FString ListingSignature;

	/** And the resting-order rows. */
	FString MyOrdersSignature;

	/** And the book rows. */
	FString BookSignature;

	/** So the order list is asked for on opening the tab rather than every tick. */
	bool bRequestedMyOrders = false;

	/** Which item's book is shown, or 0. */
	int32 SelectedItemDefId = 0;

	FString MarketSearch;

	/** The order waiting on a price and an amount. */
	int32 PendingOrderItemDefId = 0;

	/**
	 * Which station the catalogue was asked for, or 0.
	 *
	 * A bool here asked once and never again, which is right until the player docks somewhere else:
	 * the second station would show the first one's goods, at the first one's prices, and look
	 * entirely plausible doing it. Holding the station instead makes moving a reason to refetch.
	 */
	int32 ListedStationId = 0;

	/** So a missing container or RowClass is said once rather than sixty times a second. */
	bool bWarnedAboutWiring = false;
};
