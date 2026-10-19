#include "SpaceMMOCombatHud.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/World.h"
#include "SpaceMMOCombatComponent.h"
#include "SpaceMMOHudPlacement.h"
#include "SpaceMMOPanelRow.h"
#include "SpaceMMOPlayerController.h"
#include "SpaceMMORenderOrigin.h"
#include "SpaceMMOStyle.h"

namespace
{
	/** Health's red: Joe's, 16 October, for every health bar. Brighter as health falls. */
	const FLinearColor HealthRed = SpaceMMO::Style::Srgb(214, 52, 52);
	const FLinearColor HealthRedBright = SpaceMMO::Style::Srgb(255, 92, 72);

	/** Stamina's amber (Joe, 19 October): never read as health. Brighter for the flash of a refused dodge. */
	const FLinearColor StaminaAmber = SpaceMMO::Style::Srgb(232, 168, 58);
	const FLinearColor StaminaFlash = SpaceMMO::Style::Srgb(255, 232, 170);

	/** How long a refused dodge's flash takes to fade. */
	constexpr double StaminaFlashSeconds = 0.35;

	/** A head shot's number, so the aim that earned it shows. */
	const FLinearColor HeadGold = SpaceMMO::Style::Srgb(242, 193, 78);

	FProgressBarStyle BarStyle(const FLinearColor& Fill)
	{
		FProgressBarStyle Style;
		Style.SetBackgroundImage(
			SpaceMMO::Style::Rounded(SpaceMMO::Style::White(0.12f), SpaceMMO::Style::White(0.0f), 0.0f, FVector4(4, 4, 4, 4)));
		Style.SetFillImage(SpaceMMO::Style::Rounded(Fill, SpaceMMO::Style::White(0.0f), 0.0f, FVector4(4, 4, 4, 4)));

		return Style;
	}
}

float USpaceMMOCombatHud::GlowStrength(const float HealthFraction)
{
	// Faint while healthy, so the bar always reads as alive; from half down it climbs to full at nothing.
	const float Low = FMath::Clamp((0.5f - HealthFraction) / 0.5f, 0.0f, 1.0f);

	return 0.15f + 0.85f * Low;
}

float USpaceMMOCombatHud::PulseRate(const float HealthFraction)
{
	const float Low = FMath::Clamp((0.5f - HealthFraction) / 0.5f, 0.0f, 1.0f);

	// Still above half; under it, from a slow breath to a quick heartbeat.
	return Low <= 0.0f ? 0.0f : 0.8f + 1.7f * Low;
}

FString USpaceMMOCombatHud::HealthLine(const int32 Health, const int32 MaxHealth)
{
	return FString::Printf(TEXT("%s / %s"),
		*ASpaceMMOPlayerController::GroupDigits(Health), *ASpaceMMOPlayerController::GroupDigits(MaxHealth));
}

FString USpaceMMOCombatHud::DeathTitle(const FString& Killer)
{
	return Killer.IsEmpty() ? FString(TEXT("Killed")) : FString::Printf(TEXT("Killed by %s"), *Killer);
}

FString USpaceMMOCombatHud::StaminaLine(const double Stamina, const double MaxStamina)
{
	return FString::Printf(TEXT("%d / %d"),
		FMath::Max(FMath::FloorToInt32(Stamina), 0), FMath::Max(FMath::RoundToInt32(MaxStamina), 0));
}

FLinearColor USpaceMMOCombatHud::StaminaColour(const double SecondsSinceRefused)
{
	if (SecondsSinceRefused < 0.0 || SecondsSinceRefused >= StaminaFlashSeconds)
	{
		return StaminaAmber;
	}

	return FMath::Lerp(StaminaFlash, StaminaAmber, static_cast<float>(SecondsSinceRefused / StaminaFlashSeconds));
}

