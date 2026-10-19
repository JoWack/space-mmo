#include "SpaceMMOLootScreen.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/Button.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "SpaceMMOCharacterPawn.h"
#include "SpaceMMOCombatComponent.h"
#include "SpaceMMODepositPrompt.h"
#include "SpaceMMOHudPlacement.h"
#include "SpaceMMOItemStyleTag.h"
#include "SpaceMMOLootComponent.h"
#include "SpaceMMOLootCrateActor.h"
#include "SpaceMMOPlayerController.h"
#include "SpaceMMORenderOrigin.h"

namespace
{
	USpaceMMOLootComponent* LootOf(const UUserWidget* Widget)
	{
		const APlayerController* Controller = Widget != nullptr ? Widget->GetOwningPlayer() : nullptr;
		const APawn* Pawn = Controller != nullptr ? Controller->GetPawn() : nullptr;

		return Pawn != nullptr ? Pawn->FindComponentByClass<USpaceMMOLootComponent>() : nullptr;
	}

	void Set(UTextBlock* Block, const FString& Value)
	{
		if (Block != nullptr)
		{
			Block->SetText(FText::FromString(Value));
		}
	}
}

// ------------------------------------------------------------------------------------------------
// A row

void USpaceMMOLootRow::NativeConstruct()
{
	Super::NativeConstruct();

	RequestRestyle();
}

void USpaceMMOLootRow::SetItem(const FBackendLootItem& InItem, USpaceMMOLootScreen* InScreen)
{
	Item = InItem;
	Screen = InScreen;

	Set(NameText, Item.Name);
	Set(QuantityText, ASpaceMMOPlayerController::GroupDigits(Item.Quantity));

	SpaceMMO::ItemStyleTag::Apply(StyleTagText, Item.ShownStyle);

	RequestRestyle();
}

void USpaceMMOLootRow::StyleTexts(const SpaceMMO::Style::ERowLook InLook)
{
	SpaceMMO::PanelLook::Apply(NameText, SpaceMMO::Style::ETextRole::Body);
	SpaceMMO::PanelLook::ApplyFigure(QuantityText, SpaceMMO::Style::ETextRole::Figure, 120.0f);
}

FReply USpaceMMOLootRow::NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (Item.Quantity <= 0 || Event.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}

	// Detected rather than begun, so a click that never moves stays a click, and a double-click can follow it.
	return FReply::Handled().DetectDrag(TakeWidget(), EKeys::LeftMouseButton);
}

FReply USpaceMMOLootRow::NativeOnMouseButtonDoubleClick(const FGeometry& Geometry, const FPointerEvent& Event)
{
	USpaceMMOLootScreen* Owner = Screen.Get();

	if (Owner == nullptr || Item.Quantity <= 0 || Event.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}

	Owner->Take(Item);

	return FReply::Handled();
}

void USpaceMMOLootRow::NativeOnDragDetected(
	const FGeometry& Geometry, const FPointerEvent& Event, UDragDropOperation*& OutOperation)
{
	Super::NativeOnDragDetected(Geometry, Event, OutOperation);

	USpaceMMOLootScreen* Owner = Screen.Get();

	if (Owner == nullptr || Item.Quantity <= 0)
	{
		return;
	}

	USpaceMMOLootDrag* Operation = NewObject<USpaceMMOLootDrag>(this);
	Operation->Item = Item;
	Operation->Screen = Owner;
	Operation->Pivot = EDragPivot::MouseDown;

	// The thing under the cursor is the row being carried, as the inventory's drags are.
	if (USpaceMMOLootRow* Ghost = Owner->MakeRow(Item))
	{
		Operation->DefaultDragVisual = Ghost;
	}

	OutOperation = Operation;
}

// ------------------------------------------------------------------------------------------------
// The window

FString USpaceMMOLootScreen::TimeLeft(const double Seconds)
{
	const int32 Whole = FMath::Max(0, FMath::CeilToInt(Seconds));

	return FString::Printf(TEXT("%d:%02d"), Whole / 60, Whole % 60);
}

FString USpaceMMOLootScreen::Title(const FString& DroppedBy)
{
	return DroppedBy.IsEmpty() ? FString(TEXT("Dropped")) : FString::Printf(TEXT("Dropped by %s"), *DroppedBy);
}

void USpaceMMOLootScreen::NativeConstruct()
{
	Super::NativeConstruct();

	using namespace SpaceMMO;

	PanelLook::Apply(TitleText, Style::ETextRole::Title);
	PanelLook::Apply(TimeText, Style::ETextRole::Figure);

	if (TakeAllButton != nullptr)
	{
		TakeAllButton->OnClicked.AddUniqueDynamic(this, &USpaceMMOLootScreen::HandleTakeAll);
	}
}

void USpaceMMOLootScreen::ShowCrate(ASpaceMMOLootCrateActor* InCrate)
{
	Crate = InCrate;
	ShownRevision = -1;

	Rebuild();
}

void USpaceMMOLootScreen::HandleTakeAll()
{
	Take(TOptional<FBackendLootItem>());
}

void USpaceMMOLootScreen::Take(const TOptional<FBackendLootItem>& Item)
{
	ASpaceMMOLootCrateActor* Shown = Crate.Get();
	USpaceMMOLootComponent* Loot = LootOf(this);

	if (Shown == nullptr || Loot == nullptr)
	{
		return;
	}

	TArray<FBackendLootItem> Wanted;

	if (Item.IsSet())
	{
		Wanted.Add(Item.GetValue());
	}

	Loot->RequestLoot(Shown, Wanted);
}

