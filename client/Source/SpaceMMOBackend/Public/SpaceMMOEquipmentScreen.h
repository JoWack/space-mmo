#pragma once

#include "Blueprint/DragDropOperation.h"
#include "CoreMinimal.h"
#include "SpaceMMOBackendTypes.h"
#include "SpaceMMOPairedPanel.h"
#include "SpaceMMOPanelRow.h"

#include "SpaceMMOEquipmentScreen.generated.h"

struct FSpaceMMOInventoryLine;

/** What one slot shows: its name, and what is in it, if anything (task 193). */
USTRUCT(BlueprintType)
struct SPACEMMOBACKEND_API FSpaceMMOEquipmentSlotView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	EBackendEquipmentSlot Slot = EBackendEquipmentSlot::Weapon1;

	/** "Weapon 1", "Head", "Mining". */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	FString SlotName;

	/** What is in it, or empty when nothing is. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	FString ItemName;

	/** The item's id, or zero for an empty slot. What a drag out of the slot names. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	int64 InstanceId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	EBackendItemStyle Style = EBackendItemStyle::Common;

	bool IsEmpty() const { return InstanceId == 0; }
};

/**
 * An item on its way out of a slot: to a container in the inventory, or to the other weapon slot.
 *
 * Its own operation rather than an inventory drag, because what it names is a slot, and taking something
 * off is a different call from moving it.
 */
UCLASS()
class SPACEMMOBACKEND_API USpaceMMOEquipmentDrag : public UDragDropOperation
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	FSpaceMMOEquipmentSlotView View;
};

/**
 * One slot on the equipment screen (task 193).
 *
 * A panel row, so it has the same box, hover and drop-target look as the inventory's rows: a slot is a
 * place to drop things, and should light the way a container does. What is in it and how its two lines sit
 * are the Blueprint's; which slot it is, the screen tells it.
 */
UCLASS()
class SPACEMMOBACKEND_API USpaceMMOEquipmentSlot : public USpaceMMOPanelRow
{
	GENERATED_BODY()

public:
	/** Fills the slot in. Called by the screen whenever what is worn changes. */
	void SetView(const FSpaceMMOEquipmentSlotView& InView);

	const FSpaceMMOEquipmentSlotView& GetView() const { return View; }

	void SetOwningScreen(class USpaceMMOEquipmentScreen* Screen) { OwningScreen = Screen; }

	/** Whether a legal drop is hovering over this slot. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	bool bIsDropTarget = false;

protected:
	virtual SpaceMMO::Style::ERowLook Look() const override;
	virtual void StyleTexts(SpaceMMO::Style::ERowLook InLook) override;

	virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply NativeOnMouseButtonDoubleClick(const FGeometry& Geometry, const FPointerEvent& Event) override;

	virtual void NativeOnDragDetected(
		const FGeometry& Geometry, const FPointerEvent& Event, UDragDropOperation*& OutOperation) override;

	virtual bool NativeOnDrop(
		const FGeometry& Geometry, const FDragDropEvent& Event, UDragDropOperation* Operation) override;

	virtual void NativeOnDragEnter(
		const FGeometry& Geometry, const FDragDropEvent& Event, UDragDropOperation* Operation) override;

	virtual void NativeOnDragLeave(const FDragDropEvent& Event, UDragDropOperation* Operation) override;

	/** "Weapon 1", or "Head:" where the name sits on the item's line. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> SlotNameText;

	/** What is in it, or a dash. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> ItemText;

	/** The coloured style tag after the item (task 187). */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> StyleTagText;

private:
	FSpaceMMOEquipmentSlotView View;

	TWeakObjectPtr<class USpaceMMOEquipmentScreen> OwningScreen;
};

/**
 * What a character wears, opened with <c>L</c> (task 193).
 *
 * <strong>As Joe approved it on 16 October:</strong> the head above a figure, a weapon either side of it,
 * the body and legs below, and a row of tool slots under that. Drag an item from the inventory onto a slot,
 * or double-click it there, to put it on; drag a slot onto a container, or double-click it, to take it off.
 * The figure in the middle is a stand-in until armour has something to show (179).
 *
 * <strong>It does not set its own visibility</strong>; see <c>ASpaceMMOPlayerController::UpdateHudContext</c>,
 * which also decides the side: beside the inventory, where the station screen goes, covering that one
 * while it is open (Joe, 16 October).
 */
