#include "SpaceMMOGameMenus.h"

#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Kismet/KismetSystemLibrary.h"
#include "SpaceMMOBackendLog.h"
#include "SpaceMMOPlayerController.h"
#include "SpaceMMOUserSettings.h"

namespace SpaceMMOGameMenus
{
	ASpaceMMOPlayerController* ControllerFor(const UUserWidget* Widget)
	{
		return Widget != nullptr ? Cast<ASpaceMMOPlayerController>(Widget->GetOwningPlayer()) : nullptr;
	}

	/** Fills a combo box and selects one entry, adding it if it is not among the options. */
	void Fill(UComboBoxString* Combo, const TArray<FString>& Options, const FString& Selected)
	{
		if (Combo == nullptr)
		{
			return;
		}

		Combo->ClearOptions();

		for (const FString& Option : Options)
		{
			Combo->AddOption(Option);
		}

		// Added rather than dropped. The current resolution of a window someone dragged, or a
		// "Custom" quality from an edited ini, is still what is set, and a box showing something else
		// would be lying about it.
		if (!Selected.IsEmpty() && !Options.Contains(Selected))
		{
			Combo->AddOption(Selected);
		}

		Combo->SetSelectedOption(Selected);
	}
}

// ---------------------------------------------------------------------------------------------------
// Esc menu

void USpaceMMOEscapeMenu::NativeConstruct()
{
	Super::NativeConstruct();

	SetIsFocusable(true);

	if (ResumeButton != nullptr)
	{
		ResumeButton->OnClicked.AddUniqueDynamic(this, &USpaceMMOEscapeMenu::HandleResume);
	}

	if (SettingsButton != nullptr)
	{
		SettingsButton->OnClicked.AddUniqueDynamic(this, &USpaceMMOEscapeMenu::HandleSettings);
	}

	if (SignOutButton != nullptr)
	{
		SignOutButton->OnClicked.AddUniqueDynamic(this, &USpaceMMOEscapeMenu::HandleSignOut);
	}

	if (QuitButton != nullptr)
	{
		QuitButton->OnClicked.AddUniqueDynamic(this, &USpaceMMOEscapeMenu::HandleQuit);
	}
}

FReply USpaceMMOEscapeMenu::NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent)
{
	// Handled here, not by the input binding that opened the menu: with the input mode UI only the
	// controller's bindings hear nothing, so this is the only place a second Esc can arrive.
	if (KeyEvent.GetKey() == EKeys::Escape)
	{
		HandleResume();

		return FReply::Handled();
	}

	return Super::NativeOnKeyDown(Geometry, KeyEvent);
}

void USpaceMMOEscapeMenu::HandleResume()
{
	if (ASpaceMMOPlayerController* Controller = SpaceMMOGameMenus::ControllerFor(this))
	{
		Controller->CloseMenu();
	}
}

void USpaceMMOEscapeMenu::HandleSettings()
{
	if (ASpaceMMOPlayerController* Controller = SpaceMMOGameMenus::ControllerFor(this))
	{
		Controller->ShowSettings();
	}
}

void USpaceMMOEscapeMenu::HandleSignOut()
{
	if (ASpaceMMOPlayerController* Controller = SpaceMMOGameMenus::ControllerFor(this))
	{
		Controller->SignOutAndReload();
	}
}

void USpaceMMOEscapeMenu::HandleQuit()
{
	if (ASpaceMMOPlayerController* Controller = SpaceMMOGameMenus::ControllerFor(this))
	{
		Controller->QuitGame();
	}
}

// ---------------------------------------------------------------------------------------------------
// Settings

FString USpaceMMOSettingsScreen::DescribeSensitivity(const float Sensitivity)
{
	return FString::Printf(TEXT("%.2fx"), Sensitivity);
}

TArray<FString> USpaceMMOSettingsScreen::WindowModeNames()
{
	return {TEXT("Fullscreen"), TEXT("Borderless window"), TEXT("Windowed")};
}

FString USpaceMMOSettingsScreen::WindowModeName(const EWindowMode::Type Mode)
{
	switch (Mode)
	{
	case EWindowMode::Fullscreen:
		return TEXT("Fullscreen");

	case EWindowMode::WindowedFullscreen:
		return TEXT("Borderless window");

	default:
		return TEXT("Windowed");
	}
}

EWindowMode::Type USpaceMMOSettingsScreen::WindowModeFromName(const FString& Name)
{
	if (Name == TEXT("Fullscreen"))
	{
		return EWindowMode::Fullscreen;
	}

	if (Name == TEXT("Borderless window"))
	{
		return EWindowMode::WindowedFullscreen;
	}

	return EWindowMode::Windowed;
}

FString USpaceMMOSettingsScreen::DescribeResolution(const FIntPoint Resolution)
{
	return FString::Printf(TEXT("%d x %d"), Resolution.X, Resolution.Y);
}

bool USpaceMMOSettingsScreen::ParseResolution(const FString& Text, FIntPoint& OutResolution)
{
	FString Width;
	FString Height;

	if (!Text.Split(TEXT(" x "), &Width, &Height))
	{
		return false;
	}

	Width.TrimStartAndEndInline();
	Height.TrimStartAndEndInline();

	if (!Width.IsNumeric() || !Height.IsNumeric())
	{
		return false;
	}

	const int32 X = FCString::Atoi(*Width);
	const int32 Y = FCString::Atoi(*Height);

	if (X <= 0 || Y <= 0)
	{
		return false;
	}

	OutResolution = FIntPoint(X, Y);

	return true;
}

TArray<FString> USpaceMMOSettingsScreen::QualityNames()
{
	return {TEXT("Low"), TEXT("Medium"), TEXT("High"), TEXT("Epic")};
}

