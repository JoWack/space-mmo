#include "SpaceMMOEquipmentScreen.h"

#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "SpaceMMOBackendClient.h"
#include "SpaceMMOBackendLog.h"
#include "SpaceMMOInventoryScreen.h"
#include "SpaceMMOItemStyleTag.h"
#include "SpaceMMOPlayerController.h"

namespace
{
	const EBackendEquipmentSlot EverySlot[] = {
		EBackendEquipmentSlot::Weapon1,
		EBackendEquipmentSlot::Weapon2,
		EBackendEquipmentSlot::Head,
		EBackendEquipmentSlot::Body,
		EBackendEquipmentSlot::Legs,
		EBackendEquipmentSlot::MiningTool,
	};

	/** The controller and the backend a widget's request goes through, or nothing if either is missing. */
	bool Route(const UUserWidget* From, ASpaceMMOPlayerController*& OutController, USpaceMMOBackendClient*& OutClient)
	{
		OutController = From != nullptr ? Cast<ASpaceMMOPlayerController>(From->GetOwningPlayer()) : nullptr;

		const UGameInstance* GameInstance = From != nullptr ? From->GetGameInstance() : nullptr;

		OutClient = GameInstance != nullptr ? GameInstance->GetSubsystem<USpaceMMOBackendClient>() : nullptr;

		return OutController != nullptr && OutClient != nullptr;
	}

	/**
	 * A refusal, shown in the server's own words: no room in the pockets, docked somewhere else, a laser
	 * that does not go in a helmet. Shown rather than swallowed, as a refused move is.
	 */
	TFunction<void(const FBackendFailure&)> ShowRefusal(ASpaceMMOPlayerController* Controller)
	{
		TWeakObjectPtr<ASpaceMMOPlayerController> Weak(Controller);

		return [Weak](const FBackendFailure& Failure)
		{
			if (ASpaceMMOPlayerController* Owner = Weak.Get())
			{
				Owner->ShowTransientMessage(
					Failure.Message.IsEmpty() ? TEXT("That was refused") : Failure.Message,
					ESpaceMMOMessageTone::Warning);
			}
		};
	}
}

// ------------------------------------------------------------------------------------------------
// The rules

TArray<FSpaceMMOEquipmentSlotView> USpaceMMOEquipmentScreen::Build(const TArray<FBackendItemInstance>& Instances)
{
	TArray<FSpaceMMOEquipmentSlotView> Views;

	for (const EBackendEquipmentSlot Which : EverySlot)
	{
		FSpaceMMOEquipmentSlotView View;
		View.Slot = Which;
		View.SlotName = SlotName(Which);

		for (const FBackendItemInstance& Instance : Instances)
		{
			if (Instance.Kind == EBackendInventoryKind::Equipped && Instance.bEquipped && Instance.EquippedSlot == Which)
			{
				View.ItemName = Instance.Name;
				View.InstanceId = Instance.Id;
				View.Style = Instance.Style;

				break;
			}
		}

		Views.Add(View);
	}

	return Views;
}

bool USpaceMMOEquipmentScreen::Fits(const EBackendEquipKind Kind, const EBackendEquipmentSlot Which)
{
	switch (Kind)
	{
	case EBackendEquipKind::Weapon:
		return IsWeaponSlot(Which);
	case EBackendEquipKind::Head:
		return Which == EBackendEquipmentSlot::Head;
	case EBackendEquipKind::Body:
		return Which == EBackendEquipmentSlot::Body;
	case EBackendEquipKind::Legs:
		return Which == EBackendEquipmentSlot::Legs;
	case EBackendEquipKind::MiningTool:
		return Which == EBackendEquipmentSlot::MiningTool;
	default:
		return false;
	}
}

FString USpaceMMOEquipmentScreen::SlotName(const EBackendEquipmentSlot Which)
{
	switch (Which)
	{
	case EBackendEquipmentSlot::Weapon1:
		return TEXT("Weapon 1");
	case EBackendEquipmentSlot::Weapon2:
		return TEXT("Weapon 2");
	case EBackendEquipmentSlot::Head:
		return TEXT("Head");
	case EBackendEquipmentSlot::Body:
		return TEXT("Body");
	case EBackendEquipmentSlot::Legs:
		return TEXT("Legs");
	case EBackendEquipmentSlot::MiningTool:
		return TEXT("Mining");
	default:
		return FString();
	}
}

