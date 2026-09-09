#include "SpaceMMOCrosshair.h"

#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "SpaceMMOCharacterPawn.h"
#include "SpaceMMODockingComponent.h"
#include "SpaceMMOStationMarkers.h"
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

	/**
	 * An arrowhead whose point sits on the target, opening away from the middle of the screen.
	 *
	 * <strong>One rule for both cases.</strong> A station in view and one pinned to the edge want to
	 * look like the same kind of mark, and orienting every chevron along the line from the centre
	 * outwards gives that for free: near the edge it reads as "out this way", and in the middle it
	 * still points at the thing rather than sitting beside it.
	 */
	void Chevron(
		FSlateWindowElementList& Elements,
		const int32 LayerId,
		const FPaintGeometry& Geometry,
		const FVector2D& Apex,
		const FVector2D& Direction,
		const float Size,
		const FLinearColor& Line,
		const FLinearColor& Outline,
		const float Thickness)
	{
		// Straight down when the target is dead centre, which has no outward direction of its own.
		const FVector2D Out = Direction.IsNearlyZero() ? FVector2D(0.0, -1.0) : Direction;

		const FVector2D Back(-Out.X, -Out.Y);
		const FVector2D Side(-Out.Y, Out.X);

		const FVector2D Left = Apex + (Back * Size) + (Side * Size * 0.55);
		const FVector2D Right = Apex + (Back * Size) - (Side * Size * 0.55);

		Stroke(Elements, LayerId, Geometry, Apex, Left, Line, Outline, Thickness);
		Stroke(Elements, LayerId, Geometry, Apex, Right, Line, Outline, Thickness);
	}

	/** Where the pawn is in system space, whichever kind of pawn it is. */
	FSystemCoordinate ViewerSystemPosition(const APawn& Pawn)
	{
		if (const ASpaceMMOShipPawn* const Ship = Cast<ASpaceMMOShipPawn>(&Pawn))
		{
			return Ship->GetSystemPosition();
		}

		if (const ASpaceMMOCharacterPawn* const Character = Cast<ASpaceMMOCharacterPawn>(&Pawn))
		{
			return Character->GetSystemPosition();
		}

		return FSystemCoordinate();
	}
}

USpaceMMOCrosshair::USpaceMMOCrosshair(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// A font has to come from somewhere, and a default-constructed FSlateFontInfo names none --
	// which draws nothing at all and looks exactly like a marker that failed to appear.
	StationFont = FCoreStyle::GetDefaultFontStyle("Regular", 10);
}

