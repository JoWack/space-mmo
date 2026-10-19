#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"

#include "SpaceMMOCombatHud.generated.h"

/**
 * What fighting puts on screen (task 194), as Joe approved the mock on 16 October: your health at bottom centre,
 * the name and a thin bar over whoever you are hitting, each hit's damage where it landed (gold for the head), and
 * when you die, a panel saying who killed you with one button to come back.
 *
 * <strong>Health bars are red, always, and glow harder as health runs low</strong> (Joe, 16 October, changing the
 * mock's ice blue). A faint glow sits behind every bar; under half health it brightens and starts to pulse, faster
 * the lower it goes.
 *
 * Reads everything from the possessed pawn's combat component, which the server keeps current; it decides nothing.
 * Built by SpaceMMOBuildMenus. Does not set its own visibility: see <c>ASpaceMMOPlayerController::UpdateHudContext</c>.
 */
UCLASS()
class SPACEMMOBACKEND_API USpaceMMOCombatHud : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * How hard a health bar glows at a fraction of full health: a faint 0.15 above half, rising to 1 at nothing.
	 * Pure and static, so the rule is tested without a widget.
	 */
	static float GlowStrength(float HealthFraction);

	/** How fast the glow pulses, in beats a second: none above half, faster as health falls. */
	static float PulseRate(float HealthFraction);

	/** "1,640 / 2,000". */
	static FString HealthLine(int32 Health, int32 MaxHealth);

	/** "KILLED BY ORENTHAL", or "KILLED" when nobody is named. */
	static FString DeathTitle(const FString& Killer);

	/** "72 / 100": what is left of the stamina pool, rounded down, so a dodge's 25 never shows as there when it is not. */
	static FString StaminaLine(double Stamina, double MaxStamina);

	/**
	 * The stamina bar's colour, Seconds after a dodge was refused for want of stamina: a flash, back to amber over a
	 * third of a second (Joe, 19 October: the bar flashes once, nothing else). Negative for never.
	 */
	static FLinearColor StaminaColour(double SecondsSinceRefused);

	/** How long a damage number floats before it is gone, and how long a target's bar stays after the last hit. */
	static constexpr double NumberSeconds = 0.9;
	static constexpr double TargetBarSeconds = 5.0;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;

	UFUNCTION()
	void HandleRespawnClicked();

	/** Bottom centre: your health. Shown on foot once health is known. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UWidget> HealthPanel;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> HealthLabelText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> HealthValueText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UProgressBar> HealthBar;

	/** The glow behind the bar. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UImage> HealthGlow;

	/** Under health: your stamina (task 195), thin and amber, always shown with it. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> StaminaLabelText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> StaminaValueText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UProgressBar> StaminaBar;

	/** Centre: shown while dead. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UWidget> DeathPanel;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> DeathTitleText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> DeathNoteText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UButton> RespawnButton;

	/** A full-screen canvas the bars over heads and the damage numbers are placed on. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UCanvasPanel> MarksCanvas;

private:
	/** One bar over one head, made the first time it is needed and reused. */
	struct FTargetMark
	{
		TWeakObjectPtr<AActor> Target;
		TObjectPtr<class UVerticalBox> Box = nullptr;
		TObjectPtr<class UTextBlock> Name = nullptr;
		TObjectPtr<class UProgressBar> Bar = nullptr;
		TObjectPtr<class UImage> Glow = nullptr;
	};

	FTargetMark& MarkFor(AActor* Target);

	class UTextBlock* NumberAt(int32 Index);

	void StyleBar(class UProgressBar* Bar, class UImage* Glow, float Fraction, double Now) const;

	TArray<FTargetMark> Marks;

	/** The pawn's count of refused dodges as last seen, and when the last one came, for the flash. */
	uint8 SeenRefusals = 0;
	bool bSeenRefusals = false;
	double RefusedAt = -1.0;

	UPROPERTY()
	TArray<TObjectPtr<class UTextBlock>> Numbers;
};
