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
	 *
	 * Empty when there is nothing to orbit; see bHasOrbital.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	FString Orbital;

	/** Whether Orbital means anything, so a label can be hidden alongside an empty value. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	bool bHasOrbital = false;

	/** ORBIT, ATMOSPHERE or SURFACE. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	FString Proximity;

	/**
	 * The station being pointed at, or empty when there is none to name (task 160).
	 *
	 * Empty rather than "none", so a pilot flying between worlds is not reading a dead row on every
	 * frame. Wording comes from <c>FSpaceMMOStationLine</c>, which the on-foot readout shares.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	FString Station;

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

	/**
	 * The station the marker has settled on, already chosen by <c>FSpaceMMOStationMarkers</c>.
	 *
	 * <strong>Resolved by the caller rather than selected here.</strong> Which station to name is a
	 * geometric question involving the body underfoot, the limb, and whether the viewer is docked;
	 * doing it in here would need a world and would stop Build being a pure function over values,
	 * which is the whole reason the wording is testable.
	 */
	FString StationName;

	FString StationBodyName;

	double StationDistanceKilometres = 0.0;

	double StationDockingRangeKilometres = 0.0;

	bool bStationOnBody = false;
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

	/**
	 * Whether there is anything to orbit where the ship is.
	 *
	 * Bind a row's visibility to this in the designer. Every value here is a bare number now --
	 * labels live in the Widget Blueprint, so they can be reworded and restyled without a rebuild
	 * -- which means an empty orbital speed would otherwise leave its label hanging over nothing.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	bool bHasOrbitalSpeed = false;

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

	/**
	 * The station line's block, added to the Widget Blueprint by name.
	 *
	 * <strong>Optional, and its absence is announced.</strong> BindWidgetOptional means a missing
	 * block compiles and runs and simply shows nothing — which for a feature whose entire purpose
	 * is to be visible is indistinguishable from it not working. So the widget says once, in the
	 * log, that it had a station to name and nowhere to put it. A measurement that can silently not
	 * happen is worse than none.
	 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> StationText;

	/** So the warning above is said once rather than every frame. */
	bool bReportedMissingStationBlock = false;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> SystemPositionText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> DebugText;
};