bool USpaceMMOEquipmentScreen::IsWeaponSlot(const EBackendEquipmentSlot Which)
{
	return Which == EBackendEquipmentSlot::Weapon1 || Which == EBackendEquipmentSlot::Weapon2;
}

FString USpaceMMOEquipmentScreen::NameLabel(const EBackendEquipmentSlot Which)
{
	return IsWeaponSlot(Which) ? SlotName(Which) : SlotName(Which) + TEXT(":");
}

FString USpaceMMOEquipmentScreen::ItemLabel(const FSpaceMMOEquipmentSlotView& View)
{
	if (!View.IsEmpty())
	{
		return View.ItemName;
	}

	return IsWeaponSlot(View.Slot) ? TEXT("— empty —") : TEXT("—");
}

bool USpaceMMOEquipmentScreen::CanEquip(const FSpaceMMOInventoryLine& Source, const EBackendEquipmentSlot Which)
{
	// An instance, reachable, worn at all, and the kind this slot takes. A stack is never worn: armour and
	// weapons carry condition, so each is an instance.
	return Source.CanDrag() && Source.InstanceId != 0 && Source.bEquippable && Fits(Source.EquipsAs, Which);
}

bool USpaceMMOEquipmentScreen::CanMoveBetween(const FSpaceMMOEquipmentSlotView& From, const EBackendEquipmentSlot To)
{
	return !From.IsEmpty() && From.Slot != To && IsWeaponSlot(From.Slot) && IsWeaponSlot(To);
}

void USpaceMMOEquipmentScreen::SendEquip(
	const UUserWidget* From, const int64 InstanceId, const TOptional<EBackendEquipmentSlot> Which)
{
	ASpaceMMOPlayerController* Controller = nullptr;
	USpaceMMOBackendClient* Client = nullptr;

	if (InstanceId == 0 || !Route(From, Controller, Client))
	{
		return;
	}

	UE_LOG(LogSpaceMMOBackend, Log, TEXT("Equipment: equip %lld into %s."),
		InstanceId, Which.IsSet() ? *SlotName(Which.GetValue()) : TEXT("its first free slot"));

	Client->Equip(Controller->GetCharacterId(), InstanceId, Which, ShowRefusal(Controller));
}

void USpaceMMOEquipmentScreen::SendUnequip(
	const UUserWidget* From, const EBackendEquipmentSlot Which, const int64 ToInventoryId)
{
	ASpaceMMOPlayerController* Controller = nullptr;
	USpaceMMOBackendClient* Client = nullptr;

	if (!Route(From, Controller, Client))
	{
		return;
	}

	UE_LOG(LogSpaceMMOBackend, Log, TEXT("Equipment: take off %s, into %s."),
		*SlotName(Which), ToInventoryId != 0 ? *FString::Printf(TEXT("inventory %lld"), ToInventoryId) : TEXT("the pockets"));

	Client->Unequip(Controller->GetCharacterId(), Which, ToInventoryId, ShowRefusal(Controller));
}

// ------------------------------------------------------------------------------------------------
// A slot

void USpaceMMOEquipmentSlot::SetView(const FSpaceMMOEquipmentSlotView& InView)
{
	View = InView;

	if (SlotNameText != nullptr)
	{
		SlotNameText->SetText(FText::FromString(USpaceMMOEquipmentScreen::NameLabel(View.Slot)));
	}

	if (ItemText != nullptr)
	{
		ItemText->SetText(FText::FromString(USpaceMMOEquipmentScreen::ItemLabel(View)));
	}

	SpaceMMO::ItemStyleTag::Apply(StyleTagText, View.IsEmpty() ? EBackendItemStyle::Common : View.Style);

	RequestRestyle();
}

SpaceMMO::Style::ERowLook USpaceMMOEquipmentSlot::Look() const
{
	return bIsDropTarget ? SpaceMMO::Style::ERowLook::DropTarget : Super::Look();
}

