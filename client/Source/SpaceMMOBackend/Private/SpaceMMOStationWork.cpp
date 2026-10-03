#include "SpaceMMOStationWork.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "SpaceMMOBackendProtocol.h"
#include "SpaceMMOStationOverlay.h"

FString FSpaceMMOStationWork::Duration(const int32 Seconds)
{
	const int32 Clamped = FMath::Max(Seconds, 0);

	if (Clamped < 60)
	{
		return FString::Printf(TEXT("%d s"), Clamped);
	}

	if (Clamped < 3600)
	{
		return FString::Printf(TEXT("%d m %d s"), Clamped / 60, Clamped % 60);
	}

	return FString::Printf(TEXT("%d h %d m"), Clamped / 3600, (Clamped % 3600) / 60);
}

int32 FSpaceMMOStationWork::HeldHere(
	const TArray<FBackendInventoryItem>& Inventory, const FString& ItemKey, const int32 StationId)
{
	int32 Held = 0;

	for (const FBackendInventoryItem& Item : Inventory)
	{
		if (Item.ItemKey == ItemKey && Item.Kind == EBackendInventoryKind::StationHangar && Item.StationId == StationId)
		{
			Held += Item.Quantity;
		}
	}

	return Held;
}

int32 FSpaceMMOStationWork::MostRuns(
	const FBackendRecipe& Recipe, const TArray<FBackendInventoryItem>& Inventory, const int32 StationId)
{
	int32 Most = MaxRuns;

	for (const FBackendRecipeInput& Input : Recipe.Inputs)
	{
		if (Input.Quantity > 0)
		{
			Most = FMath::Min(Most, HeldHere(Inventory, Input.ItemKey, StationId) / Input.Quantity);
		}
	}

	return Most;
}

int32 FSpaceMMOStationWork::ClampSelection(const int32 Selected, const int32 RecipeCount)
{
	// Clamped rather than trusted. The catalogue can be re-fetched at any time, and a selection left
	// past its end would read as "nothing is selected" while Start silently did nothing.
	return RecipeCount > 0 ? FMath::Clamp(Selected, 0, RecipeCount - 1) : INDEX_NONE;
}

TArray<FSpaceMMORecipeRowText> FSpaceMMOStationWork::BuildRecipeRows(
	const TArray<FBackendRecipe>& Recipes,
	const TArray<FBackendInventoryItem>& Inventory,
	const int32 StationId,
	const int32 Selected,
	const int32 Runs)
{
	TArray<FSpaceMMORecipeRowText> Rows;

	const int32 Chosen = ClampSelection(Selected, Recipes.Num());
	const int32 Count = FMath::Max(Runs, 1);

	for (int32 Index = 0; Index < Recipes.Num(); ++Index)
	{
		const FBackendRecipe& Recipe = Recipes[Index];

		FSpaceMMORecipeRowText Row;

		Row.Index = Index;
		Row.Title = FString::Printf(TEXT("%s ×%d"), *Recipe.OutputName, Recipe.OutputQuantity);
		Row.Time = Duration(Recipe.JobSeconds);
		Row.Skill = FString::Printf(TEXT("%s lv %d"), *Recipe.SkillName.ToLower(), Recipe.RequiredLevel);
		Row.bSelected = Index == Chosen;

		// Materials only for the selected recipe. Every input of every recipe would be a wall of text on
		// a panel that has to be read at a glance.
		if (Row.bSelected)
		{
			for (const FBackendRecipeInput& Input : Recipe.Inputs)
			{
				FSpaceMMORecipeInputText Line;

				Line.Name = Input.Name;
				Line.Held = HeldHere(Inventory, Input.ItemKey, StationId);
				Line.Needed = Input.Quantity * Count;

				Row.Inputs.Add(Line);
			}

			Row.Tool = Recipe.RequiredToolName.IsEmpty()
				? FString(TEXT("tool: none"))
				: FString::Printf(TEXT("tool: %s"), *Recipe.RequiredToolName);
		}

		Rows.Add(Row);
	}

	return Rows;
}