void USpaceMMOCombatHud::NativeConstruct()
{
	Super::NativeConstruct();

	using namespace SpaceMMO;

	PanelLook::Apply(HealthLabelText, Style::ETextRole::Column);
	PanelLook::Apply(HealthValueText, Style::ETextRole::Body);
	PanelLook::Apply(StaminaLabelText, Style::ETextRole::Column);
	PanelLook::Apply(StaminaValueText, Style::ETextRole::Body);

	if (StaminaBar != nullptr)
	{
		StaminaBar->SetWidgetStyle(BarStyle(FLinearColor::White));
		StaminaBar->SetFillColorAndOpacity(StaminaAmber);
	}
	PanelLook::Apply(DeathTitleText, Style::ETextRole::Title);
	PanelLook::Apply(DeathNoteText, Style::ETextRole::Note);

	if (HealthBar != nullptr)
	{
		HealthBar->SetWidgetStyle(BarStyle(FLinearColor::White));
		HealthBar->SetFillColorAndOpacity(HealthRed);
	}

	if (RespawnButton != nullptr)
	{
		RespawnButton->OnClicked.AddUniqueDynamic(this, &USpaceMMOCombatHud::HandleRespawnClicked);
	}
}

void USpaceMMOCombatHud::HandleRespawnClicked()
{
	const APawn* Pawn = GetOwningPlayerPawn();

	if (USpaceMMOCombatComponent* Combat = Pawn != nullptr ? Pawn->FindComponentByClass<USpaceMMOCombatComponent>() : nullptr)
	{
		Combat->RequestRespawn();
	}
}

void USpaceMMOCombatHud::StyleBar(UProgressBar* Bar, UImage* Glow, const float Fraction, const double Now) const
{
	const float Strength = GlowStrength(Fraction);
	const float Rate = PulseRate(Fraction);

	// A beat between 70% and 100% of the glow's strength when pulsing, steady when not.
	const float Beat = Rate > 0.0f ? 0.85f + 0.15f * FMath::Sin(static_cast<float>(Now) * 2.0f * PI * Rate) : 1.0f;

	if (Bar != nullptr)
	{
		Bar->SetPercent(FMath::Clamp(Fraction, 0.0f, 1.0f));

		// The fill itself brightens towards the low end, so even with the glow hidden the bar says it. A tint over a
		// white fill rather than a new style each frame, which would lay the bar out again every time.
		Bar->SetFillColorAndOpacity(FLinearColor::LerpUsingHSV(HealthRed, HealthRedBright, Strength));
	}

	if (Glow != nullptr)
	{
		FLinearColor Halo = HealthRedBright;
		Halo.A = 0.55f * Strength * Beat;
		Glow->SetColorAndOpacity(Halo);
	}
}

USpaceMMOCombatHud::FTargetMark& USpaceMMOCombatHud::MarkFor(AActor* Target)
{
	for (FTargetMark& Mark : Marks)
	{
		if (Mark.Target.Get() == Target)
		{
			return Mark;
		}
	}

	// Reuse one whose target is gone or long since hit, before making another.
	for (FTargetMark& Mark : Marks)
	{
		if (!Mark.Target.IsValid())
		{
			Mark.Target = Target;

			return Mark;
		}
	}

	using namespace SpaceMMO;

	FTargetMark Made;
	Made.Target = Target;
	Made.Box = WidgetTree->ConstructWidget<UVerticalBox>();
	Made.Name = WidgetTree->ConstructWidget<UTextBlock>();
	Made.Bar = WidgetTree->ConstructWidget<UProgressBar>();
	Made.Glow = WidgetTree->ConstructWidget<UImage>();

	PanelLook::Apply(Made.Name, Style::ETextRole::Column, Style::TextPrimary());
	Made.Name->SetJustification(ETextJustify::Center);

	if (UVerticalBoxSlot* NameSlot = Made.Box->AddChildToVerticalBox(Made.Name))
	{
		NameSlot->SetHorizontalAlignment(HAlign_Center);
	}

	// The bar over its own faint glow: an overlay of two, at a fixed size.
	USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>();
	Size->SetWidthOverride(110.0f);
	Size->SetHeightOverride(5.0f);

	UCanvasPanel* Layer = WidgetTree->ConstructWidget<UCanvasPanel>();
	Size->AddChild(Layer);

	FSlateBrush GlowBrush = Style::Rounded(FLinearColor::White, Style::White(0.0f), 0.0f, FVector4(6, 6, 6, 6));
	Made.Glow->SetBrush(GlowBrush);

	if (UCanvasPanelSlot* GlowSlot = Layer->AddChildToCanvas(Made.Glow))
	{
		GlowSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
		GlowSlot->SetOffsets(FMargin(-4.0f, -4.0f, -4.0f, -4.0f));
	}

	if (UCanvasPanelSlot* BarSlot = Layer->AddChildToCanvas(Made.Bar))
	{
		BarSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
		BarSlot->SetOffsets(FMargin(0.0f));
	}

	Made.Bar->SetWidgetStyle(BarStyle(FLinearColor::White));
	Made.Bar->SetFillColorAndOpacity(HealthRed);

	if (UVerticalBoxSlot* BarSlot = Made.Box->AddChildToVerticalBox(Size))
	{
		BarSlot->SetPadding(FMargin(0.0f, 4.0f, 0.0f, 0.0f));
		BarSlot->SetHorizontalAlignment(HAlign_Center);
	}

	if (UCanvasPanelSlot* BoxSlot = MarksCanvas->AddChildToCanvas(Made.Box))
	{
		BoxSlot->SetAutoSize(true);
	}

	Made.Box->SetVisibility(ESlateVisibility::HitTestInvisible);

	return Marks.Add_GetRef(Made);
}