UCLASS()
class SPACEMMOBACKEND_API USpaceMMOEquipmentScreen : public USpaceMMOPairedPanel
{
	GENERATED_BODY()

public:
	/**
	 * Every slot, in the enum's order, filled from what the server says is worn. Pure and static, so it is
	 * tested without a widget.
	 *
	 * An item counts as worn only when it is in the Equipped container and has a slot: the server sends
	 * both, and a client that trusted one alone would show a laser in two places during a refresh.
	 */
	static TArray<FSpaceMMOEquipmentSlotView> Build(const TArray<FBackendItemInstance>& Instances);

	/** Which slots a kind of item may go in: the server's Equipment.Fits, mirrored. */
	static bool Fits(EBackendEquipKind Kind, EBackendEquipmentSlot Which);

	/** What a slot is called on screen. "Mining" for the mining tool's, as in the mock. */
	static FString SlotName(EBackendEquipmentSlot Which);

	/**
	 * The slot's name as its box shows it. In the mock Joe approved, a weapon slot's name sits on a line of
	 * its own ("Weapon 1") and every other slot's shares the item's line ("Head:").
	 */
	static FString NameLabel(EBackendEquipmentSlot Which);

	/**
	 * What is in the slot as its box shows it: the item, or for an empty slot "— empty —" under a weapon
	 * slot's name and a bare dash after the others', as in the mock.
	 *
	 * Decided here from the slot rather than set per Blueprint, which is how it was first built: class
	 * defaults the commandlet set on the weapon slot's Blueprint were saved there and never reached the
	 * copies placed inside the screen, which showed the armour slots' wording (16 October, first render).
	 */
	static FString ItemLabel(const FSpaceMMOEquipmentSlotView& View);

	/** Whether a slot is one of the two a weapon goes in. */
	static bool IsWeaponSlot(EBackendEquipmentSlot Which);

	/**
	 * Whether an inventory line may be dropped on a slot: something worn, reachable, and of the kind the
	 * slot takes. Refused while still a drag rather than after a round trip that looked like it worked.
	 */
	static bool CanEquip(const FSpaceMMOInventoryLine& Source, EBackendEquipmentSlot Which);

	/**
	 * Whether a slot may be dropped on another: only a weapon between the two weapon slots, which swaps
	 * them. A laser cannot be dragged into a helmet.
	 */
	static bool CanMoveBetween(const FSpaceMMOEquipmentSlotView& From, EBackendEquipmentSlot To);

	/**
	 * Asks the server to put an item on. Static so the inventory's double-click and a drop here go
	 * through the one route, with the one way of reporting a refusal.
	 */
	static void SendEquip(const UUserWidget* From, int64 InstanceId, TOptional<EBackendEquipmentSlot> Which);

	/** Asks the server to take an item off, into the container named, or the pockets when it is zero. */
	static void SendUnequip(const UUserWidget* From, EBackendEquipmentSlot Which, int64 ToInventoryId);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<USpaceMMOEquipmentSlot> Weapon1Slot;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<USpaceMMOEquipmentSlot> Weapon2Slot;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<USpaceMMOEquipmentSlot> HeadSlot;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<USpaceMMOEquipmentSlot> BodySlot;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<USpaceMMOEquipmentSlot> LegsSlot;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<USpaceMMOEquipmentSlot> MiningToolSlot;

	/** "EQUIPMENT". Styled here, in the panels' type, rather than in the Blueprint. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> PanelNameText;

	/** "TOOLS", over the tool slots. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> ToolsHeadingText;

private:
	/** Each bound slot beside the slot it is, so the screen can fill them without six names. */
	TArray<TPair<EBackendEquipmentSlot, USpaceMMOEquipmentSlot*>> Slots() const;

	/** What the slots were last filled from, so they are refilled only when something changed. */
	FString ViewSignature;

	bool bWarnedAboutWiring = false;
};