FSpaceMMOStartBarText FSpaceMMOStationWork::BuildStartBar(
	const TArray<FBackendRecipe>& Recipes,
	const TArray<FBackendInventoryItem>& Inventory,
	const int32 StationId,
	const int32 Selected,
	const int32 Runs)
{
	FSpaceMMOStartBarText Bar;

	const int32 Chosen = ClampSelection(Selected, Recipes.Num());

	if (Chosen == INDEX_NONE)
	{
		return Bar;
	}

	const FBackendRecipe& Recipe = Recipes[Chosen];

	Bar.bHasRecipe = true;
	Bar.MostRuns = MostRuns(Recipe, Inventory, StationId);

	// Capped by what is held, but never below one: with nothing to make, the count stays at one and
	// Start still asks the server, whose refusal says what is missing better than a dead button would.
	Bar.Runs = FMath::Clamp(Runs, 1, FMath::Max(Bar.MostRuns, 1));

	Bar.Label = FString::Printf(TEXT("Start %s ×%d"), *Recipe.OutputName, Recipe.OutputQuantity * Bar.Runs);

	if (Bar.MostRuns > 0)
	{
		Bar.Note = FString::Printf(
			TEXT("most you can make: %d  ·  %s"), Bar.MostRuns, *Duration(Recipe.JobSeconds * Bar.Runs));

		return Bar;
	}

	Bar.bShort = true;

	TArray<FString> Short;

	for (const FBackendRecipeInput& Input : Recipe.Inputs)
	{
		if (HeldHere(Inventory, Input.ItemKey, StationId) < Input.Quantity)
		{
			Short.Add(Input.Name);
		}
	}

	// Named when there are one or two. More than that ran out of the panel (Joe, 3 October), and the row
	// above already names each one in red.
	Bar.Note = Short.Num() <= 2
		? FString::Printf(TEXT("short of %s in this hangar"), *FString::Join(Short, TEXT(" and ")))
		: FString::Printf(TEXT("short of %d inputs in this hangar"), Short.Num());

	return Bar;
}

TArray<FSpaceMMOJobRowText> FSpaceMMOStationWork::BuildJobRows(
	const TArray<FBackendIndustryJob>& Jobs, const TArray<FBackendRecipe>& Recipes)
{
	TArray<FSpaceMMOJobRowText> Rows;

	for (const FBackendIndustryJob& Job : Jobs)
	{
		FSpaceMMOJobRowText Row;

		Row.JobId = Job.Id;
		Row.Title = FString::Printf(TEXT("%s ×%d"), *Job.OutputName, Job.OutputQuantityTotal);
		Row.bReady = Job.bIsClaimable;

		if (!Row.bReady)
		{
			Row.Remaining = Duration(Job.SecondsRemaining);

			// The whole job's time is the recipe's times its runs, which is how the server sets it. A job
			// whose recipe is no longer in the catalogue shows its time left without a bar position.
			const FBackendRecipe* Recipe = Recipes.FindByPredicate(
				[&Job](const FBackendRecipe& Candidate) { return Candidate.Key == Job.RecipeKey; });

			const int32 Total = Recipe != nullptr ? Recipe->JobSeconds * FMath::Max(Job.Runs, 1) : 0;

			Row.Progress = Total > 0
				? FMath::Clamp(1.0f - static_cast<float>(Job.SecondsRemaining) / static_cast<float>(Total), 0.0f, 1.0f)
				: 0.0f;
		}

		Rows.Add(Row);
	}

	return Rows;
}

