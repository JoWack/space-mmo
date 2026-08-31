#include "SpaceMMOCrosshair.h"

#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Rendering/DrawElements.h"
#include "SpaceMMOCharacterPawn.h"
#include "SpaceMMOCrosshairMarker.h"
#include "SpaceMMOShipPawn.h"

namespace
{
	/** Draws a line twice: a thicker dark one, then the light one over it. */
	void Stroke(
		FSlateWindowElementList& Elements,
		const int32 LayerId,
		const FPaintGeometry& Geometry,
		const FVector2D& From,
		const FVector2D& To,
		const FLinearColor& Line,
		const FLinearColor& Outline,
		const float Thickness)
	{
		TArray<FVector2D> Points;
		Points.Add(From);
		Points.Add(To);

		FSlateDrawElement::MakeLines(
			Elements, LayerId, Geometry, Points, ESlateDrawEffect::None, Outline, true,
			Thickness + 2.0f);

		FSlateDrawElement::MakeLines(
			Elements, LayerId + 1, Geometry, Points, ESlateDrawEffect::None, Line, true, Thickness);
	}

	/** A ring, as a closed run of short lines. */
	void Ring(
		FSlateWindowElementList& Elements,
		const int32 LayerId,
		const FPaintGeometry& Geometry,
		const FVector2D& Centre,
		const float Radius,
		const FLinearColor& Line,
		const FLinearColor& Outline,
		const float Thickness)
	{
		constexpr int32 Segments = 16;

		TArray<FVector2D> Points;
		Points.Reserve(Segments + 1);

		for (int32 Index = 0; Index <= Segments; ++Index)
		{
			const double Angle = 2.0 * UE_DOUBLE_PI * Index / Segments;

			Points.Add(Centre + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius);
		}

		FSlateDrawElement::MakeLines(
			Elements, LayerId, Geometry, Points, ESlateDrawEffect::None, Outline, true,
			Thickness + 2.0f);

		FSlateDrawElement::MakeLines(
			Elements, LayerId + 1, Geometry, Points, ESlateDrawEffect::None, Line, true, Thickness);
	}
}

void USpaceMMOCrosshair::NativeTick(const FGeometry& Geometry, const float DeltaSeconds)
{
	Super::NativeTick(Geometry, DeltaSeconds);

	bMarkerVisible = false;

	const APlayerController* const Controller = GetOwningPlayer();

	const APawn* const Pawn = Controller != nullptr ? Controller->GetPawn() : nullptr;

	if (Pawn == nullptr)
	{
		return;
	}

	// Faded while the camera is swung, because mid-orbit the view points somewhere the pawn has no
	// opinion about and a reticle there is simply lying about what would happen.
	bool bOrbiting = false;

	if (const ASpaceMMOShipPawn* const Ship = Cast<ASpaceMMOShipPawn>(Pawn))
	{
		bOrbiting = Ship->IsOrbiting();
	}
	else if (const ASpaceMMOCharacterPawn* const Character = Cast<ASpaceMMOCharacterPawn>(Pawn))
	{
		bOrbiting = Character->IsOrbiting();
	}

	Opacity = FMath::FInterpTo(
		Opacity, bOrbiting ? 0.0f : 1.0f, DeltaSeconds,
		FadeSeconds > 0.0f ? 1.0f / FadeSeconds : 0.0f);

	// Only a ship gets the second marker. On foot the character goes where it is pointed and the
	// reticle already says so; a velocity marker there would be a second crosshair that never
	// separates from the first.
	const ASpaceMMOShipPawn* const Ship = Cast<ASpaceMMOShipPawn>(Pawn);

	if (Ship == nullptr)
	{
		return;
	}

	const APlayerCameraManager* const Camera = Controller->PlayerCameraManager;

	if (Camera == nullptr)
	{
		return;
	}

	const FVector Velocity = Ship->GetFlightState().Velocity;

	// Centimetres per second against a threshold in metres.
	if (Velocity.Size() < MarkerMinimumSpeedMetresPerSecond * 100.0)
	{
		return;
	}

	const FVector Direction = Velocity.GetSafeNormal();

	const FRotator View = Camera->GetCameraRotation();

	const FVector2D Size = Geometry.GetLocalSize();

	if (Size.X <= 0.0 || Size.Y <= 0.0)
	{
		return;
	}

	// Half the width over the tangent of half the horizontal field of view: the pixels-per-radian
	// this projection works in. Taken from the camera rather than assumed, because the ship and the
	// character do not have to share a field of view and one day will not.
	const double HalfFovRadians =
		FMath::DegreesToRadians(FMath::Clamp(Camera->GetFOVAngle(), 1.0f, 179.0f) * 0.5f);

	const double Focal = (Size.X * 0.5) / FMath::Tan(HalfFovRadians);

	bMarkerVisible = FCrosshairMarker::ScreenOffset(
		FVector::DotProduct(Direction, View.Vector()),
		FVector::DotProduct(Direction, FRotationMatrix(View).GetUnitAxis(EAxis::Y)),
		FVector::DotProduct(Direction, FRotationMatrix(View).GetUnitAxis(EAxis::Z)),
		Focal,
		FMath::Min(Size.X, Size.Y) * 0.5 * MarkerMaxRadiusFraction,
		MarkerOffset);
}

int32 USpaceMMOCrosshair::NativePaint(
	const FPaintArgs& Args,
	const FGeometry& Geometry,
	const FSlateRect& CullingRect,
	FSlateWindowElementList& Elements,
	const int32 LayerId,
	const FWidgetStyle& Style,
	const bool bParentEnabled) const
{
	const int32 Base = Super::NativePaint(
		Args, Geometry, CullingRect, Elements, LayerId, Style, bParentEnabled);

	if (Opacity <= 0.01f)
	{
		return Base;
	}

	const FVector2D Size = Geometry.GetLocalSize();

	if (Size.X <= 0.0 || Size.Y <= 0.0)
	{
		return Base;
	}

	const FPaintGeometry Paint = Geometry.ToPaintGeometry();

	const FVector2D Centre(Size.X * 0.5, Size.Y * 0.5);

	FLinearColor Line = LineColour;
	FLinearColor Outline = OutlineColour;

	Line.A *= Opacity;
	Outline.A *= Opacity;

	// Four ticks around a gap, and no dot in the middle: at this size a centre dot and the gap
	// around it fight each other, and the gap is what lets you see what you are pointing at.
	const FVector2D Axes[4] = {
		FVector2D(1.0, 0.0), FVector2D(-1.0, 0.0), FVector2D(0.0, 1.0), FVector2D(0.0, -1.0)
	};

	for (const FVector2D& Axis : Axes)
	{
		Stroke(
			Elements, Base, Paint,
			Centre + Axis * CentreGap,
			Centre + Axis * (CentreGap + TickLength),
			Line, Outline, Thickness);
	}

	if (bMarkerVisible)
	{
		Ring(Elements, Base, Paint, Centre + MarkerOffset, MarkerRadius, Line, Outline, Thickness);
	}

	return Base + 2;
}
