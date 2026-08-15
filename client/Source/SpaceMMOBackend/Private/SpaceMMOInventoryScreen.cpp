#include "SpaceMMOInventoryScreen.h"

#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "SpaceMMOBackendClient.h"
#include "SpaceMMOBackendLog.h"
#include "SpaceMMOPlayerController.h"

namespace
{
	/** One container's worth of goods, before it becomes lines. */
	struct FInventoryGroup
	{
		FString Heading;

		/** So a heading is a drop target, and so an empty container is one at all. */
		int64 InventoryId = 0;

		/** Sorts groups: carried, then the hold, then the docked hangar, then the rest. */
		int32 Order = 0;

		bool bReachable = true;

		TArray<FSpaceMMOInventoryLine> Lines;
	};

	FString StationNameFor(const TArray<FBackendStation>& Stations, const int32 StationId)
	{
		for (const FBackendStation& Station : Stations)
		{
			if (Station.Id == StationId)
			{
				return Station.Name;
			}
		}

		// Named by number rather than left blank. A hangar whose station has not been fetched yet is
		// still somewhere, and "HANGAR — 4" tells a player more than "HANGAR — ".
		return FString::Printf(TEXT("%d"), StationId);
	}
}

void USpaceMMOInventoryRow::SetLine(const FSpaceMMOInventoryLine& InLine)
{
	Line = InLine;


	auto Set = [](UTextBlock* Block, const FString& Value)
	{
		if (Block != nullptr)
		{
			Block->SetText(FText::FromString(Value));
		}
	};

	Set(LabelText, Line.Label);
	Set(AmountText, Line.Amount);

	bIsHeading = Line.bIsHeading;
	bReachable = Line.bReachable;
}

FReply USpaceMMOInventoryRow::NativeOnMouseButtonDown(
	const FGeometry& Geometry, const FPointerEvent& Event)
{
	// Only rows naming something reachable start a drag. A heading is a destination, not cargo, and
	// goods at a station the player is not at cannot be picked up at all.
	if (!Line.CanDrag() || !Event.GetEffectingButton().IsMouseButton())
	{
		return FReply::Unhandled();
	}

	// Detect rather than begin: a click that never moves stays a click, so this does not swallow
	// selection later.
	return FReply::Handled().DetectDrag(TakeWidget(), EKeys::LeftMouseButton);
}

void USpaceMMOInventoryRow::NativeOnDragDetected(
	const FGeometry& Geometry, const FPointerEvent& Event, UDragDropOperation*& OutOperation)
{
	Super::NativeOnDragDetected(Geometry, Event, OutOperation);

	if (!Line.CanDrag())
	{
		return;
	}

	USpaceMMOInventoryDrag* Operation = NewObject<USpaceMMOInventoryDrag>(this);

	Operation->Line = Line;
	Operation->Pivot = EDragPivot::MouseDown;

	// The drag visual is another row, filled with the same line. Reusing the row widget means the
	// thing under the cursor is exactly the thing being carried, and it costs the designer nothing.
	if (USpaceMMOInventoryScreen* Screen = OwningScreen.Get())
	{
		if (USpaceMMOInventoryRow* Ghost = Screen->MakeRow(Line))
		{
			Operation->DefaultDragVisual = Ghost;
		}
	}

	OutOperation = Operation;
}

bool USpaceMMOInventoryRow::NativeOnDrop(
	const FGeometry& Geometry, const FDragDropEvent& Event, UDragDropOperation* Operation)
{
	USpaceMMOInventoryScreen* Screen = OwningScreen.Get();

	const USpaceMMOInventoryDrag* Drag = Cast<USpaceMMOInventoryDrag>(Operation);

	if (Screen == nullptr || Drag == nullptr)
	{
		return false;
	}

	Screen->SetDropTargetContainer(0);

	if (!USpaceMMOInventoryScreen::CanDrop(Drag->Line, Line))
	{
		return false;
	}

	// Dropped anywhere in a container, heading included -- every line carries its container, which
	// is what makes the whole group the target rather than a thin strip at the top of it.
	Screen->BeginTransfer(Drag->Line, Line.InventoryId);

	return true;
}