FString USpaceMMOSettingsScreen::QualityName(const int32 Level)
{
	const TArray<FString> Names = QualityNames();

	return Names.IsValidIndex(Level) ? Names[Level] : FString(TEXT("Custom"));
}

int32 USpaceMMOSettingsScreen::QualityFromName(const FString& Name)
{
	return QualityNames().IndexOfByKey(Name);
}

void USpaceMMOSettingsScreen::NativeConstruct()
{
	Super::NativeConstruct();

	SetIsFocusable(true);

	if (BackButton != nullptr)
	{
		BackButton->OnClicked.AddUniqueDynamic(this, &USpaceMMOSettingsScreen::HandleBack);
	}

	if (ApplyButton != nullptr)
	{
		ApplyButton->OnClicked.AddUniqueDynamic(this, &USpaceMMOSettingsScreen::Apply);
	}

	if (SensitivitySlider != nullptr)
	{
		SensitivitySlider->OnValueChanged.AddUniqueDynamic(this, &USpaceMMOSettingsScreen::HandleSliderChanged);
	}
}

FReply USpaceMMOSettingsScreen::NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent)
{
	if (KeyEvent.GetKey() == EKeys::Escape)
	{
		HandleBack();

		return FReply::Handled();
	}

	return Super::NativeOnKeyDown(Geometry, KeyEvent);
}

void USpaceMMOSettingsScreen::Open()
{
	const USpaceMMOUserSettings* Settings = USpaceMMOUserSettings::Get();

	if (Settings == nullptr)
	{
		// Logged, because every control would otherwise show a default that is not what is set, and
		// Apply would write those defaults over the real values.
		UE_LOG(LogSpaceMMOBackend, Warning,
			TEXT("Menus: the game's settings object is not a SpaceMMOUserSettings; check "
				 "GameUserSettingsClassName in DefaultEngine.ini. Settings cannot be shown."));

		return;
	}

	if (SensitivitySlider != nullptr)
	{
		SensitivitySlider->SetMinValue(0.0f);
		SensitivitySlider->SetMaxValue(1.0f);
		SensitivitySlider->SetValue(USpaceMMOUserSettings::SliderFromSensitivity(Settings->MouseSensitivity));
	}

	if (SensitivityText != nullptr)
	{
		SensitivityText->SetText(FText::FromString(DescribeSensitivity(Settings->MouseSensitivity)));
	}

	if (InvertLookCheck != nullptr)
	{
		InvertLookCheck->SetIsChecked(Settings->bInvertLook);
	}

	SpaceMMOGameMenus::Fill(WindowModeCombo, WindowModeNames(), WindowModeName(Settings->GetFullscreenMode()));

	TArray<FIntPoint> Supported;
	UKismetSystemLibrary::GetSupportedFullscreenResolutions(Supported);

	TArray<FString> Resolutions;

	// Largest first, the order people scan a resolution list in.
	for (int32 Index = Supported.Num() - 1; Index >= 0; --Index)
	{
		Resolutions.AddUnique(DescribeResolution(Supported[Index]));
	}

	SpaceMMOGameMenus::Fill(ResolutionCombo, Resolutions, DescribeResolution(Settings->GetScreenResolution()));

	SpaceMMOGameMenus::Fill(QualityCombo, QualityNames(), QualityName(Settings->GetOverallScalabilityLevel()));
}

void USpaceMMOSettingsScreen::Apply()
{
	USpaceMMOUserSettings* Settings = USpaceMMOUserSettings::Get();

	if (Settings == nullptr)
	{
		return;
	}

	if (SensitivitySlider != nullptr)
	{
		Settings->MouseSensitivity = USpaceMMOUserSettings::SensitivityFromSlider(SensitivitySlider->GetValue());
	}

	if (InvertLookCheck != nullptr)
	{
		Settings->bInvertLook = InvertLookCheck->IsChecked();
	}

	if (WindowModeCombo != nullptr)
	{
		Settings->SetFullscreenMode(WindowModeFromName(WindowModeCombo->GetSelectedOption()));
	}

	FIntPoint Resolution = FIntPoint::ZeroValue;

	if (ResolutionCombo != nullptr && ParseResolution(ResolutionCombo->GetSelectedOption(), Resolution))
	{
		Settings->SetScreenResolution(Resolution);
	}

	// "Custom" is left alone: it means the groups were set separately, and picking it back should not
	// flatten them to one level.
	const int32 Quality = QualityCombo != nullptr ? QualityFromName(QualityCombo->GetSelectedOption()) : -1;

	if (Quality >= 0)
	{
		Settings->SetOverallScalabilityLevel(Quality);
	}

	Settings->ApplySettings(/* bCheckForCommandLineOverrides */ false);

	// Logged with what was applied, so a playtest report of "the setting did nothing" can be checked
	// against what was actually set rather than what the screen showed.
	UE_LOG(LogSpaceMMOBackend, Log,
		TEXT("Settings applied: sensitivity %.2f, invert %s, %s, %s, quality %s."),
		Settings->MouseSensitivity,
		Settings->bInvertLook ? TEXT("on") : TEXT("off"),
		*WindowModeName(Settings->GetFullscreenMode()),
		*DescribeResolution(Settings->GetScreenResolution()),
		*QualityName(Settings->GetOverallScalabilityLevel()));
}

void USpaceMMOSettingsScreen::HandleBack()
{
	if (ASpaceMMOPlayerController* Controller = SpaceMMOGameMenus::ControllerFor(this))
	{
		Controller->ShowEscapeMenu();
	}
}

void USpaceMMOSettingsScreen::HandleSliderChanged(const float Value)
{
	if (SensitivityText != nullptr)
	{
		SensitivityText->SetText(
			FText::FromString(DescribeSensitivity(USpaceMMOUserSettings::SensitivityFromSlider(Value))));
	}
}
