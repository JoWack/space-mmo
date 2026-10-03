#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "SpaceMMOBackendTypes.h"

#include "SpaceMMOPairedPanel.generated.h"

/**
 * A screen that shares the viewport when another one is open.
 *
 * The station overlay and the inventory screen are the pair that has a reason to be seen together —
 * goods move between a hangar and a hold, and reading one while the other is hidden is exactly the
 * thing that makes hauling feel like paperwork. Open together they take a side each; alone, each
 * returns to the middle.
 *
 * <strong>C++ decides which side; the Blueprint decides what a side looks like.</strong> This class
 * owns the anchor, alignment and position of <c>PanelRoot</c> and nothing else — its size, border,
 * background and contents stay the designer's. The offset is editable because how far apart two
 * panels should sit is taste rather than fact.
 *
 * A base class rather than the same code twice: two panels sliding by slightly different arithmetic
 * would be a bug nobody notices until they are open together, which is the only moment either
 * matters.
 */
UCLASS(Abstract)
class SPACEMMOBACKEND_API USpaceMMOPairedPanel : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Where this panel should sit. Set by the controller, which knows what else is open. */
	void SetSide(ESpaceMMOPanelSide NewSide);

	ESpaceMMOPanelSide GetSide() const { return Side; }

	/** Bind a header's wording or a tab strip's width to this if the paired look wants to differ. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	ESpaceMMOPanelSide Side = ESpaceMMOPanelSide::Centre;

	/** Hidden or shown by the controller; the Blueprint's key events follow it, see SyncPromptKeys. */
	virtual void SetVisibility(ESlateVisibility InVisibility) override;

protected:
	virtual void NativeConstruct() override;

	virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;

	/** Whether this panel's Blueprint prompt is waiting for an answer. */
	virtual bool IsPromptOpen() const { return false; }

	/**
	 * Gives the Blueprint's key events the keyboard only while its prompt is up and the panel shows.
	 *
	 * <strong>Why this exists (1 October).</strong> Both paired panels have Enter and Escape key
	 * events in their graphs, for their quantity and order prompts. A widget registers those on
	 * construction and keeps them while collapsed, above the player controller -- so the inventory
	 * and station screens held Escape for the whole game, open or not, and the Esc menu never heard
	 * it. Found by logging the input stack when Escape arrived. Pushing and popping here keeps
	 * Escape cancelling a prompt that is up and frees it the rest of the time; Enter, the same.
	 */
	void SyncPromptKeys();

	/**
	 * The thing that moves.
	 *
	 * Must be a direct child of a Canvas Panel, since its canvas slot is what carries the position.
	 * Give it a fixed size rather than Size To Content: a panel that resizes as rows arrive would
	 * jump about while an industry job counts down.
	 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UWidget> PanelRoot;

	/**
	 * The light dim over the world behind a panel (task 173; Joe chose a clear world, lightly dimmed, over
	 * the menus' blur). Shown by one panel at a time: two open together would dim the world twice, so the
	 * panel on the right leaves it to the one on the left.
	 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UWidget> WorldDim;

	/**
	 * How far from centre a side sits, as a fraction of viewport width.
	 *
	 * A fraction rather than pixels so the pairing holds at any resolution — the gap between two
	 * panels is a proportion of the screen, not a number of dots.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SpaceMMO|HUD")
	float SideOffset = 0.26f;

	/** How long the slide takes. Zero snaps. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SpaceMMO|HUD")
	float SlideSeconds = 0.15f;

private:
	/** Where the panel is now, in widget-space pixels from the centre of the viewport. */
	float CurrentOffset = 0.0f;

	/** Whether CurrentOffset has been placed at all, so the first frame does not slide in. */
	bool bPlaced = false;

	/** Whether the Blueprint's key events are on the controller's input stack right now. */
	bool bPromptKeysRegistered = true;
};
