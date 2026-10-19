#pragma once

#include "Blueprint/DragDropOperation.h"
#include "CoreMinimal.h"
#include "SpaceMMODeathWire.h"
#include "SpaceMMOPairedPanel.h"
#include "SpaceMMOPanelRow.h"

#include "SpaceMMOLootScreen.generated.h"

class ASpaceMMOLootCrateActor;
class USpaceMMOLootScreen;

/** A stack being dragged out of a crate, onto the pockets (task 103). */
UCLASS()
class SPACEMMOBACKEND_API USpaceMMOLootDrag : public UDragDropOperation
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FBackendLootItem Item;

	/** Who to tell when it is dropped: the window takes it from its crate. */
	TWeakObjectPtr<USpaceMMOLootScreen> Screen;
};

/** One stack in a crate's window: its name, its world and how many. Double-click or drag it to take only that. */
UCLASS()
class SPACEMMOBACKEND_API USpaceMMOLootRow : public USpaceMMOPanelRow
{
	GENERATED_BODY()

public:
	void SetItem(const FBackendLootItem& InItem, USpaceMMOLootScreen* InScreen);

	const FBackendLootItem& GetItem() const { return Item; }

protected:
	virtual void NativeConstruct() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply NativeOnMouseButtonDoubleClick(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual void NativeOnDragDetected(
		const FGeometry& Geometry, const FPointerEvent& Event, UDragDropOperation*& OutOperation) override;
	virtual void StyleTexts(SpaceMMO::Style::ERowLook InLook) override;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> NameText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> StyleTagText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> QuantityText;

private:
	FBackendLootItem Item;

	TWeakObjectPtr<USpaceMMOLootScreen> Screen;
};

/**
 * A dropped crate's window (task 103), as Joe chose it on 19 October: who dropped it and the time left, a row per
 * material, and Take all; a row double-clicked, or dragged onto the pockets, takes only that. It opens on E beside the
 * inventory, where what is taken goes, and takes the left as the station and equipment screens do.
 *
 * <code>
 * DROPPED BY AYLA1204   9:12 |  INVENTORY
 * -------------------------- |  ...
 * Ferrite Ore            5   |
 * Structural Plate       3   |
 *       [ Take all ]         |
 * </code>
 *
 * Reads the crate the server replicates, so a stack someone else takes leaves this window as it leaves the crate. It
 * closes itself when the crate is gone, out of reach, or its reader is dead.
 */
UCLASS()
class SPACEMMOBACKEND_API USpaceMMOLootScreen : public USpaceMMOPairedPanel
{
	GENERATED_BODY()

public:
	/** "9:12": minutes and seconds left, counted up to the next whole second so it reaches 0:00 as it goes. */
	static FString TimeLeft(double Seconds);

	/** "Dropped by Ayla1204", which the title's style puts in capitals. */
	static FString Title(const FString& DroppedBy);

	/** Which crate it shows. Set by the controller as it opens. */
	void ShowCrate(ASpaceMMOLootCrateActor* Crate);

	ASpaceMMOLootCrateActor* GetCrate() const { return Crate.Get(); }

	/** Takes one stack, or everything that fits when none is given. */
	void Take(const TOptional<FBackendLootItem>& Item);

	/** Makes a row, for the list and for a drag's visual. */
	USpaceMMOLootRow* MakeRow(const FBackendLootItem& Item);

	/** The row Blueprint. Set by SpaceMMOBuildMenus on the screen's class defaults. */
	UPROPERTY(EditDefaultsOnly, Category = "SpaceMMO|HUD")
	TSubclassOf<USpaceMMOLootRow> RowClass;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;

	UFUNCTION()
	void HandleTakeAll();

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TitleText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TimeText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UPanelWidget> LootRows;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UButton> TakeAllButton;

private:
	void Rebuild();

	TWeakObjectPtr<ASpaceMMOLootCrateActor> Crate;

	/** The crate's contents' revision these rows show, so they are made again only when it changes. */
	int32 ShownRevision = -1;
};

/**
 * The prompt over a dropped crate in reach (task 103), as the deposit's is over a rock: who dropped it, the time left,
 * what is in it, and the key that opens it.
 *
 * <code>
 * DROPPED BY AYLA1204         9:12
 * Ferrite Ore 5 · Structural Plate 3
 * [E] open
 * </code>
 *
 * Shown only while E means the crate (<c>USpaceMMOLootComponent::CrateForE</c>); the deposit prompt stands aside then.
 * Does not set its own visibility, which is <c>ASpaceMMOPlayerController::UpdateHudContext</c>'s; it collapses its
 * root when there is nothing to say.
 */
UCLASS()
class SPACEMMOBACKEND_API USpaceMMOLootPrompt : public UUserWidget
{
	GENERATED_BODY()

public:
	/** "Ferrite Ore 5 · Structural Plate 3". */
	static FString Contents(const TArray<FBackendLootItem>& Items);

	/** How far over the crate's base the prompt floats, in metres: over the crate, not up the beam. */
	static constexpr double HeightMetres = 1.4;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;

	/** What moves over the crate. A direct child of a canvas panel. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UWidget> PromptRoot;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TitleText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TimeText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> ContentsText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> KeyText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> ActionText;
};
