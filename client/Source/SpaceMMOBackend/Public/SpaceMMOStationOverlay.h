#pragma once

#include "Blueprint/UserWidget.h"
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
class SPACEMMOBACKEND_API USpaceMMOStationOverlay : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Which tab is showing. Set by the number keys while the overlay is open. */
	void SetTab(ESpaceMMOStationTab Tab);

	ESpaceMMOStationTab GetTab() const { return ActiveTab; }

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

	/** So a missing container or RowClass is said once rather than sixty times a second. */
	bool bWarnedAboutWiring = false;
};