void USpaceMMOEquipmentSlot::StyleTexts(const SpaceMMO::Style::ERowLook InLook)
{
	using namespace SpaceMMO;

	// The slot's name as a column heading reads, quiet; what is in it as an item does, or dimmed when it
	// is only a dash, so a full slot stands out from an empty one at a glance.
	PanelLook::Apply(SlotNameText,
		USpaceMMOEquipmentScreen::IsWeaponSlot(View.Slot) ? Style::ETextRole::Column : Style::ETextRole::Body);
	PanelLook::Apply(ItemText, View.IsEmpty() ? Style::ETextRole::Dimmed : Style::ETextRole::Body);
}

FReply USpaceMMOEquipmentSlot::NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
	// An empty slot has nothing to pick up; a full one starts a drag only once the mouse moves, so a
	// double-click stays a double-click.
	if (View.IsEmpty() || Event.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}

	return FReply::Handled().DetectDrag(TakeWidget(), EKeys::LeftMouseButton);
}

FReply USpaceMMOEquipmentSlot::NativeOnMouseButtonDoubleClick(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (View.IsEmpty() || Event.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}

	// Off, into the pockets: double-clicking names nowhere, and the server's default is where you would
	// put something you took off.
	USpaceMMOEquipmentScreen::SendUnequip(this, View.Slot, 0);

	return FReply::Handled();
}

void USpaceMMOEquipmentSlot::NativeOnDragDetected(
	const FGeometry& Geometry, const FPointerEvent& Event, UDragDropOperation*& OutOperation)
{
	Super::NativeOnDragDetected(Geometry, Event, OutOperation);

	if (View.IsEmpty())
	{
		return;
	}

	USpaceMMOEquipmentDrag* Operation = NewObject<USpaceMMOEquipmentDrag>(this);

	Operation->View = View;
	Operation->Pivot = EDragPivot::MouseDown;

	// The thing under the cursor is another of this slot, filled the same: what is being carried, in the
	// look it had where it was picked up.
	if (USpaceMMOEquipmentSlot* Ghost = CreateWidget<USpaceMMOEquipmentSlot>(GetOwningPlayer(), GetClass()))
	{
		Ghost->SetView(View);
		Operation->DefaultDragVisual = Ghost;
	}

	OutOperation = Operation;
}

bool USpaceMMOEquipmentSlot::NativeOnDrop(
	const FGeometry& Geometry, const FDragDropEvent& Event, UDragDropOperation* Operation)
{
	bIsDropTarget = false;

	if (const USpaceMMOInventoryDrag* FromInventory = Cast<USpaceMMOInventoryDrag>(Operation))
	{
		if (!USpaceMMOEquipmentScreen::CanEquip(FromInventory->Line, View.Slot))
		{
			return false;
		}

		USpaceMMOEquipmentScreen::SendEquip(this, FromInventory->Line.InstanceId, View.Slot);

		return true;
	}

	if (const USpaceMMOEquipmentDrag* FromSlot = Cast<USpaceMMOEquipmentDrag>(Operation))
	{
		if (!USpaceMMOEquipmentScreen::CanMoveBetween(FromSlot->View, View.Slot))
		{
			return false;
		}

		// Weapon 1 onto Weapon 2: the server swaps the two.
		USpaceMMOEquipmentScreen::SendEquip(this, FromSlot->View.InstanceId, View.Slot);

		return true;
	}

	return false;
}

void USpaceMMOEquipmentSlot::NativeOnDragEnter(
	const FGeometry& Geometry, const FDragDropEvent& Event, UDragDropOperation* Operation)
{
	Super::NativeOnDragEnter(Geometry, Event, Operation);

	// Only a slot the item fits lights: dragging a laser across the screen shows where it can go.
	if (const USpaceMMOInventoryDrag* FromInventory = Cast<USpaceMMOInventoryDrag>(Operation))
	{
		bIsDropTarget = USpaceMMOEquipmentScreen::CanEquip(FromInventory->Line, View.Slot);
	}
	else if (const USpaceMMOEquipmentDrag* FromSlot = Cast<USpaceMMOEquipmentDrag>(Operation))
	{
		bIsDropTarget = USpaceMMOEquipmentScreen::CanMoveBetween(FromSlot->View, View.Slot);
	}
}

