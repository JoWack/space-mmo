#pragma once

#include "Blueprint/DragDropOperation.h"
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

	/**
	 * Which container this line belongs to.
	 *
	 * Every line carries it, headings included — which is what makes the whole group a drop target
	 * rather than only its heading, without a widget per group to drop onto.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	int64 InventoryId = 0;

	/** The stack's item, or 0 on a heading or an instance. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	int32 ItemDefId = 0;

	/** The instance's id, or 0 on a heading or a stack. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	int64 InstanceId = 0;

	/** How many are here, for a stack. Instances are always one and move whole. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	int32 Quantity = 0;

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

	/** Whether this line names something that can be picked up and moved. */
	bool CanDrag() const
	{
		return !bIsHeading && bReachable && (ItemDefId != 0 || InstanceId != 0);
	}
};

/**
 * Goods in transit between two containers.
 *
 * Carries the whole source line rather than an id, because the drop needs to know what is being
 * moved as well as where from — an instance moves whole, a stack asks how many, and the difference
 * is a property of the thing being dragged.
 */
UCLASS()
class SPACEMMOBACKEND_API USpaceMMOInventoryDrag : public UDragDropOperation
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	FSpaceMMOInventoryLine Line;
};

/** One row. Its own widget so the Blueprint owns what a row looks like. */
UCLASS()
class SPACEMMOBACKEND_API USpaceMMOInventoryRow : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Fills the row in. Called by the screen; nothing else needs it. */
	void SetLine(const FSpaceMMOInventoryLine& Line);

	const FSpaceMMOInventoryLine& GetLine() const { return Line; }

	/** Set by the screen so a dropped row knows who to tell. */
	void SetOwningScreen(class USpaceMMOInventoryScreen* Screen) { OwningScreen = Screen; }

	/**
	 * Whether a legal drop is hovering over this row.
	 *
	 * Bind a highlight to it. Every row in a container reports this together, so the whole group
	 * lights up rather than the one line under the cursor — a group is the target, and a single
	 * highlighted row would suggest goods land *there* rather than in the container.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	bool bIsDropTarget = false;

	/** Bind a heading's weight or colour to this. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	bool bIsHeading = false;

	/** Bind dimming to this. False means the goods are somewhere else. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	bool bReachable = true;

protected:
	virtual FReply NativeOnMouseButtonDown(
		const FGeometry& Geometry, const FPointerEvent& Event) override;

	virtual void NativeOnDragDetected(
		const FGeometry& Geometry,
		const FPointerEvent& Event,
		UDragDropOperation*& OutOperation) override;

	virtual bool NativeOnDrop(
		const FGeometry& Geometry,
		const FDragDropEvent& Event,
		UDragDropOperation* Operation) override;

	virtual void NativeOnDragEnter(
		const FGeometry& Geometry,
		const FDragDropEvent& Event,
		UDragDropOperation* Operation) override;

	virtual void NativeOnDragLeave(
		const FDragDropEvent& Event, UDragDropOperation* Operation) override;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> LabelText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> AmountText;

private:
	FSpaceMMOInventoryLine Line;

	TWeakObjectPtr<class USpaceMMOInventoryScreen> OwningScreen;
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
		const TArray<FBackendInventoryContainer>& Containers,
		const TArray<FBackendStation>& Stations,
		int32 DockedStationId);

	/**
	 * Whether goods named by one line may be dropped on another.
	 *
	 * Pure and static so every case is testable without a mouse: what may be picked up, what may
	 * receive it, and the one that is easy to get wrong — a drop back into the container the goods
	 * are already in, which would send the server a move from a place to itself.
	 */
	static bool CanDrop(const FSpaceMMOInventoryLine& Source, const FSpaceMMOInventoryLine& Target);

	/**
	 * Begins a move, asking how many first if the goods are a stack.
	 *
	 * An instance goes immediately: it carries its own condition, so it is one thing and there is
	 * nothing to ask. A stack fires <c>OnQuantityRequested</c> and waits for
	 * <c>ConfirmQuantity</c> — always, rather than behind a modifier key, because a modifier is
	 * invisible until somebody tells you about it and the prompt arrives pre-filled with everything.
	 */
	void BeginTransfer(const FSpaceMMOInventoryLine& Source, int64 ToInventoryId);

	/** Called by the Blueprint's quantity prompt. Clamped, so a typed number cannot invent goods. */
	UFUNCTION(BlueprintCallable, Category = "SpaceMMO|HUD")
	void ConfirmQuantity(int32 Quantity);

	/** Called by the Blueprint's quantity prompt when the player backs out. */
	UFUNCTION(BlueprintCallable, Category = "SpaceMMO|HUD")
	void CancelQuantity();

	/** Marks every row in a container as a drop target, or clears it with 0. */
	void SetDropTargetContainer(int64 InventoryId);

	/** Builds one row widget from a line. Used for the list and for the thing under the cursor. */
	USpaceMMOInventoryRow* MakeRow(const FSpaceMMOInventoryLine& Line);

protected:
	virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;

	/**
	 * Asks the Blueprint to show its quantity prompt.
	 *
	 * The prompt is entirely the designer's — a slider, a box, a row of buttons — because how you
	 * ask for a number is a look rather than a rule. C++ supplies the name and the most that can be
	 * moved, and waits for ConfirmQuantity or CancelQuantity.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "SpaceMMO|HUD")
	void OnQuantityRequested(const FString& ItemName, int32 MaxQuantity);

	/** The container rows are added to. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UPanelWidget> InventoryRows;

	/** What one row looks like. Set this in the Widget Blueprint's class defaults. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SpaceMMO|HUD")
	TSubclassOf<USpaceMMOInventoryRow> RowClass;

private:
	/** The move waiting on a quantity, if any. */
	FSpaceMMOInventoryLine PendingSource;

	int64 PendingDestination = 0;

	/** Rows currently lit as a drop target, so the highlight can be cleared without a search. */
	int64 HighlightedContainer = 0;

	/** What the rows were last built from, so they are rebuilt only when something changed. */
	FString RowSignature;

	/** So a missing InventoryRows or RowClass is said once rather than sixty times a second. */
	bool bWarnedAboutWiring = false;
};