TArray<FSpaceMMOQuestRowText> FSpaceMMOStationWork::BuildQuestRows(
	const TArray<FBackendJournalEntry>& Journal, const TArray<FBackendAvailableQuest>& Available)
{
	auto Reward = [](const int64 MinorUnits)
	{
		return MinorUnits > 0
			? FString::Printf(TEXT("%s cr"), *FSpaceMMOBackendProtocol::FormatCredits(MinorUnits))
			: FString();
	};

	TArray<FSpaceMMOQuestRowText> Ready;
	TArray<FSpaceMMOQuestRowText> Active;

	for (const FBackendJournalEntry& Entry : Journal)
	{
		// History is dropped. A journal listing everything ever done buries the one row saying what to
		// do next, which is the only row anybody is looking for.
		if (Entry.State == EBackendQuestState::Completed || Entry.State == EBackendQuestState::Abandoned)
		{
			continue;
		}

		FSpaceMMOQuestRowText Row;

		Row.QuestKey = Entry.QuestKey;
		Row.Name = Entry.Name;
		Row.Description = Entry.StepDescription;
		Row.Reward = Reward(Entry.RewardMinorUnits);

		if (Entry.State == EBackendQuestState::ReadyToTurnIn)
		{
			// Said as ready rather than as 10/10: finished work wants handing in, not counting.
			Row.Kind = ESpaceMMOQuestRowKind::Ready;
			Row.Fraction = 1.0f;

			Ready.Add(Row);

			continue;
		}

		Row.Kind = ESpaceMMOQuestRowKind::Active;
		Row.Progress = FString::Printf(TEXT("%d/%d"), Entry.StepProgress, Entry.StepRequired);
		Row.Fraction = Entry.StepRequired > 0
			? FMath::Clamp(static_cast<float>(Entry.StepProgress) / static_cast<float>(Entry.StepRequired), 0.0f, 1.0f)
			: 0.0f;

		Active.Add(Row);
	}

	// Finished work first: a player standing on a quest that is done wants paying before anything else.
	TArray<FSpaceMMOQuestRowText> Rows = Ready;
	Rows.Append(Active);

	for (const FBackendAvailableQuest& Quest : Available)
	{
		FSpaceMMOQuestRowText Row;

		Row.QuestKey = Quest.QuestKey;
		Row.Name = Quest.Name;
		Row.Kind = ESpaceMMOQuestRowKind::Offered;
		Row.Description = Quest.Description;
		Row.Reward = Reward(Quest.RewardMinorUnits);

		Rows.Add(Row);
	}

	return Rows;
}

// Named, not anonymous: in a unity build an anonymous namespace is shared with whatever lands in the same
// blob, and "SetText" and "Show" are names somebody else will have.
namespace SpaceMMOStationWorkRows
{
	void SetText(UTextBlock* Block, const FString& Value)
	{
		if (Block != nullptr)
		{
			Block->SetText(FText::FromString(Value));
		}
	}

	void Show(UWidget* Widget, const bool bShown, const ESlateVisibility Visible = ESlateVisibility::Visible)
	{
		if (Widget != nullptr)
		{
			Widget->SetVisibility(bShown ? Visible : ESlateVisibility::Collapsed);
		}
	}
}

// ---------------------------------------------------------------------------------------------------
// Recipe row

void USpaceMMORecipeRow::SetOwningOverlay(USpaceMMOStationOverlay* const Overlay)
{
	OwningOverlay = Overlay;
}

void USpaceMMORecipeRow::SetRow(const FSpaceMMORecipeRowText& InRow)
{
	Row = InRow;

	SpaceMMOStationWorkRows::SetText(TitleText, Row.Title);
	SpaceMMOStationWorkRows::SetText(TimeText, Row.Time);
	SpaceMMOStationWorkRows::SetText(SkillText, Row.Skill);

	// One text per input, built here rather than in the Blueprint, so each can be coloured by whether
	// the hangar has enough of it.
	if (InputsBox != nullptr)
	{
		InputsBox->ClearChildren();

		TArray<FString> Parts;

		for (const FSpaceMMORecipeInputText& Input : Row.Inputs)
		{
			Parts.Add(FString::Printf(TEXT("%s %d/%d"), *Input.Name, Input.Held, Input.Needed));
		}

		if (!Row.Tool.IsEmpty())
		{
			Parts.Add(Row.Tool);
		}

		for (int32 Index = 0; Index < Parts.Num(); ++Index)
		{
			UTextBlock* Part = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
			Part->SetText(FText::FromString(Parts[Index]));

			const bool bShort = Row.Inputs.IsValidIndex(Index) && Row.Inputs[Index].IsShort();

			SpaceMMO::PanelLook::Apply(Part, SpaceMMO::Style::ETextRole::Note,
				bShort ? TOptional<FLinearColor>(SpaceMMO::Style::ErrorRed()) : TOptional<FLinearColor>());

			// The gaps are the wrap box's own; it wraps onto a further line when the row is full.
			InputsBox->AddChild(Part);
		}

		SpaceMMOStationWorkRows::Show(InputsBox, Parts.Num() > 0, ESlateVisibility::HitTestInvisible);
	}

	RequestRestyle();
}

