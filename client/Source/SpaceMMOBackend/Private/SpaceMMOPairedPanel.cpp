#include "SpaceMMOPairedPanel.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Widget.h"

void USpaceMMOPairedPanel::SetSide(const ESpaceMMOPanelSide NewSide)
{
	Side = NewSide;
}

void USpaceMMOPairedPanel::NativeTick(const FGeometry& Geometry, const float DeltaSeconds)
{
	Super::NativeTick(Geometry, DeltaSeconds);

	// Not named Slot: UWidget already has a member by that name, and shadowing it is a warning this
	// project treats as an error.
	UCanvasPanelSlot* RootSlot = PanelRoot != nullptr
		? Cast<UCanvasPanelSlot>(PanelRoot->Slot)
		: nullptr;

	if (RootSlot == nullptr)
	{
		return;
	}

	const FVector2D Viewport = UWidgetLayoutLibrary::GetViewportSize(this)
		/ FMath::Max(UWidgetLayoutLibrary::GetViewportScale(this), UE_KINDA_SMALL_NUMBER);

	const float Target =
		Side == ESpaceMMOPanelSide::Left ? -SideOffset * Viewport.X
		: Side == ESpaceMMOPanelSide::Right ? SideOffset * Viewport.X
		: 0.0f;

	if (!bPlaced)
	{
		// The first frame lands where it belongs rather than sliding in from the middle, so opening
		// a panel into an already-paired layout does not look like it arrived from somewhere.
		bPlaced = true;
		CurrentOffset = Target;
	}
	else if (SlideSeconds > 0.0f)
	{
		// Framerate-independent, so the slide takes the same time on any machine. Interpolated
		// rather than driven by a curve because the whole motion is a sixth of a second and what
		// matters is that the two panels read as one workspace rearranging rather than teleporting.
		CurrentOffset = FMath::FInterpTo(
			CurrentOffset, Target, DeltaSeconds, 1.0f / FMath::Max(SlideSeconds, KINDA_SMALL_NUMBER));
	}
	else
	{
		CurrentOffset = Target;
	}

	// Anchor and alignment are set here rather than trusted from the Blueprint so the arithmetic
	// above means one thing: an anchor at the centre with the panel aligned about its own middle,
	// so the position is a pure offset from the centre of the screen. The panel's *size* is
	// untouched — that stays in the canvas slot's offsets, where the designer put it.
	RootSlot->SetAnchors(FAnchors(0.5f, 0.5f));
	RootSlot->SetAlignment(FVector2D(0.5, 0.5));
	RootSlot->SetPosition(FVector2D(CurrentOffset, 0.0));
}
