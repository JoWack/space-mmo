#include "SpaceMMOStationOverlay.h"

#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "SpaceMMOBackendLog.h"
#include "SpaceMMOPlayerController.h"

void USpaceMMOTextRow::SetLine(const FString& Line)
{
	if (LineText != nullptr)
	{
		LineText->SetText(FText::FromString(Line));
	}
}

void USpaceMMOStationOverlay::SetTab(const ESpaceMMOStationTab Tab)
{
	ActiveTab = Tab;

	bMarketTab = Tab == ESpaceMMOStationTab::Market;
	bIndustryTab = Tab == ESpaceMMOStationTab::Industry;
	bQuestsTab = Tab == ESpaceMMOStationTab::Quests;
}

void USpaceMMOStationOverlay::FillPanel(
	UPanelWidget* Container,
	const TArray<FString>& Lines,
	FString& Signature)
{
	if (Container == nullptr)
	{
		return;
	}

	// Rebuilt only when the wording changed. Industry counts down every second and the market moves
	// whenever anybody trades, so most frames still have nothing new to say.
	const FString Next = FString::Join(Lines, TEXT("\n"));

	if (Next == Signature)
	{
		return;
	}

	Signature = Next;

	Container->ClearChildren();

	for (const FString& Line : Lines)
	{
		USpaceMMOTextRow* Row = CreateWidget<USpaceMMOTextRow>(GetOwningPlayer(), RowClass);

		if (Row == nullptr)
		{
			continue;
		}

		Row->SetLine(Line);

		Container->AddChild(Row);
	}
}

void USpaceMMOStationOverlay::NativeTick(const FGeometry& Geometry, const float DeltaSeconds)
{
	Super::NativeTick(Geometry, DeltaSeconds);

	// Never call SetVisibility on this widget from here — see the note on
	// USpaceMMOFlightReadout::NativeTick. UpdateHudContext owns the Tab toggle.
	const ASpaceMMOPlayerController* Controller =
		Cast<ASpaceMMOPlayerController>(GetOwningPlayer());

	if (Controller == nullptr)
	{
		return;
	}

	if (RowClass == nullptr
		|| (MarketRows == nullptr && IndustryRows == nullptr && QuestRows == nullptr))
	{
		// Warned once rather than per tick: it is a wiring mistake, not an event. Without this a
		// station overlay that opens completely empty is indistinguishable from a station with
		// nothing for sale, no recipes and no quests.
		if (!bWarnedAboutWiring)
		{
			bWarnedAboutWiring = true;

			UE_LOG(LogSpaceMMOBackend, Warning,
				TEXT("HUD: the station overlay shows nothing — %s. Set them in the Widget "
					"Blueprint; MarketRows, IndustryRows and QuestRows are bound by name and "
					"RowClass in Class Defaults."),
				RowClass == nullptr
					? TEXT("no RowClass set")
					: TEXT("none of MarketRows, IndustryRows or QuestRows is bound"));
		}

		return;
	}

	FString StationName;

	TArray<FString> Market;
	TArray<FString> Industry;
	TArray<FString> Quests;

	Controller->GetStationPanels(StationName, Market, Industry, Quests);

	if (StationNameText != nullptr)
	{
		StationNameText->SetText(FText::FromString(StationName));
	}

	// All three are filled rather than only the visible one. They are cheap while unchanged, and a
	// tab that rebuilt on being switched to would flicker on every switch — which is the moment a
	// player is looking straight at it.
	FillPanel(MarketRows, Market, MarketSignature);
	FillPanel(IndustryRows, Industry, IndustrySignature);
	FillPanel(QuestRows, Quests, QuestSignature);
}