SpaceMMO::Style::ERowLook USpaceMMORecipeRow::Look() const
{
	return Row.bSelected ? SpaceMMO::Style::ERowLook::Selected : Super::Look();
}

void USpaceMMORecipeRow::StyleTexts(const SpaceMMO::Style::ERowLook InLook)
{
	using namespace SpaceMMO;

	PanelLook::Apply(TitleText, Style::ETextRole::Body);
	PanelLook::ApplyFigure(TimeText, Style::ETextRole::Figure, 110.0f);
	PanelLook::ApplyFigure(SkillText, Style::ETextRole::Figure, 190.0f);
}

FReply USpaceMMORecipeRow::NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (USpaceMMOStationOverlay* Overlay = OwningOverlay.Get())
	{
		Overlay->SelectRecipe(Row.Index);

		return FReply::Handled();
	}

	return FReply::Unhandled();
}

// ---------------------------------------------------------------------------------------------------
// Job row

void USpaceMMOJobRow::NativeConstruct()
{
	Super::NativeConstruct();

	// AddUnique, not a bool guard: a row can be constructed again after being taken off screen, and a
	// guard cannot tell "already bound" from "bound to a button that is gone" (CLAUDE.md, the G key).
	if (ClaimButton != nullptr)
	{
		ClaimButton->OnClicked.AddUniqueDynamic(this, &USpaceMMOJobRow::Claim);
	}
}

void USpaceMMOJobRow::SetOwningOverlay(USpaceMMOStationOverlay* const Overlay)
{
	OwningOverlay = Overlay;
}

void USpaceMMOJobRow::SetRow(const FSpaceMMOJobRowText& InRow)
{
	Row = InRow;

	SpaceMMOStationWorkRows::SetText(TitleText, Row.Title);
	SpaceMMOStationWorkRows::SetText(StatusText, Row.bReady ? FString(TEXT("Ready")) : Row.Remaining);
	SpaceMMOStationWorkRows::SetText(ClaimText, TEXT("Claim"));

	SpaceMMOStationWorkRows::Show(ClaimButton, Row.bReady);

	if (ProgressBar != nullptr)
	{
		ProgressBar->SetPercent(Row.Progress);
		SpaceMMOStationWorkRows::Show(ProgressBar, !Row.bReady, ESlateVisibility::HitTestInvisible);
	}

	RequestRestyle();
}

void USpaceMMOJobRow::StyleTexts(const SpaceMMO::Style::ERowLook InLook)
{
	using namespace SpaceMMO;

	PanelLook::Apply(TitleText, Style::ETextRole::Body);

	if (Row.bReady)
	{
		PanelLook::ApplyFigure(StatusText, Style::ETextRole::Column, 90.0f);
		PanelLook::Apply(StatusText, Style::ETextRole::Column, Style::Ice());
	}
	else
	{
		PanelLook::ApplyFigure(StatusText, Style::ETextRole::Figure, 90.0f);
	}

	PanelLook::Apply(ClaimText, Style::ETextRole::ButtonSmall);
	PanelLook::ApplyBar(ProgressBar);

	if (ClaimButton != nullptr)
	{
		ClaimButton->SetStyle(Style::ButtonStyle(false, true));
	}
}

void USpaceMMOJobRow::Claim()
{
	if (USpaceMMOStationOverlay* Overlay = OwningOverlay.Get())
	{
		Overlay->ClaimJob(Row.JobId);
	}
}

// ---------------------------------------------------------------------------------------------------
// Quest row

