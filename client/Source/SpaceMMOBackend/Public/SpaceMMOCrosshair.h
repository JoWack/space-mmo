#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"

#include "SpaceMMOCrosshair.generated.h"

/**
 * The reticle, and where the ship is actually going.
 *
 * <strong>Drawn rather than assembled.</strong> Every other element of this HUD is a Widget
 * Blueprint of rows and text, and this one is four ticks and two rings whose positions are computed
 * every frame. Laying that out in UMG would put half the reasoning in an asset nobody can diff and
 * the other half in code that reaches into it by name.
 *
 * The Widget Blueprint still exists and is still named in DefaultGame.ini, for the same reason as
 * the others: it is how the thing gets a class to instantiate and a place to hold its style. It is
 * empty of widgets.
 *
 * <strong>It says nothing the simulation reads.</strong> design-bible.md §8: "the camera is a client
 * concern only — it must never affect server-side validation, which is why interaction range is
 * checked against the pawn, never the camera." A crosshair is the most tempting thing on a HUD to
 * quietly promote into an aiming rule, and it is not one.
 */
UCLASS()
class SPACEMMOBACKEND_API USpaceMMOCrosshair : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Half the length of each tick, in pixels. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Crosshair")
	float TickLength = 7.0f;

	/** The gap between the centre and where each tick begins. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Crosshair")
	float CentreGap = 7.0f;

	/** How thick the lines are drawn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Crosshair")
	float Thickness = 1.5f;

	/** Radius of the ring that shows where the ship is going. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Crosshair")
	float MarkerRadius = 9.0f;

	/**
	 * How far from the centre the marker may be pinned, as a fraction of the smaller half-dimension.
	 *
	 * Short of the edge rather than on it, so a pinned marker is still a marker rather than a line
	 * along the border.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Crosshair")
	float MarkerMaxRadiusFraction = 0.8f;

	/**
	 * Below this speed the marker is not drawn, in metres per second.
	 *
	 * A ship barely moving has a velocity direction dominated by whatever it drifted last, so the
	 * marker would wander the screen while sitting still and mean nothing while doing it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Crosshair")
	float MarkerMinimumSpeedMetresPerSecond = 2.0f;

	/**
	 * The line colour, and the outline behind it.
	 *
	 * <strong>Two passes, because one colour cannot be seen everywhere.</strong> White vanishes
	 * against the ore deposits and against sunlit terrain; black vanishes against space. A dark line
	 * drawn thicker underneath a light one reads against both, which is what every game does and why.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Crosshair")
	FLinearColor LineColour = FLinearColor(1.0f, 1.0f, 1.0f, 0.85f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Crosshair")
	FLinearColor OutlineColour = FLinearColor(0.0f, 0.0f, 0.0f, 0.55f);

	/** How long the crosshair takes to fade out when the camera is swung, and back when released. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Crosshair")
	float FadeSeconds = 0.15f;

protected:
	virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;

	virtual int32 NativePaint(
		const FPaintArgs& Args,
		const FGeometry& Geometry,
		const FSlateRect& CullingRect,
		FSlateWindowElementList& Elements,
		int32 LayerId,
		const FWidgetStyle& Style,
		bool bParentEnabled) const override;

private:
	/** Where the velocity marker goes this frame, in pixels from the centre. */
	FVector2D MarkerOffset = FVector2D::ZeroVector;

	/** Whether there is a marker to draw at all. */
	bool bMarkerVisible = false;

	/** 0 while the camera is swung, 1 otherwise, eased between. */
	float Opacity = 1.0f;
};