TArray<FSpaceMMOInventoryLine> USpaceMMOInventoryScreen::Build(
	const TArray<FBackendInventoryItem>& Stacks,
	const TArray<FBackendItemInstance>& Instances,
	const TArray<FBackendInventoryContainer>& Containers,
	const TArray<FBackendStation>& Stations,
	const int32 DockedStationId)
{
	// Keyed by inventory id, which is what a container actually is — and what a transfer is
	// addressed by. Keying on kind and station instead would merge two hangars at one station and
	// leave a drop with nowhere unambiguous to land.
	TMap<int64, FInventoryGroup> Groups;

	auto GroupFor = [&Groups, &Stations, DockedStationId](
		const int64 InventoryId,
		const EBackendInventoryKind Kind,
		const int32 StationId) -> FInventoryGroup&
	{
		if (FInventoryGroup* Existing = Groups.Find(InventoryId))
		{
			return *Existing;
		}

		FInventoryGroup Group;
		Group.InventoryId = InventoryId;

		switch (Kind)
		{
		case EBackendInventoryKind::CharacterCarried:
			Group.Heading = TEXT("CARRIED");
			Group.Order = 0;
			break;

		case EBackendInventoryKind::ShipHold:
			Group.Heading = TEXT("SHIP HOLD");
			Group.Order = 1;
			break;

		default:
			Group.Heading = FString::Printf(
				TEXT("HANGAR — %s"), *StationNameFor(Stations, StationId));

			// Carried goods and a ship's hold travel with their owner, so only hangars can be out of
			// reach — and only the one being stood in is in it. This mirrors what the API enforces
			// rather than guessing at it: transfers are refused unless the character is docked here.
			Group.bReachable = StationId == DockedStationId && DockedStationId != 0;
			Group.Order = Group.bReachable ? 2 : 3;
			break;
		}

		return Groups.Add(InventoryId, Group);
	};

	// Every container first, so one holding nothing still appears and can be dropped into. An empty
	// hold is where a first haul goes, and no list of contents can mention it.
	for (const FBackendInventoryContainer& Container : Containers)
	{
		GroupFor(Container.InventoryId, Container.Kind, Container.StationId);
	}

	for (const FBackendInventoryItem& Stack : Stacks)
	{
		FInventoryGroup& Group = GroupFor(Stack.InventoryId, Stack.Kind, Stack.StationId);

		FSpaceMMOInventoryLine Line;
		Line.Label = Stack.Name;
		Line.Amount = ASpaceMMOPlayerController::GroupDigits(Stack.Quantity);
		Line.bReachable = Group.bReachable;
		Line.InventoryId = Stack.InventoryId;
		Line.ItemDefId = Stack.ItemDefId;
		Line.Quantity = Stack.Quantity;

		Group.Lines.Add(Line);
	}

	for (const FBackendItemInstance& Instance : Instances)
	{
		FInventoryGroup& Group = GroupFor(
			Instance.InventoryId, Instance.Kind, Instance.StationId);

		FSpaceMMOInventoryLine Line;
		Line.Label = Instance.Name;
		Line.Amount = FString::Printf(TEXT("cond %d"), Instance.Condition);
		Line.bReachable = Group.bReachable;
		Line.InventoryId = Instance.InventoryId;
		Line.InstanceId = Instance.Id;
		Line.Quantity = 1;

		Group.Lines.Add(Line);
	}

	TArray<FInventoryGroup> Ordered;

	Groups.GenerateValueArray(Ordered);

	// Carried, hold, the hangar you are standing in, then everywhere else by name. What is to hand
	// goes at the top, which is the order the questions get asked in.
	Ordered.Sort([](const FInventoryGroup& A, const FInventoryGroup& B)
	{
		return A.Order != B.Order ? A.Order < B.Order : A.Heading < B.Heading;
	});

	TArray<FSpaceMMOInventoryLine> Result;

	for (FInventoryGroup& Group : Ordered)
	{
		// Sorted here rather than trusted from the response: JSON array order is whatever the query
		// returned, and a list that reorders itself between refreshes is unreadable precisely when
		// it is being watched. Condition breaks ties so two of the same tool can be told apart.
		Group.Lines.Sort([](const FSpaceMMOInventoryLine& A, const FSpaceMMOInventoryLine& B)
		{
			return A.Label != B.Label ? A.Label < B.Label : A.Amount < B.Amount;
		});

		FSpaceMMOInventoryLine Heading;
		Heading.Label = Group.Heading;
		Heading.bIsHeading = true;
		Heading.bReachable = Group.bReachable;
		Heading.InventoryId = Group.InventoryId;

		Result.Add(Heading);
		Result.Append(Group.Lines);
	}

	if (Result.IsEmpty())
	{
		// Says so rather than rendering as a blank rectangle, which reads as a screen that failed to
		// load rather than as a character who owns nothing.
		FSpaceMMOInventoryLine Empty;
		Empty.Label = TEXT("Nothing yet");

		Result.Add(Empty);
	}

	return Result;
}