void USpaceMMOQuestRow::NativeConstruct()
{
	Super::NativeConstruct();

	if (ActionButton != nullptr)
	{
		ActionButton->OnClicked.AddUniqueDynamic(this, &USpaceMMOQuestRow::Act);
	}
}

void USpaceMMOQuestRow::SetOwningOverlay(USpaceMMOStationOverlay* const Overlay)
{
	OwningOverlay = Overlay;
}

void USpaceMMOQuestRow::SetRow(const FSpaceMMOQuestRowText& InRow)
{
	Row = InRow;

	SpaceMMOStationWorkRows::SetText(NameText, Row.Name);
	SpaceMMOStationWorkRows::SetText(DescriptionText, Row.Description);
	SpaceMMOStationWorkRows::SetText(RewardText, Row.Reward);

	switch (Row.Kind)
	{
	case ESpaceMMOQuestRowKind::Ready:
		SpaceMMOStationWorkRows::SetText(StatusText, TEXT("Ready to hand in"));
		SpaceMMOStationWorkRows::SetText(ActionText, TEXT("Hand in"));
		break;

	case ESpaceMMOQuestRowKind::Offered:
		SpaceMMOStationWorkRows::SetText(StatusText, FString());
		SpaceMMOStationWorkRows::SetText(ActionText, TEXT("Accept"));
		break;

	default:
		SpaceMMOStationWorkRows::SetText(StatusText, Row.Progress);
		SpaceMMOStationWorkRows::SetText(ActionText, FString());
		break;
	}

	SpaceMMOStationWorkRows::Show(ActionButton, Row.Kind != ESpaceMMOQuestRowKind::Active);
	SpaceMMOStationWorkRows::Show(StatusText, Row.Kind != ESpaceMMOQuestRowKind::Offered, ESlateVisibility::HitTestInvisible);
	SpaceMMOStationWorkRows::Show(DescriptionText, !Row.Description.IsEmpty(), ESlateVisibility::HitTestInvisible);
	SpaceMMOStationWorkRows::Show(RewardText, !Row.Reward.IsEmpty(), ESlateVisibility::HitTestInvisible);

	if (ProgressBar != nullptr)
	{
		ProgressBar->SetPercent(Row.Fraction);
		SpaceMMOStationWorkRows::Show(ProgressBar, Row.Kind == ESpaceMMOQuestRowKind::Active, ESlateVisibility::HitTestInvisible);
	}

	RequestRestyle();
}

void USpaceMMOQuestRow::StyleTexts(const SpaceMMO::Style::ERowLook InLook)
{
	using namespace SpaceMMO;

	PanelLook::Apply(NameText, Style::ETextRole::Body);

	if (Row.Kind == ESpaceMMOQuestRowKind::Ready)
	{
		PanelLook::Apply(StatusText, Style::ETextRole::Column, Style::Ice());
	}
	else
	{
		PanelLook::ApplyFigure(StatusText, Style::ETextRole::Figure, 90.0f);
	}

	PanelLook::Apply(DescriptionText, Style::ETextRole::Note);
	PanelLook::ApplyFigure(RewardText, Style::ETextRole::Note, 110.0f);
	PanelLook::Apply(ActionText, Style::ETextRole::ButtonSmall);
	PanelLook::ApplyBar(ProgressBar);

	// Handing in is the primary thing on the tab; accepting more is the ordinary one.
	if (ActionButton != nullptr)
	{
		ActionButton->SetStyle(Style::ButtonStyle(Row.Kind == ESpaceMMOQuestRowKind::Ready, true));
	}
}

void USpaceMMOQuestRow::Act()
{
	USpaceMMOStationOverlay* Overlay = OwningOverlay.Get();

	if (Overlay == nullptr)
	{
		return;
	}

	if (Row.Kind == ESpaceMMOQuestRowKind::Ready)
	{
		Overlay->HandInQuest(Row.QuestKey, Row.Name);
	}
	else if (Row.Kind == ESpaceMMOQuestRowKind::Offered)
	{
		Overlay->AcceptQuest(Row.QuestKey);
	}
}
