#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "GenericPlatform/GenericWindow.h"

#include "SpaceMMOGameMenus.generated.h"

/*
 * The Esc menu and settings (task 110). Overlays over the world, which keeps running: this is an
 * MMO, and nothing a menu does stops the server.
 *
 * As with the character screens, every part is optional, buttons are bound by name, and the look is
 * the Widget Blueprint's.
 */

/**
 * Esc in game: Resume, Settings, Sign out, Quit game.
 *
 * Parts: <c>ResumeButton</c>, <c>SettingsButton</c>, <c>SignOutButton</c>, <c>QuitButton</c>.
 * Esc again resumes.
 */
UCLASS()
class SPACEMMOBACKEND_API USpaceMMOEscapeMenu : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;

	virtual FReply NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent) override;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Menus", meta = (BindWidgetOptional))
	TObjectPtr<class UButton> ResumeButton;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Menus", meta = (BindWidgetOptional))
	TObjectPtr<class UButton> SettingsButton;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Menus", meta = (BindWidgetOptional))
	TObjectPtr<class UButton> SignOutButton;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Menus", meta = (BindWidgetOptional))
	TObjectPtr<class UButton> QuitButton;

private:
	UFUNCTION()
	void HandleResume();

	UFUNCTION()
	void HandleSettings();

	UFUNCTION()
	void HandleSignOut();

	UFUNCTION()
	void HandleQuit();
};

/**
 * Settings: mouse sensitivity, invert look, window mode, resolution and graphics quality.
 *
 * Parts: <c>SensitivitySlider</c> and <c>SensitivityText</c>; <c>InvertLookCheck</c>;
 * <c>WindowModeCombo</c>, <c>ResolutionCombo</c> and <c>QualityCombo</c> (string combo boxes, filled
 * here); <c>BackButton</c>, <c>ApplyButton</c>.
 *
 * Nothing changes until Apply. Back discards what has not been applied and returns to the Esc menu.
 */
UCLASS()
class SPACEMMOBACKEND_API USpaceMMOSettingsScreen : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Called by the controller each time the screen is shown: reads the saved settings in. */
	void Open();

	UFUNCTION(BlueprintCallable, Category = "SpaceMMO|Menus")
	void Apply();

	/** "1.00x". */
	static FString DescribeSensitivity(float Sensitivity);

	/** "Fullscreen", "Borderless window", "Windowed". */
	static TArray<FString> WindowModeNames();

	static FString WindowModeName(EWindowMode::Type Mode);

	static EWindowMode::Type WindowModeFromName(const FString& Name);

	/** "1920 x 1080", and back. False for anything that is not two positive numbers. */
	static FString DescribeResolution(FIntPoint Resolution);

	static bool ParseResolution(const FString& Text, FIntPoint& OutResolution);

	/** "Low", "Medium", "High", "Epic": Unreal's overall scalability levels 0 to 3. */
	static TArray<FString> QualityNames();

	/** A level's name, or "Custom" for -1, which is Unreal's answer when the groups disagree. */
	static FString QualityName(int32 Level);

	/** The level for a name, or -1 for "Custom" and anything unknown, which Apply leaves alone. */
	static int32 QualityFromName(const FString& Name);

protected:
	virtual void NativeConstruct() override;

	virtual FReply NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent) override;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Menus", meta = (BindWidgetOptional))
	TObjectPtr<class USlider> SensitivitySlider;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Menus", meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> SensitivityText;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Menus", meta = (BindWidgetOptional))
	TObjectPtr<class UCheckBox> InvertLookCheck;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Menus", meta = (BindWidgetOptional))
	TObjectPtr<class UComboBoxString> WindowModeCombo;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Menus", meta = (BindWidgetOptional))
	TObjectPtr<class UComboBoxString> ResolutionCombo;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Menus", meta = (BindWidgetOptional))
	TObjectPtr<class UComboBoxString> QualityCombo;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Menus", meta = (BindWidgetOptional))
	TObjectPtr<class UButton> BackButton;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Menus", meta = (BindWidgetOptional))
	TObjectPtr<class UButton> ApplyButton;

private:
	UFUNCTION()
	void HandleBack();

	UFUNCTION()
	void HandleSliderChanged(float Value);
};