UTextBlock* USpaceMMOCombatHud::NumberAt(const int32 Index)
{
	while (Numbers.Num() <= Index)
	{
		UTextBlock* Made = WidgetTree->ConstructWidget<UTextBlock>();

		SpaceMMO::PanelLook::Apply(Made, SpaceMMO::Style::ETextRole::Body);
		Made->SetVisibility(ESlateVisibility::Collapsed);

		if (UCanvasPanelSlot* NumberSlot = MarksCanvas->AddChildToCanvas(Made))
		{
			NumberSlot->SetAutoSize(true);
		}

		Numbers.Add(Made);
	}

	return Numbers[Index];
}

void USpaceMMOCombatHud::NativeTick(const FGeometry& Geometry, const float DeltaSeconds)
{
	Super::NativeTick(Geometry, DeltaSeconds);

	APlayerController* Controller = GetOwningPlayer();
	const APawn* Pawn = Controller != nullptr ? Controller->GetPawn() : nullptr;
	const USpaceMMOCombatComponent* Combat = Pawn != nullptr ? Pawn->FindComponentByClass<USpaceMMOCombatComponent>() : nullptr;

	const UWorld* World = GetWorld();
	const double Now = World != nullptr ? World->GetRealTimeSeconds() : 0.0;

	// Your health: on foot, once the server has said what it is.
	const bool bShowHealth = Combat != nullptr && Combat->HasHealth();

	if (HealthPanel != nullptr)
	{
		HealthPanel->SetVisibility(bShowHealth ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}

	if (bShowHealth)
	{
		const int32 Health = Combat->GetHealth();
		const int32 Max = Combat->GetMaxHealth();

		if (HealthValueText != nullptr)
		{
			HealthValueText->SetText(FText::FromString(HealthLine(Health, Max)));
		}

		StyleBar(HealthBar, HealthGlow, Max > 0 ? static_cast<float>(Health) / Max : 0.0f, Now);

		// Stamina, always with health (Joe, 19 October): the pool the owning client predicts, and a flash for each
		// dodge it refused for want of it.
		if (const ASpaceMMOCharacterPawn* Body = Cast<ASpaceMMOCharacterPawn>(Pawn))
		{
			const double Stamina = Body->GetStamina();
			const double StaminaMax = Body->GetStaminaMax();

			if (!bSeenRefusals || Body->GetDodgesRefused() != SeenRefusals)
			{
				RefusedAt = bSeenRefusals ? Now : -1.0;
				SeenRefusals = Body->GetDodgesRefused();
				bSeenRefusals = true;
			}

			if (StaminaValueText != nullptr)
			{
				StaminaValueText->SetText(FText::FromString(StaminaLine(Stamina, StaminaMax)));
			}

			if (StaminaBar != nullptr)
			{
				StaminaBar->SetPercent(StaminaMax > 0.0 ? static_cast<float>(Stamina / StaminaMax) : 0.0f);
				StaminaBar->SetFillColorAndOpacity(StaminaColour(RefusedAt < 0.0 ? -1.0 : Now - RefusedAt));
			}
		}
	}

	// Dying, and coming back.
	const bool bDead = Combat != nullptr && Combat->IsDead();

	if (DeathPanel != nullptr)
	{
		DeathPanel->SetVisibility(bDead ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	if (bDead && DeathTitleText != nullptr)
	{
		DeathTitleText->SetText(FText::FromString(DeathTitle(Combat->GetKillerName())));
	}

	if (MarksCanvas == nullptr || WidgetTree == nullptr)
	{
		return;
	}

	// Bars over the heads of whoever you have hit lately, and the numbers where each hit landed.
	TArray<const AActor*> Shown;
	int32 NumbersUsed = 0;

	const USpaceMMORenderOriginSubsystem* Origin =
		World != nullptr ? World->GetSubsystem<USpaceMMORenderOriginSubsystem>() : nullptr;

	if (Combat != nullptr)
	{
		const TArray<FSpaceMMODealtHit>& Hits = Combat->GetRecentHits();

		for (int32 Index = Hits.Num() - 1; Index >= 0; --Index)
		{
			const FSpaceMMODealtHit& Hit = Hits[Index];
			const double Age = Now - Hit.ArrivedSeconds;

			AActor* Target = Hit.Target.Get();

			if (Target != nullptr && Age <= TargetBarSeconds && !Shown.Contains(Target))
			{
				const USpaceMMOCombatComponent* Theirs = Target->FindComponentByClass<USpaceMMOCombatComponent>();
				FVector2D Above;

				if (Theirs != nullptr && SpaceMMO::Hud::ProjectAbove(Controller, Target, 0.95f, Above))
				{
					FTargetMark& Mark = MarkFor(Target);

					Mark.Name->SetText(FText::FromString(Theirs->GetDisplayName()));
					StyleBar(Mark.Bar, Mark.Glow,
						Theirs->GetMaxHealth() > 0 ? static_cast<float>(Theirs->GetHealth()) / Theirs->GetMaxHealth() : 0.0f,
						Now);

					SpaceMMO::Hud::PlaceAt(Mark.Box, Above);
					Mark.Box->SetVisibility(ESlateVisibility::HitTestInvisible);

					Shown.Add(Target);
				}
			}


			if (Age > NumberSeconds || Origin == nullptr)
			{
				continue;
			}

			FVector2D Point;

			if (!UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(
				Controller, Origin->ToWorldLocation(FSystemCoordinate(Hit.PointKilometres)), Point, false))
			{
				continue;
			}


			// Floats up and fades over its life.
			const float Life = static_cast<float>(Age / NumberSeconds);

			UTextBlock* Number = NumberAt(NumbersUsed++);
			FLinearColor Colour = Hit.Zone == EBackendHitZone::Head ? HeadGold : SpaceMMO::Style::TextPrimary();
			Colour.A = 1.0f - Life * Life;

			Number->SetText(FText::AsNumber(Hit.Damage));
			Number->SetColorAndOpacity(FSlateColor(Colour));
			SpaceMMO::Hud::PlaceAt(Number, Point - FVector2D(-24.0, 40.0 * Life));
			Number->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
	}

	for (FTargetMark& Mark : Marks)
	{
		if (!Shown.Contains(Mark.Target.Get()))
		{
			Mark.Box->SetVisibility(ESlateVisibility::Collapsed);
			Mark.Target = nullptr;
		}
	}

	for (int32 Index = NumbersUsed; Index < Numbers.Num(); ++Index)
	{
		Numbers[Index]->SetVisibility(ESlateVisibility::Collapsed);
	}
}
