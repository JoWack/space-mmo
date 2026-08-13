#include "SpaceMMOHudPlacement.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Widget.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"

namespace SpaceMMO::Hud
{
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

		// Every component, not only the colliding ones — the second argument of
		// GetComponentsBoundingBox, which GetActorBounds inverts (Actor.cpp:5398).
		//
		// This is a label over something a player can see, so what matters is where the thing looks
		// like it is, not where it can be bumped into. Asking for colliding components only put the
		// deposit prompt at the world origin: a deposit's marker mesh is deliberately NoCollision
		// (SpaceMMODepositActor.cpp:40), so nothing qualified, and an FBox(ForceInit) that nothing
		// expands is a zero box at the origin (Actor.cpp:2267) rather than an error. With render
		// origin rebasing that projects somewhere arbitrary and usually still on screen, which reads
		// as a label with a mysterious offset rather than as one pointing at nothing.
		Actor->GetActorBounds(false, Origin, Extent);

		const FVector Above = Origin + Actor->GetActorUpVector() * Extent.Size() * HeightScale;

		return UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(
			Controller, Above, OutPosition, false);
	}

	void PlaceAt(UWidget* Widget, const FVector2D& Position)
	{
		// Not named Slot: UWidget already has a member by that name, and shadowing it is a warning
		// this project treats as an error.
		UCanvasPanelSlot* RootSlot = Widget != nullptr
			? Cast<UCanvasPanelSlot>(Widget->Slot)
			: nullptr;

		if (RootSlot == nullptr)
		{
			return;
		}

		// Bottom centre, so the label sits above the point rather than on it, and grows upwards.
		RootSlot->SetAlignment(FVector2D(0.5, 1.0));
		RootSlot->SetPosition(Position);
	}
}
