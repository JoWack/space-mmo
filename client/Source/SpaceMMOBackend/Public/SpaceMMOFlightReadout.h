#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "SpaceMMOCoordinates.h"
#include "SpaceMMOPlanet.h"

#include "SpaceMMOFlightReadout.generated.h"

/**
 * Everything the flight readout says, already worded.
 *
 * A struct of finished strings rather than numbers, so the formatting is a pure function that can
 * be tested without a widget, a world, or a renderer — the same arrangement that makes the panel
 * builders testable. The widget's whole job is putting these into text blocks.
 */
USTRUCT(BlueprintType)
struct SPACEMMOBACKEND_API FSpaceMMOFlightReadoutText
{
	GENERATED_BODY()

	/** Height above the terrain, which is what reaches zero on landing. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	FString Altitude;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	FString Speed;

	/**
	 * What a circular orbit would take here.
	 *
	 * Shown beside speed because it is the number that explains the flight model: on the 20 km
	 * capital it is about 443 m/s, drag caps a ship at 200, and a ship faster than orbital is
	 * thrown off the ground by its own speed rather than flying along it.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	FString Orbital;

	/** ORBIT, ATMOSPHERE or SURFACE. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	FString Proximity;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	FString SystemPosition;

	/**
	 * Render-space position, sphere altitude and rebase count, on one line.
	 *
	 * Verification of the coordinate model rather than anything a pilot needs, so it is kept apart
	 * and shown only when flight debug is on. It is what an origin-rebasing fault would show up in,
	 * which is why it exists at all.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	FString Debug;
};

/**
 * What the readout is told about the ship, in the units the ship holds them in.
 *
 * Centimetres and kilometres, matching ADR-0001 rather than the display, so every conversion
 * happens in one place — the formatter — instead of at each caller.
 */
struct SPACEMMOBACKEND_API FSpaceMMOFlightReadoutInputs
{
	FSystemCoordinate SystemPosition;

	/** Centimetres per second, as FShipFlightState holds it. */
	double SpeedCentimetresPerSecond = 0.0;

	/** Centimetres per second. Zero when there is no planet to orbit. */
	double OrbitalSpeedCentimetresPerSecond = 0.0;

	double GroundAltitudeKilometres = 0.0;

	double SphereAltitudeKilometres = 0.0;

	EPlanetProximity Proximity = EPlanetProximity::Orbital;

	FVector WorldLocationCentimetres = FVector::ZeroVector;

	int32 RebaseCount = 0;
};

/**
 * The always-on flight readout.
 *
 * <strong>Layout lives in a Widget Blueprint, not here.</strong> The text blocks below are bound by
 * name, so fonts, colours, spacing and anchoring are editable in the editor without a rebuild, and
 * a missing name fails Blueprint compilation with a clear error rather than drifting silently.
 *
 * This replaces three AddOnScreenDebugMessage calls that rendered in an order nothing could
 * influence — the ship's own readouts used keys 1, 3 and 2 and drew as 2, 3, 1 — and could be
 * pushed off the bottom of the screen by a long panel.
 */
UCLASS()
class SPACEMMOBACKEND_API USpaceMMOFlightReadout : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * Words the readout. Pure, static, and tested without a widget in sight.
	 *
	 * Metres and metres per second, because a ship doing 184 m/s reading "0.184 km/s" is a number
	 * nobody can compare at a glance — and comparing it against orbital speed is the entire reason
	 * it is on screen.
	 */
	static FSpaceMMOFlightReadoutText Build(const FSpaceMMOFlightReadoutInputs& Inputs);

	/** Whether the debug line is shown, which follows the ship's own flight-debug flag. */
	UPROPERTY(BlueprintReadWrite, Category = "SpaceMMO|HUD")
	bool bShowDebug = false;

protected:
	virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;

	/**
	 * Bound by name from the Widget Blueprint.
	 *
	 * Optional, so a Blueprint that omits one still compiles and runs: a HUD that refuses to appear
	 * because somebody deleted a row is worse than a HUD missing a row.
	 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> AltitudeText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> SpeedText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> OrbitalText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> ProximityText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> SystemPositionText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> DebugText;
};
