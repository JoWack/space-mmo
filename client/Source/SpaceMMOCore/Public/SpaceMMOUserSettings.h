#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameUserSettings.h"

#include "SpaceMMOUserSettings.generated.h"

/**
 * The player's own settings: Unreal's window, resolution and scalability, plus how the mouse looks
 * around (task 110).
 *
 * <strong>A subclass of Unreal's, not a second store.</strong> UGameUserSettings already saves to
 * <c>Saved/Config/.../GameUserSettings.ini</c> and already owns window mode, resolution and the
 * scalability groups; two settings files would be two answers to "what did I choose". Named in
 * DefaultEngine.ini as <c>GameUserSettingsClassName</c>, which is the engine's own hook for this.
 *
 * In Core rather than beside the screens because the pawns read it, and Core cannot see Backend.
 */
UCLASS(Config = GameUserSettings, ConfigDoNotCheckDefaults)
class SPACEMMOCORE_API USpaceMMOUserSettings : public UGameUserSettings
{
	GENERATED_BODY()

public:
	/** Multiplies every mouse look and turn. 1 is the game as tuned. */
	UPROPERTY(Config)
	float MouseSensitivity = 1.0f;

	/** Flips vertical look: walking, orbiting the camera, and pitching a ship. */
	UPROPERTY(Config)
	bool bInvertLook = false;

	/** The slider's ends. A tenth of the tuned speed is unusable and ten times it is a spin. */
	static constexpr float MinSensitivity = 0.25f;
	static constexpr float MaxSensitivity = 4.0f;

	virtual void SetToDefaults() override;

	/**
	 * The live settings, or null in a run with no engine settings object (the automated runs have
	 * one; a commandlet might not). Callers treat null as the defaults.
	 */
	static USpaceMMOUserSettings* Get();

	/**
	 * One mouse axis value, as the player has asked for it to feel.
	 *
	 * Pure and static so it is tested without an engine. The pawns call it with the live settings.
	 */
	static float ScaleLook(float Value, bool bVertical, float Sensitivity, bool bInvert);

	/** The same, with whatever is currently set. */
	static float ScaleLook(float Value, bool bVertical);

	/**
	 * Slider position, 0 to 1, to sensitivity, and back.
	 *
	 * Logarithmic, so the middle of the slider is the tuned speed and halving and doubling are the
	 * same distance either side of it. A linear 0.25-4 slider puts 1 a fifth of the way along and
	 * spends most of its length on speeds nobody wants.
	 */
	static float SensitivityFromSlider(float Slider);

	static float SliderFromSensitivity(float Sensitivity);
};
