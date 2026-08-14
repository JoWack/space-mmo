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

void USpaceMMOInventoryRow::SetLine(const FSpaceMMOInventoryLine& Line)
{
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

TArray<FSpaceMMOInventoryLine> USpaceMMOInventoryScreen::Build(
	const TArray<FBackendInventoryItem>& Stacks,
	const TArray<FBackendItemInstance>& Instances,
	const TArray<FBackendStation>& Stations,
	const int32 DockedStationId)
{
	// Keyed by kind and station, because a hangar at one station is a different container from a
	// hangar at another and merging them would be the exact lie this screen exists to prevent.
	TMap<TPair<EBackendInventoryKind, int32>, FInventoryGroup> Groups;

	auto GroupFor = [&Groups, &Stations, DockedStationId](
		const EBackendInventoryKind Kind, const int32 StationId) -> FInventoryGroup&
	{
		const TPair<EBackendInventoryKind, int32> Key(Kind, StationId);

		if (FInventoryGroup* Existing = Groups.Find(Key))
		{
			return *Existing;
		}

		FInventoryGroup Group;

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

		return Groups.Add(Key, Group);
	};

	for (const FBackendInventoryItem& Stack : Stacks)
	{
		FInventoryGroup& Group = GroupFor(Stack.Kind, Stack.StationId);

		FSpaceMMOInventoryLine Line;
		Line.Label = Stack.Name;
		Line.Amount = ASpaceMMOPlayerController::GroupDigits(Stack.Quantity);
		Line.bReachable = Group.bReachable;

		Group.Lines.Add(Line);
	}

	for (const FBackendItemInstance& Instance : Instances)
	{
		FInventoryGroup& Group = GroupFor(Instance.Kind, Instance.StationId);

		FSpaceMMOInventoryLine Line;
		Line.Label = Instance.Name;
		Line.Amount = FString::Printf(TEXT("cond %d"), Instance.Condition);
		Line.bReachable = Group.bReachable;

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
		USpaceMMOInventoryRow* Row =
			CreateWidget<USpaceMMOInventoryRow>(GetOwningPlayer(), RowClass);

		if (Row == nullptr)
		{
			continue;
		}

		Row->SetLine(Line);

		InventoryRows->AddChild(Row);
	}
}
