#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "SpaceMMOBackendTypes.h"

#include "SpaceMMOSkillsScreen.generated.h"

/** One skill's row, already worded. */
USTRUCT(BlueprintType)
struct SPACEMMOBACKEND_API FSpaceMMOSkillRowText
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	FString Name;

	/** "lv 4". Level 1 is a real level here, not an absence — every character has every skill. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	FString Level;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	FString Xp;

	/** "340 to lv 5", or empty at the cap or when the server did not send progress. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	FString ToNext;

	/**
	 * How far through the level, 0 to 1, for a bar.
	 *
	 * Negative when the server did not send it. A bar bound to this must be hidden rather than
	 * drawn empty, or an old server makes every skill look freshly started.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	float Progress = -1.0f;

	/** Whether any XP has been earned, for dimming the untouched ones. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	bool bTrained = false;
};

/**
 * One row in the skills screen.
 *
 * Its own widget so the Blueprint owns what a row looks like — including whether the bar is a bar,
 * a number or nothing.
 */
UCLASS()
class SPACEMMOBACKEND_API USpaceMMOSkillRow : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Fills this row in. Called by the screen; nothing else needs it. */
	void SetRow(const FSpaceMMOSkillRowText& Row);

	/** Whether this skill has been trained at all. Bind dimming to it. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	bool bTrained = false;

	/** Whether there is a progress figure to draw. Bind the bar's visibility to it. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	bool bHasProgress = false;

protected:
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> NameText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> LevelText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> XpText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> ToNextText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UProgressBar> ProgressBar;
};

/**
 * The skills screen, opened with K.
 *
 * <strong>Every skill, not just the trained ones.</strong> The always-on panel filtered to
 * <c>Xp &gt; 0</c> for a good reason — thirty untouched zeroes would bury the one line that changed
 * — but that reasoning is about something permanently on screen. A screen somebody opens on purpose
 * is exactly where the full list belongs, and the combat milestone adds eight skills a player would
 * otherwise have no way to discover.
 *
 * <strong>It does not set its own visibility</strong>; see
 * <c>ASpaceMMOPlayerController::UpdateHudContext</c>, which owns the toggle.
 */
UCLASS()
class SPACEMMOBACKEND_API USpaceMMOSkillsScreen : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * Words and orders the rows. Pure, static and tested without a widget.
	 *
	 * Trained first and alphabetical within each group, so a list that is mostly zeroes still opens
	 * on what the player has actually done — and so nothing reorders itself between refreshes,
	 * which is unreadable precisely when it is being watched.
	 */
	static TArray<FSpaceMMOSkillRowText> Build(const TArray<FBackendSkill>& Skills);

protected:
	virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;

	/** The container rows are added to. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UPanelWidget> SkillRows;

	/** What one row looks like. Set this in the Widget Blueprint's class defaults. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SpaceMMO|HUD")
	TSubclassOf<USpaceMMOSkillRow> RowClass;

private:
	/**
	 * What the rows were last built from.
	 *
	 * Rebuilding thirty widgets every frame would be wasteful and would also fight anything the
	 * player is doing with them, so the list is rebuilt only when the wording actually changes.
	 */
	FString RowSignature;
};
