#include "SpaceMMOUserSettings.h"

#include "Engine/Engine.h"

void USpaceMMOUserSettings::SetToDefaults()
{
	Super::SetToDefaults();

	MouseSensitivity = 1.0f;
	bInvertLook = false;
}

USpaceMMOUserSettings* USpaceMMOUserSettings::Get()
{
	return GEngine != nullptr ? Cast<USpaceMMOUserSettings>(GEngine->GetGameUserSettings()) : nullptr;
}

float USpaceMMOUserSettings::ScaleLook(
	const float Value, const bool bVertical, const float Sensitivity, const bool bInvert)
{
	// Clamped here as well as on the slider: the value is read back from an ini a person can edit,
	// and a zero there would read as a broken mouse with no error anywhere.
	const float Scale = FMath::Clamp(Sensitivity, MinSensitivity, MaxSensitivity);

	return Value * Scale * (bVertical && bInvert ? -1.0f : 1.0f);
}

float USpaceMMOUserSettings::ScaleLook(const float Value, const bool bVertical)
{
	const USpaceMMOUserSettings* Settings = Get();

	return Settings != nullptr
		? ScaleLook(Value, bVertical, Settings->MouseSensitivity, Settings->bInvertLook)
		: Value;
}

float USpaceMMOUserSettings::SensitivityFromSlider(const float Slider)
{
	const float T = FMath::Clamp(Slider, 0.0f, 1.0f);

	return FMath::Exp(FMath::Lerp(FMath::Loge(MinSensitivity), FMath::Loge(MaxSensitivity), T));
}

float USpaceMMOUserSettings::SliderFromSensitivity(const float Sensitivity)
{
	const float Clamped = FMath::Clamp(Sensitivity, MinSensitivity, MaxSensitivity);

	return (FMath::Loge(Clamped) - FMath::Loge(MinSensitivity))
		/ (FMath::Loge(MaxSensitivity) - FMath::Loge(MinSensitivity));
}