void USpaceMMOInventoryRow::NativeOnDragEnter(
	const FGeometry& Geometry, const FDragDropEvent& Event, UDragDropOperation* Operation)
{
	Super::NativeOnDragEnter(Geometry, Event, Operation);

	const USpaceMMOInventoryDrag* Drag = Cast<USpaceMMOInventoryDrag>(Operation);

	USpaceMMOInventoryScreen* Screen = OwningScreen.Get();

	if (Drag == nullptr || Screen == nullptr)
	{
		return;
	}

	// The whole container lights, not this row: a group is the target, and one bright line would
	// suggest the goods land on that line rather than in the container it belongs to.
	Screen->SetDropTargetContainer(
		USpaceMMOInventoryScreen::CanDrop(Drag->Line, Line) ? Line.InventoryId : 0);
}

void USpaceMMOInventoryRow::NativeOnDragLeave(
	const FDragDropEvent& Event, UDragDropOperation* Operation)
{
	Super::NativeOnDragLeave(Event, Operation);

	if (USpaceMMOInventoryScreen* Screen = OwningScreen.Get())
	{
		Screen->SetDropTargetContainer(0);
	}
}

bool USpaceMMOInventoryScreen::CanDrop(
	const FSpaceMMOInventoryLine& Source, const FSpaceMMOInventoryLine& Target)
{
	// Nothing to carry, or carried from somewhere out of reach.
	if (!Source.CanDrag())
	{
		return false;
	}

	// A container the player is not at refuses the goods, which is the rule the API enforces rather
	// than a guess at it. Refusing here means the drag says no while it is still a drag, instead of
	// after a round trip that looked like it worked.
	if (!Target.bReachable || Target.InventoryId == 0)
	{
		return false;
	}

	// Already there. Harmless to send and confusing to watch: the server would move goods from a
	// place to itself and the screen would redraw identically, which reads as the drop being lost.
	return Target.InventoryId != Source.InventoryId;
}

USpaceMMOInventoryRow* USpaceMMOInventoryScreen::MakeRow(const FSpaceMMOInventoryLine& Line)
{
	if (RowClass == nullptr)
	{
		return nullptr;
	}

	USpaceMMOInventoryRow* Row = CreateWidget<USpaceMMOInventoryRow>(GetOwningPlayer(), RowClass);

	if (Row != nullptr)
	{
		Row->SetOwningScreen(this);
		Row->SetLine(Line);
	}

	return Row;
}

void USpaceMMOInventoryScreen::SetDropTargetContainer(const int64 InventoryId)
{
	if (HighlightedContainer == InventoryId || InventoryRows == nullptr)
	{
		return;
	}

	HighlightedContainer = InventoryId;

	for (UWidget* Child : InventoryRows->GetAllChildren())
	{
		if (USpaceMMOInventoryRow* Row = Cast<USpaceMMOInventoryRow>(Child))
		{
			Row->bIsDropTarget = InventoryId != 0 && Row->GetLine().InventoryId == InventoryId;
		}
	}
}

void USpaceMMOInventoryScreen::BeginTransfer(
	const FSpaceMMOInventoryLine& Source, const int64 ToInventoryId)
{
	// An instance is one thing carrying its own condition, so there is nothing to ask.
	if (Source.InstanceId != 0)
	{
		PendingSource = Source;
		PendingDestination = ToInventoryId;

		ConfirmQuantity(1);

		return;
	}

	PendingSource = Source;
	PendingDestination = ToInventoryId;

	OnQuantityRequested(Source.Label, Source.Quantity);
}

void USpaceMMOInventoryScreen::CancelQuantity()
{
	PendingSource = FSpaceMMOInventoryLine();
	PendingDestination = 0;
}

