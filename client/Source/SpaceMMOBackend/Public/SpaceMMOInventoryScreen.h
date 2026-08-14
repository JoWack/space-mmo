#pragma once

#include "SpaceMMOPairedPanel.h"
#include "CoreMinimal.h"
#include "SpaceMMOBackendTypes.h"

#include "SpaceMMOInventoryScreen.generated.h"

/**
 * One line of the inventory: either a container's heading or something inside it.
 *
 * Headings and items share a row type because they share a column layout, and because the whole
 * list is one flat sequence — grouping is a property of the order, not of the widget tree.
 */
USTRUCT(BlueprintType)
struct SPACEMMOBACKEND_API FSpaceMMOInventoryLine
{
	GENERATED_BODY()

	/** "SHIP HOLD", "HANGAR — Tycho Trading Hub", or an item's name. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	FString Label;

	/** "250", "cond 87", or empty on a heading. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	FString Amount;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	bool bIsHeading = false;

	/**
	 * Whether these goods can actually be reached from where the player is.
	 *
	 * False for a hangar at a station they are not docked at. Those are listed and dimmed rather
	 * than hidden: the API refuses transfers from elsewhere, so they are genuinely unusable — but
	 * knowing that 1,480 ferrite is sitting two planets away is exactly what a hauling game wants a
	 * player to feel, and hiding it would make the goods simply vanish.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	bool bReachable = true;
};

/** One row. Its own widget so the Blueprint owns what a row looks like. */
UCLASS()
class SPACEMMOBACKEND_API USpaceMMOInventoryRow : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Fills the row in. Called by the screen; nothing else needs it. */
	void SetLine(const FSpaceMMOInventoryLine& Line);

	/** Bind a heading's weight or colour to this. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	bool bIsHeading = false;

	/** Bind dimming to this. False means the goods are somewhere else. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	bool bReachable = true;

protected:
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> LabelText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> AmountText;
};

/**
 * Everything a character owns and where it is, opened with <c>I</c>.
 *
 * <strong>Grouped by container, because that is the fact this screen exists to convey.</strong>
 * Everything gathered or crafted lands in a station hangar, four materials are planet-locked by
 * ADR-0008, and the cross-faction recipe needs all four — so "what do I own" is a much less useful
 * question than "what do I own, and is it here".
 *
 * <strong>Read-only.</strong> Moving goods works over HTTP already (task 99) and wants a selection
 * model and a quantity affordance; seeing what you own and where is a screen on its own, and it is
 * the half that is missing. Transfer arrives later as drag between groups.
 *
 * <strong>It does not set its own visibility</strong>; see
 * <c>ASpaceMMOPlayerController::UpdateHudContext</c>.
 */
UCLASS()
class SPACEMMOBACKEND_API USpaceMMOInventoryScreen : public USpaceMMOPairedPanel
{
	GENERATED_BODY()

public:
	/**
	 * Words and orders the whole list. Pure, static and tested without a widget or a backend.
	 *
	 * Order is carried, then the ship's hold, then hangars — the docked one first and the rest
	 * alphabetically. That puts what is to hand at the top and what is far away at the bottom, which
	 * is the order the questions get asked in.
	 *
	 * Stacks and instances stay apart within a container, because two lasers at different condition
	 * are two things and a quantity of 2 says they are one. ADR-0006 insures each instance against
	 * its own acquisition value, so the distinction is load-bearing rather than cosmetic.
	 */
	static TArray<FSpaceMMOInventoryLine> Build(
		const TArray<FBackendInventoryItem>& Stacks,
		const TArray<FBackendItemInstance>& Instances,
		const TArray<FBackendStation>& Stations,
		int32 DockedStationId);

protected:
	virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;

	/** The container rows are added to. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UPanelWidget> InventoryRows;

	/** What one row looks like. Set this in the Widget Blueprint's class defaults. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SpaceMMO|HUD")
	TSubclassOf<USpaceMMOInventoryRow> RowClass;

private:
	/** What the rows were last built from, so they are rebuilt only when something changed. */
	FString RowSignature;

	/** So a missing InventoryRows or RowClass is said once rather than sixty times a second. */
	bool bWarnedAboutWiring = false;
};