void USpaceMMOCrosshair::NativeTick(const FGeometry& Geometry, const float DeltaSeconds)
{
	Super::NativeTick(Geometry, DeltaSeconds);

	bMarkerVisible = false;

	// Cleared here rather than further down, beside the other piece of per-frame state.
	//
	// It used to be reset after the pawn check below, which leaves last frame's chevrons standing
	// on every frame where there is no pawn -- and the moment there is no pawn is possession
	// changing, which is precisely boarding and docking. The mark would have hung on screen
	// pointing at where a station was from where the player used to be.
	StationMarks.Reset();

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

	// The camera basis, worked out once: the station chevrons and the velocity marker project
	// through the same lens, and two copies of this arithmetic would be two chances to disagree
	// about where the middle of the screen is.
	const APlayerCameraManager* const Camera = Controller->PlayerCameraManager;

	const FVector2D Size = Geometry.GetLocalSize();

	if (Camera == nullptr || Size.X <= 0.0 || Size.Y <= 0.0)
	{
		return;
	}

	const FRotator View = Camera->GetCameraRotation();

	const FRotationMatrix ViewAxes(View);

	const FVector Forward = View.Vector();
	const FVector Right = ViewAxes.GetUnitAxis(EAxis::Y);
	const FVector Up = ViewAxes.GetUnitAxis(EAxis::Z);

	// Half the width over the tangent of half the horizontal field of view: the pixels-per-radian
	// this projection works in. Taken from the camera rather than assumed, because the ship and the
	// character do not have to share a field of view and one day will not.
	const double HalfFovRadians =
		FMath::DegreesToRadians(FMath::Clamp(Camera->GetFOVAngle(), 1.0f, 179.0f) * 0.5f);

	const double Focal = (Size.X * 0.5) / FMath::Tan(HalfFovRadians);

	const double MaxRadius = FMath::Min(Size.X, Size.Y) * 0.5 * MarkerMaxRadiusFraction;

	// ── Station chevrons (task 160) ──────────────────────────────────────────
	//
	// Before the ship check below, because these persist on foot: a character who has walked away
	// from their ship still needs to know which way the outpost is, and that is most of what task
	// 160 is about.
	if (const USpaceMMODockingComponent* const Docking =
		Pawn->FindComponentByClass<USpaceMMODockingComponent>())
	{
		TArray<FSpaceMMOStationMarkerView> Markers;
		int32 Named = INDEX_NONE;

		if (Docking->BuildStationMarkers(Markers, Named))
		{
			// System space and render space share their axes -- one is the other translated and
			// scaled -- so a direction taken in kilometres is already the direction to project.
			const FSystemCoordinate Here = ViewerSystemPosition(*Pawn);

			for (const FSpaceMMOStationMarkerView& Marker : Markers)
			{
				const FVector Towards =
					(Marker.Position.Kilometres - Here.Kilometres).GetSafeNormal();

				if (Towards.IsNearlyZero())
				{
					continue;
				}

				FStationMark Mark;

				// Nothing honest to draw for a direction exactly behind the camera, and inventing a
				// side of the screen for it turns a pilot the wrong way.
				if (!FCrosshairMarker::ScreenOffset(
					FVector::DotProduct(Towards, Forward),
					FVector::DotProduct(Towards, Right),
					FVector::DotProduct(Towards, Up),
					Focal,
					MaxRadius,
					Mark.Offset))
				{
					continue;
				}

				Mark.Name = Marker.Name;
				Mark.Distance = FSpaceMMOStationLine::Short(Marker.DistanceKilometres);

				StationMarks.Add(Mark);
			}
		}
	}

	// ── The velocity marker ──────────────────────────────────────────────────
	//
	// Only a ship gets it. On foot the character goes where it is pointed and the reticle already
	// says so; a velocity marker there would be a second crosshair that never separates from the
	// first.
	const ASpaceMMOShipPawn* const Ship = Cast<ASpaceMMOShipPawn>(Pawn);

	if (Ship == nullptr)
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

	bMarkerVisible = FCrosshairMarker::ScreenOffset(
		FVector::DotProduct(Direction, Forward),
		FVector::DotProduct(Direction, Right),
		FVector::DotProduct(Direction, Up),
		Focal,
		MaxRadius,
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

	const FVector2D Size = Geometry.GetLocalSize();

	if (Size.X <= 0.0 || Size.Y <= 0.0)
	{
		return Base;
	}

	const FPaintGeometry Paint = Geometry.ToPaintGeometry();

	const FVector2D Centre(Size.X * 0.5, Size.Y * 0.5);

	// ── Station chevrons, drawn before the fade gate ─────────────────────────
	//
	// <strong>These do not fade while the camera is swung, and the reticle does.</strong> The fade
	// exists because mid-orbit the view points somewhere the pawn has no opinion about, so a
	// velocity marker there is lying. A station is somewhere whatever the camera is doing, and
	// swinging the camera to look for one is exactly when it must not disappear.
	{
		const TSharedRef<FSlateFontMeasure> Measure =
			FSlateApplication::Get().GetRenderer()->GetFontMeasureService();

		for (const FStationMark& Mark : StationMarks)
		{
			const FVector2D Apex = Centre + Mark.Offset;

			Chevron(
				Elements, Base, Paint, Apex, Mark.Offset.GetSafeNormal(),
				StationChevronSize, StationColour, OutlineColour, Thickness);

			// Under the chevron and centred on it. Measured rather than nudged by a constant: the
			// names are content and "Deepdock" and "Capital Trading Hub" are not the same width.
			double Line = StationChevronSize + 4.0;

			for (const FString& Label : { Mark.Name, Mark.Distance })
			{
				if (Label.IsEmpty())
				{
					continue;
				}

				const FVector2D Extent = Measure->Measure(Label, StationFont);

				FSlateDrawElement::MakeText(
					Elements,
					Base + 1,
					// Size first, then where to put it. FGeometry::ToPaintGeometry takes a local size
					// and a layout transform, not a rectangle -- passing a position as the first
					// argument compiles against the wrong overload and draws text at the origin.
					Geometry.ToPaintGeometry(
						FVector2D(Extent.X, Extent.Y),
						FSlateLayoutTransform(
							FVector2D(Apex.X - (Extent.X * 0.5), Apex.Y + Line))),
					Label,
					StationFont,
					ESlateDrawEffect::None,
					StationColour);

				Line += Extent.Y;
			}
		}
	}

	// The reticle and the velocity ring, which do fade.
	if (Opacity <= 0.01f)
	{
		return Base + 2;
	}

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