void USpaceMMOInventoryScreen::ConfirmQuantity(const int32 Quantity)
{
	const FSpaceMMOInventoryLine Source = PendingSource;
	const int64 Destination = PendingDestination;

	// Cleared first, so a prompt that somehow answers twice cannot move the goods twice.
	CancelQuantity();

	ASpaceMMOPlayerController* Controller = Cast<ASpaceMMOPlayerController>(GetOwningPlayer());

	const UGameInstance* GameInstance = GetGameInstance();

	USpaceMMOBackendClient* Client = GameInstance != nullptr
		? GameInstance->GetSubsystem<USpaceMMOBackendClient>()
		: nullptr;

	if (Controller == nullptr || Client == nullptr || Destination == 0)
	{
		return;
	}

	// Refusals come back with a reason the player can act on -- not docked, or not enough left
	// because somebody else moved it first. Shown rather than swallowed: a drag that looked like it
	// worked and silently did nothing is the worst of the available outcomes.
	TWeakObjectPtr<ASpaceMMOPlayerController> WeakController(Controller);

	auto OnFailure = [WeakController](const FBackendFailure& Failure)
	{
		if (ASpaceMMOPlayerController* Owner = WeakController.Get())
		{
			Owner->ShowTransientMessage(
				Failure.Message.IsEmpty() ? TEXT("That move was refused") : Failure.Message,
				ESpaceMMOMessageTone::Warning);
		}
	};

	if (Source.InstanceId != 0)
	{
		Client->TransferInstance(
			Controller->GetCharacterId(), Source.InstanceId, Destination, OnFailure);

		Controller->ShowTransientMessage(
			FString::Printf(TEXT("Moved %s"), *Source.Label), ESpaceMMOMessageTone::Positive);

		return;
	}

	// Clamped rather than trusted. The prompt is a Blueprint and a typed number is whatever somebody
	// typed; asking for more than is held would be refused by the server anyway, but refusing to ask
	// keeps the message about what happened rather than about arithmetic.
	const int32 Moving = FMath::Clamp(Quantity, 1, FMath::Max(Source.Quantity, 1));

	Client->TransferStack(
		Controller->GetCharacterId(),
		Source.InventoryId,
		Destination,
		Source.ItemDefId,
		Moving,
		OnFailure);

	Controller->ShowTransientMessage(
		FString::Printf(
			TEXT("Moved %s %s"), *ASpaceMMOPlayerController::GroupDigits(Moving), *Source.Label),
		ESpaceMMOMessageTone::Positive);
}

void USpaceMMOInventoryScreen::NativeTick(const FGeometry& Geometry, const float DeltaSeconds)
{
	Super::NativeTick(Geometry, DeltaSeconds);

	// Never call SetVisibility on this widget from here — see the note on
	// USpaceMMOFlightReadout::NativeTick. UpdateHudContext owns the I toggle.
	const ASpaceMMOPlayerController* Controller =
		Cast<ASpaceMMOPlayerController>(GetOwningPlayer());

	const UGameInstance* GameInstance = GetGameInstance();

	const USpaceMMOBackendClient* Client = GameInstance != nullptr
		? GameInstance->GetSubsystem<USpaceMMOBackendClient>()
		: nullptr;

	if (Controller == nullptr || Client == nullptr)
	{
		return;
	}

	if (InventoryRows == nullptr || RowClass == nullptr)
	{
		// Warned once rather than per tick: it is a wiring mistake, not an event, and a screen that
		// opens empty is indistinguishable from a character who owns nothing.
		if (!bWarnedAboutWiring)
		{
			bWarnedAboutWiring = true;

			UE_LOG(LogSpaceMMOBackend, Warning,
				TEXT("HUD: the inventory screen shows nothing — %s%s%s. Set them in the Widget "
					"Blueprint; InventoryRows is bound by name and RowClass in Class Defaults."),
				InventoryRows == nullptr ? TEXT("no panel named 'InventoryRows'") : TEXT(""),
				InventoryRows == nullptr && RowClass == nullptr ? TEXT(" and ") : TEXT(""),
				RowClass == nullptr ? TEXT("no RowClass set") : TEXT(""));
		}

		return;
	}

	const TArray<FSpaceMMOInventoryLine> Lines = Build(
		Client->GetInventory(),
		Client->GetItemInstances(),
		Client->GetContainers(),
		Client->GetStations(),
		Controller->DockedStationId());

	// Rebuilt only when something changed. Inventory arrives on a refresh timer, so most frames have
	// nothing new to say, and tearing down a few dozen widgets on each one would be pure waste.
	FString Signature;

	for (const FSpaceMMOInventoryLine& Line : Lines)
	{
		Signature += Line.Label + Line.Amount + (Line.bReachable ? TEXT("+") : TEXT("-"));
	}

	if (Signature == RowSignature)
	{
		return;
	}

	RowSignature = Signature;

	InventoryRows->ClearChildren();

	for (const FSpaceMMOInventoryLine& Line : Lines)
	{
		if (USpaceMMOInventoryRow* Row = MakeRow(Line))
		{
			InventoryRows->AddChild(Row);
		}
	}

	// Rows are new objects, so any highlight went with the old ones.
	HighlightedContainer = 0;
}