void USpaceMMOEquipmentSlot::NativeOnDragLeave(const FDragDropEvent& Event, UDragDropOperation* Operation)
{
	Super::NativeOnDragLeave(Event, Operation);

	bIsDropTarget = false;
}

// ------------------------------------------------------------------------------------------------
// The screen

TArray<TPair<EBackendEquipmentSlot, USpaceMMOEquipmentSlot*>> USpaceMMOEquipmentScreen::Slots() const
{
	return {
		{EBackendEquipmentSlot::Weapon1, Weapon1Slot.Get()},
		{EBackendEquipmentSlot::Weapon2, Weapon2Slot.Get()},
		{EBackendEquipmentSlot::Head, HeadSlot.Get()},
		{EBackendEquipmentSlot::Body, BodySlot.Get()},
		{EBackendEquipmentSlot::Legs, LegsSlot.Get()},
		{EBackendEquipmentSlot::MiningTool, MiningToolSlot.Get()},
	};
}

void USpaceMMOEquipmentScreen::NativeConstruct()
{
	Super::NativeConstruct();

	SpaceMMO::PanelLook::Apply(PanelNameText, SpaceMMO::Style::ETextRole::Title);
	SpaceMMO::PanelLook::Apply(ToolsHeadingText, SpaceMMO::Style::ETextRole::Group);

	// Each slot is told which it is, by the name it is bound under, so a Blueprint cannot have two boxes
	// that both think they are Weapon 1.
	for (const TPair<EBackendEquipmentSlot, USpaceMMOEquipmentSlot*>& Each : Slots())
	{
		if (Each.Value != nullptr)
		{
			Each.Value->SetOwningScreen(this);

			FSpaceMMOEquipmentSlotView Empty;
			Empty.Slot = Each.Key;
			Empty.SlotName = SlotName(Each.Key);
			Each.Value->SetView(Empty);
		}
	}
}

void USpaceMMOEquipmentScreen::NativeTick(const FGeometry& Geometry, const float DeltaSeconds)
{
	Super::NativeTick(Geometry, DeltaSeconds);

	// Never call SetVisibility on this widget from here; UpdateHudContext owns the L toggle.
	const UGameInstance* GameInstance = GetGameInstance();

	const USpaceMMOBackendClient* Client = GameInstance != nullptr
		? GameInstance->GetSubsystem<USpaceMMOBackendClient>()
		: nullptr;

	if (Client == nullptr)
	{
		return;
	}

	const TArray<TPair<EBackendEquipmentSlot, USpaceMMOEquipmentSlot*>> Bound = Slots();

	if (!bWarnedAboutWiring && Bound.ContainsByPredicate([](const auto& Each) { return Each.Value == nullptr; }))
	{
		// Said once: a slot missing from the Blueprint is a wiring mistake, and an empty box where a laser
		// should be is indistinguishable from not owning one.
		bWarnedAboutWiring = true;

		UE_LOG(LogSpaceMMOBackend, Warning,
			TEXT("HUD: the equipment screen is missing a slot. It binds Weapon1Slot, Weapon2Slot, HeadSlot, "
				"BodySlot, LegsSlot and MiningToolSlot by name; SpaceMMOBuildMenus builds them."));
	}

	const TArray<FSpaceMMOEquipmentSlotView> Views = Build(Client->GetItemInstances());

	FString Signature;

	for (const FSpaceMMOEquipmentSlotView& View : Views)
	{
		Signature += FString::Printf(TEXT("%lld|%s|%d;"), View.InstanceId, *View.ItemName, static_cast<int32>(View.Style));
	}

	if (Signature == ViewSignature)
	{
		return;
	}

	ViewSignature = Signature;

	for (const FSpaceMMOEquipmentSlotView& View : Views)
	{
		for (const TPair<EBackendEquipmentSlot, USpaceMMOEquipmentSlot*>& Each : Bound)
		{
			if (Each.Key == View.Slot && Each.Value != nullptr)
			{
				Each.Value->SetView(View);
			}
		}
	}
}
