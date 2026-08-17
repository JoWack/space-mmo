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
	static TArray<FSpaceMMOMarketRowText> BuildMarketRows(
		const TArray<FBackendMarketListing>& Listings);

	/** Picks the item whose book is shown below the list. */
	void SelectMarketItem(int32 ItemDefId);

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

protected:
	virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;

	/** Where the player is docked, so the overlay says which station this is. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> StationNameText;

	/**
	 * Asks the Blueprint to show its order prompt.
	 *
	 * @param bCanSell        False when the player holds none of it, so Sell can be greyed rather
	 *                        than offering an order they cannot back.
	 * @param HeldQuantity    The most they could sell.
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
		int64 MarketPriceMinorUnits,
		bool bHasMarket,
		int64 GuaranteedPriceMinorUnits,
		bool bHasGuaranteed);

	/** The market's item list, above the book. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UPanelWidget> MarketListingRows;

	/** What one market row looks like. Set this in the Widget Blueprint's class defaults. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SpaceMMO|HUD")
	TSubclassOf<USpaceMMOMarketRow> MarketRowClass;

	/** One container per tab. Rows are rebuilt into whichever is showing. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UPanelWidget> MarketRows;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UPanelWidget> IndustryRows;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UPanelWidget> QuestRows;

	/** What one line looks like. Set this in the Widget Blueprint's class defaults. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SpaceMMO|HUD")
	TSubclassOf<USpaceMMOTextRow> RowClass;

private:
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
	FString MarketSignature;
	FString IndustrySignature;
	FString QuestSignature;

	/** What the listing rows were last built from. */
	FString ListingSignature;

	/** Which item's book is shown, or 0. */
	int32 SelectedItemDefId = 0;

	FString MarketSearch;

	/** The order waiting on a price and an amount. */
	int32 PendingOrderItemDefId = 0;

	/** So the catalogue is asked for once rather than every tick. */
	bool bRequestedListings = false;

	/** So a missing container or RowClass is said once rather than sixty times a second. */
	bool bWarnedAboutWiring = false;
};
