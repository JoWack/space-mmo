#include "SpaceMMOSkillsScreen.h"

#include "Components/PanelWidget.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "SpaceMMOBackendClient.h"
#include "SpaceMMOPlayerController.h"

void USpaceMMOSkillRow::SetRow(const FSpaceMMOSkillRowText& Row)
{
	auto Set = [](UTextBlock* Block, const FString& Value)
	{
		if (Block != nullptr)
		{
			Block->SetText(FText::FromString(Value));
		}
	};

	Set(NameText, Row.Name);
	Set(LevelText, Row.Level);
	Set(XpText, Row.Xp);
	Set(ToNextText, Row.ToNext);

	bTrained = Row.bTrained;
	bHasProgress = Row.Progress >= 0.0f;
	bHasToNext = !Row.ToNext.IsEmpty();

	if (ProgressBar != nullptr)
	{
		// Clamped rather than trusted: this arrives over the wire, and a bar asked for 1.4 draws
		// past its own end.
		ProgressBar->SetPercent(bHasProgress ? FMath::Clamp(Row.Progress, 0.0f, 1.0f) : 0.0f);
	}
}

TArray<FSpaceMMOSkillRowText> USpaceMMOSkillsScreen::Build(const TArray<FBackendSkill>& Skills)
{
	TArray<FBackendSkill> Ordered = Skills;

	// Trained first, alphabetical within each group. Sorted here rather than trusted from the
	// response: JSON array order is whatever the query returned, and a list that reorders itself
	// between refreshes is unreadable precisely when it is being watched.
	Ordered.Sort([](const FBackendSkill& A, const FBackendSkill& B)
	{
		if ((A.Xp > 0) != (B.Xp > 0))
		{
			return A.Xp > 0;
		}

		return A.Name < B.Name;
	});

	TArray<FSpaceMMOSkillRowText> Rows;

	Rows.Reserve(Ordered.Num());

	for (const FBackendSkill& Skill : Ordered)
	{
		FSpaceMMOSkillRowText Row;

		Row.Name = Skill.Name;

		// "lv 1" rather than a dash for an untouched skill. Every character has every skill at level
		// one from creation — the skills endpoint says so in as many words — so a dash would claim
		// something the server does not.
		Row.Level = FString::Printf(TEXT("lv %d"), Skill.Level);
		Row.Xp = FString::Printf(TEXT("%s xp"), *ASpaceMMOPlayerController::GroupDigits(Skill.Xp));
		Row.bTrained = Skill.Xp > 0;
		Row.Progress = Skill.HasProgress() ? Skill.ProgressToNextLevel : -1.0f;

		// Nothing to say at the cap, and nothing to say if the server never sent the figures. Both
		// leave the line empty rather than printing "0 to lv 100".
		Row.ToNext = Skill.HasProgress() && Skill.XpToNextLevel > 0
			? FString::Printf(
				TEXT("%s to lv %d"),
				*ASpaceMMOPlayerController::GroupDigits(Skill.XpToNextLevel),
				Skill.Level + 1)
			: FString();

		Rows.Add(Row);
	}

	return Rows;
}

void USpaceMMOSkillsScreen::NativeTick(const FGeometry& Geometry, const float DeltaSeconds)
{
	Super::NativeTick(Geometry, DeltaSeconds);

	// Never call SetVisibility on this widget from here — see the note on
	// USpaceMMOFlightReadout::NativeTick. UpdateHudContext owns the K toggle.
	const UGameInstance* GameInstance = GetGameInstance();

	const USpaceMMOBackendClient* Client = GameInstance != nullptr
		? GameInstance->GetSubsystem<USpaceMMOBackendClient>()
		: nullptr;

	if (Client == nullptr || SkillRows == nullptr || RowClass == nullptr)
	{
		return;
	}

	const TArray<FSpaceMMOSkillRowText> Rows = Build(Client->GetSkills());

	// Rebuild only when the wording changed. XP arrives on a refresh timer, so most frames have
	// nothing new to say, and tearing down thirty widgets each one would be pure waste.
	FString Signature;

	for (const FSpaceMMOSkillRowText& Row : Rows)
	{
		Signature += Row.Name + Row.Level + Row.Xp + Row.ToNext + TEXT("|");
	}

	if (Signature == RowSignature)
	{
		return;
	}

	RowSignature = Signature;

	SkillRows->ClearChildren();

	for (const FSpaceMMOSkillRowText& Row : Rows)
	{
		USpaceMMOSkillRow* Widget = CreateWidget<USpaceMMOSkillRow>(GetOwningPlayer(), RowClass);

		if (Widget == nullptr)
		{
			continue;
		}

		Widget->SetRow(Row);

		SkillRows->AddChild(Widget);
	}
}
