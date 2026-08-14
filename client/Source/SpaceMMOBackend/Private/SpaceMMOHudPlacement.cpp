#include "SpaceMMOHudPlacement.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PrimitiveComponent.h"
#include "Components/Widget.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"

namespace SpaceMMO::Hud
{
	void VisibleBounds(const AActor* Actor, FVector& Origin, FVector& Extent)
	{
		// An actor with nothing drawable is measured at its own location rather than at the world
		// origin, so a label on one is merely in a dull place instead of somewhere meaningless.
		Origin = Actor != nullptr ? Actor->GetActorLocation() : FVector::ZeroVector;
		Extent = FVector::ZeroVector;

		if (Actor == nullptr)
		{
			return;
		}

		// Bounds are gathered here rather than through GetActorBounds, which offers no way to
		// exclude what has to be excluded. Two things had to be got right and neither is the
		// default:
		//
		// <strong>Non-colliding components count.</strong> This is a label over something a player
		// looks at, so it wants where the thing appears, not where it can be bumped into — and the
		// meshes here are deliberately NoCollision (SpaceMMODepositActor.cpp:40,
		// SpaceMMOCharacterPawn.cpp:38). Asking for colliding components only returns an
		// FBox(ForceInit) that nothing expands: a zero box at the world origin (Actor.cpp:2267),
		// not an error, which under render-origin rebasing projects somewhere plausible and reads
		// as a mysterious offset.
		//
		// <strong>Editor-only components do not count.</strong> Every UCameraComponent registers a
		// DrawFrustumComponent and a CameraProxyMeshComponent (CameraComponent.cpp:168,182), and a
		// frustum is a 10 m box. Both pawns carry two cameras, so four of these swamped a 0.4 x
		// 0.9 m character and put its bounding radius at 19 m — which floated its messages 19 m
		// into the air and off the top of the screen. Deposits have no camera, which is why the
		// same code worked there and made the fault look widget-specific.
		//
		// Tested with IsEditorOnly rather than IsVisualizationComponent, which is the more obvious
		// name and does not compile for the dedicated server: it and its flag live inside
		// WITH_EDITORONLY_DATA (ActorComponent.h:346). IsEditorOnly is unguarded
		// (ActorComponent.h:708) and answers the same question here, because
		// SetIsVisualizationComponent sets bIsEditorOnly as well — which is what both of the
		// camera's components are created with. In a server build they are never created at all, so
		// the filter costs nothing there.
		FBox Box(ForceInit);

		Actor->ForEachComponent<UPrimitiveComponent>(false,
			[&Box](const UPrimitiveComponent* Primitive)
			{
				if (Primitive->IsRegistered() && !Primitive->IsEditorOnly())
				{
					Box += Primitive->Bounds.GetBox();
				}
			});

		if (Box.IsValid)
		{
			Box.GetCenterAndExtents(Origin, Extent);
		}
	}

	bool ProjectAbove(
		const APlayerController* Controller,
		const AActor* Actor,
		const float HeightScale,
		FVector2D& OutPosition)
	{
		if (Controller == nullptr || Actor == nullptr)
		{
			return false;
		}

		FVector Origin;
		FVector Extent;

		VisibleBounds(Actor, Origin, Extent);

		const FVector Above = Origin + Actor->GetActorUpVector() * Extent.Size() * HeightScale;

		return UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(
			Controller, Above, OutPosition, false);
	}

	bool PlaceAt(UWidget* Widget, const FVector2D& Position)
	{
		// Not named Slot: UWidget already has a member by that name, and shadowing it is a warning
		// this project treats as an error.
		UCanvasPanelSlot* RootSlot = Widget != nullptr
			? Cast<UCanvasPanelSlot>(Widget->Slot)
			: nullptr;

		if (RootSlot == nullptr)
		{
			return false;
		}

		// Bottom centre, so the label sits above the point rather than on it, and grows upwards.
		RootSlot->SetAlignment(FVector2D(0.5, 1.0));
		RootSlot->SetPosition(Position);

		return true;
	}
}