USpaceMMOLootRow* USpaceMMOLootScreen::MakeRow(const FBackendLootItem& Item)
{
	if (RowClass == nullptr)
	{
		return nullptr;
	}

	USpaceMMOLootRow* Row = CreateWidget<USpaceMMOLootRow>(GetOwningPlayer(), RowClass);

	if (Row != nullptr)
	{
		Row->SetItem(Item, this);
	}

	return Row;
}

void USpaceMMOLootScreen::Rebuild()
{
	const ASpaceMMOLootCrateActor* Shown = Crate.Get();

	if (Shown == nullptr || LootRows == nullptr)
	{
		return;
	}

	ShownRevision = Shown->GetItemsRevision();

	Set(TitleText, Title(Shown->GetDroppedBy()));

	LootRows->ClearChildren();

	// By name, then world, as the inventory sorts: a list that reorders itself as stacks are taken is hard to read.
	TArray<FBackendLootItem> Items = Shown->GetItems();

	Items.Sort([](const FBackendLootItem& A, const FBackendLootItem& B)
	{
		return A.Name != B.Name ? A.Name < B.Name : A.ShownStyle < B.ShownStyle;
	});

	for (const FBackendLootItem& Item : Items)
	{
		if (USpaceMMOLootRow* Row = MakeRow(Item))
		{
			LootRows->AddChild(Row);
		}
	}
}

void USpaceMMOLootScreen::NativeTick(const FGeometry& Geometry, const float DeltaSeconds)
{
	Super::NativeTick(Geometry, DeltaSeconds);

	ASpaceMMOPlayerController* Controller = Cast<ASpaceMMOPlayerController>(GetOwningPlayer());
	const ASpaceMMOLootCrateActor* Shown = Crate.Get();

	const APawn* Pawn = Controller != nullptr ? Controller->GetPawn() : nullptr;
	const ASpaceMMOCharacterPawn* Body = Cast<ASpaceMMOCharacterPawn>(Pawn);
	const USpaceMMOCombatComponent* Combat = Pawn != nullptr ? Pawn->FindComponentByClass<USpaceMMOCombatComponent>() : nullptr;

	// Gone, emptied, walked away from, or dead: nothing here to take any more.
	const bool bInReach = Shown != nullptr && Body != nullptr && !Shown->GetItems().IsEmpty()
		&& FVector::Distance(Shown->GetSystemPosition().Kilometres, Body->GetSystemPosition().Kilometres) * 1000.0
			<= USpaceMMOLootComponent::RangeMetres + 1.0;

	if (Controller != nullptr && (!bInReach || (Combat != nullptr && Combat->IsDead())))
	{
		Controller->CloseLootScreen();

		return;
	}

	if (Shown == nullptr)
	{
		return;
	}

	if (Shown->GetItemsRevision() != ShownRevision)
	{
		Rebuild();
	}

	Set(TimeText, TimeLeft(Shown->SecondsLeft()));
}

// ------------------------------------------------------------------------------------------------
// The prompt

FString USpaceMMOLootPrompt::Contents(const TArray<FBackendLootItem>& Items)
{
	TArray<FString> Parts;

	for (const FBackendLootItem& Item : Items)
	{
		Parts.Add(FString::Printf(TEXT("%s %s"), *Item.Name, *ASpaceMMOPlayerController::GroupDigits(Item.Quantity)));
	}

	return FString::Join(Parts, TEXT(" · "));
}

void USpaceMMOLootPrompt::NativeConstruct()
{
	Super::NativeConstruct();

	using namespace SpaceMMO;

	PanelLook::Apply(TitleText, Style::ETextRole::Group);
	PanelLook::Apply(TimeText, Style::ETextRole::Figure);
	PanelLook::Apply(ContentsText, Style::ETextRole::Body);
	PanelLook::Apply(KeyText, Style::ETextRole::Key);
	PanelLook::Apply(ActionText, Style::ETextRole::Note);
}

void USpaceMMOLootPrompt::NativeTick(const FGeometry& Geometry, const float DeltaSeconds)
{
	Super::NativeTick(Geometry, DeltaSeconds);

	// Never SetVisibility on this widget itself: UpdateHudContext owns that. Its root is this one's to show.
	APlayerController* Controller = GetOwningPlayer();
	const USpaceMMOLootComponent* Loot = LootOf(this);
	const ASpaceMMOLootCrateActor* Crate = Loot != nullptr ? Loot->CrateForE() : nullptr;

	const UWorld* World = GetWorld();
	const USpaceMMORenderOriginSubsystem* Origin =
		World != nullptr ? World->GetSubsystem<USpaceMMORenderOriginSubsystem>() : nullptr;

	FVector2D Position;

	// Over the crate itself rather than the top of its bounds, which are the beam's, fifteen metres up.
	const bool bShown = Crate != nullptr && Controller != nullptr && Origin != nullptr
		&& UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(
			Controller,
			Crate->GetActorLocation() + Crate->GetActorUpVector() * HeightMetres * 100.0,
			Position,
			false);

	if (PromptRoot != nullptr)
	{
		const ESlateVisibility Wanted = bShown ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed;

		if (PromptRoot->GetVisibility() != Wanted)
		{
			PromptRoot->SetVisibility(Wanted);
		}
	}

	if (!bShown)
	{
		return;
	}

	SpaceMMO::Hud::PlaceAt(PromptRoot, Position);

	Set(TitleText, USpaceMMOLootScreen::Title(Crate->GetDroppedBy()).ToUpper());
	Set(TimeText, USpaceMMOLootScreen::TimeLeft(Crate->SecondsLeft()));
	Set(ContentsText, Contents(Crate->GetItems()));

	const FString Key = USpaceMMODepositPrompt::FindGatherKey();

	Set(KeyText, Key.IsEmpty() ? FString() : FString::Printf(TEXT("[%s]"), *Key));
	Set(ActionText, TEXT("open"));
}
