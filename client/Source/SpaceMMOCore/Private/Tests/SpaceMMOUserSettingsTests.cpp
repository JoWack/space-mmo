#include "Misc/AutomationTest.h"
#include "SpaceMMOUserSettings.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * Mouse sensitivity and invert look (task 110). Pure, so the arithmetic is tested here; whether the
 * pawns actually call it is the playtest's question, and the settings log line says what was set.
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOUserSettingsScaleLookTest,
	"SpaceMMO.Settings.ScaleLook",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOUserSettingsScaleLookTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Defaults change nothing"), USpaceMMOUserSettings::ScaleLook(0.3f, true, 1.0f, false), 0.3f);
	TestEqual(TEXT("Sensitivity multiplies"), USpaceMMOUserSettings::ScaleLook(0.3f, false, 2.0f, false), 0.6f);

	// Invert is vertical only. Inverting turn as well is a different setting nobody asked for.
	TestEqual(TEXT("Invert flips vertical"), USpaceMMOUserSettings::ScaleLook(0.3f, true, 1.0f, true), -0.3f);
	TestEqual(TEXT("Invert leaves horizontal"), USpaceMMOUserSettings::ScaleLook(0.3f, false, 1.0f, true), 0.3f);

	// A zero from a hand-edited ini would read as a dead mouse with no error anywhere.
	TestEqual(
		TEXT("Zero is clamped to the minimum"),
		USpaceMMOUserSettings::ScaleLook(1.0f, false, 0.0f, false),
		USpaceMMOUserSettings::MinSensitivity);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOUserSettingsSliderTest,
	"SpaceMMO.Settings.SensitivitySlider",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOUserSettingsSliderTest::RunTest(const FString& Parameters)
{
	// The middle is the speed the game was tuned at, and the ends are the limits.
	TestTrue(TEXT("Middle is 1x"), FMath::IsNearlyEqual(USpaceMMOUserSettings::SensitivityFromSlider(0.5f), 1.0f, 1e-4f));
	TestTrue(TEXT("Left end"), FMath::IsNearlyEqual(USpaceMMOUserSettings::SensitivityFromSlider(0.0f), USpaceMMOUserSettings::MinSensitivity, 1e-4f));
	TestTrue(TEXT("Right end"), FMath::IsNearlyEqual(USpaceMMOUserSettings::SensitivityFromSlider(1.0f), USpaceMMOUserSettings::MaxSensitivity, 1e-4f));

	for (const float Sensitivity : {0.25f, 0.6f, 1.0f, 1.7f, 4.0f})
	{
		const float Back = USpaceMMOUserSettings::SensitivityFromSlider(USpaceMMOUserSettings::SliderFromSensitivity(Sensitivity));

		TestTrue(*FString::Printf(TEXT("%.2f round trips (got %.4f)"), Sensitivity, Back), FMath::IsNearlyEqual(Back, Sensitivity, 1e-3f));
	}

	return true;
}

#endif
