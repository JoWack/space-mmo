#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "SpaceMMOStyle.h"

#include "SpaceMMOPanelRow.generated.h"

class UBorder;
class UProgressBar;
class UTextBlock;

/**
 * A row in one of the game's panels, styled from SpaceMMO::Style by what state it is in (task 173).
 *
 * <strong>Why the row styles itself.</strong> Each row used to colour itself through Blueprint bindings --
 * a Select between two literal colours -- and a binding can choose a colour but not an outline. The look
 * Joe approved on 2 October draws a selected row with an ice outline and a heading with no box at all,
 * which is a different brush rather than a different tint. So the row decides its look from the flags it
 * already had, and the Blueprint keeps everything else: what is in a row and where.
 *
 * Applied when the look changes, not every frame: a market is a few dozen rows, and resetting a brush
 * re-lays a widget out.
 */
UCLASS(Abstract)
class SPACEMMOBACKEND_API USpaceMMOPanelRow : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Makes the next tick restyle the row: called after its content changes, which can change its state. */
	void RequestRestyle() { bRestyle = true; }

protected:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;
	virtual void NativeOnMouseEnter(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual void NativeOnMouseLeave(const FPointerEvent& Event) override;

	/** What this row looks like now. Rows with more states than hover override this. */
	virtual SpaceMMO::Style::ERowLook Look() const;

	/** Styles the row's texts for its state. Called with the look, whenever either changes. */
	virtual void StyleTexts(SpaceMMO::Style::ERowLook InLook) {}

	/** The row's box. Optional: a row without one is plain text. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UBorder> RowFrame;

	bool bHovered = false;

private:
	void ApplyLook();

	TOptional<SpaceMMO::Style::ERowLook> AppliedLook;

	bool bRestyle = true;
};

namespace SpaceMMO::PanelLook
{
	/** Sets a text's size, spacing, case and colour for what it is. A colour may be given to override. */
	SPACEMMOBACKEND_API void Apply(
		UTextBlock* Text, Style::ETextRole Role, TOptional<FLinearColor> Colour = TOptional<FLinearColor>());

	/** A figure column: right-aligned at a fixed width, so a list's numbers line up under their heading. */
	SPACEMMOBACKEND_API void ApplyFigure(UTextBlock* Text, Style::ETextRole Role, float Width = 140.0f);

	/** A thin progress bar: an ice fill on a faint track. Skills, jobs and quests all draw the same one. */
	SPACEMMOBACKEND_API void ApplyBar(UProgressBar* Bar);
}
