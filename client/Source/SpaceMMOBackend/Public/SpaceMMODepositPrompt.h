#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "SpaceMMOBackendTypes.h"

#include "SpaceMMODepositPrompt.generated.h"

/**
 * What the rock in front of you is, and whether you can work it.
 *
 * Split into a value and a blocker per requirement rather than one sentence, so the Blueprint can
 * colour the blockers and leave everything else alone. That colouring is the reason this is a widget
 * at all: as debug text, "you are lv 1" carried exactly the same weight as every other line, so the
 * one thing stopping you read like the things that were not.
 */
USTRUCT(BlueprintType)
struct SPACEMMOBACKEND_API FSpaceMMODepositPromptText
{
	GENERATED_BODY()

	/**
	 * Whether there is a deposit to show — in reach <em>and</em> on screen.
	 *
	 * The second half matters because the prompt floats over the rock itself rather than sitting at
	 * a fixed place: <c>FindDepositInRange</c> returns the nearest deposit within reach, which can
	 * easily be behind the player. A label pinned at the screen edge pointing at something out of
	 * view describes nothing, so the prompt hides instead — turning around brings it back.
	 *
	 * Everything below is empty when false.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	bool bHasDeposit = false;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	FString ItemName;

	/** The skill and the level it wants, as one phrase: "mining · lv 3". */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	FString Requirement;

	/** "you are lv 1", or empty once the level is met. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	FString LevelBlocker;

	/** "needs Crude Mining Laser", or empty for a deposit that wants no tool. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	FString Tool;

	/** "you have none", or empty when one is carried. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	FString ToolBlocker;

	/**
	 * The key that gathers, as bound — "E" today.
	 *
	 * Read from the input mappings rather than written into the Blueprint, because a hint that says
	 * E after somebody rebinds the action is worse than no hint: it is confidently wrong, and the
	 * player has no way to tell. Empty when the action is not bound at all.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	FString GatherKey;

	/**
	 * Whether the gather would actually be allowed.
	 *
	 * Drives dimming the key hint rather than removing it: a player who cannot mine this yet still
	 * needs to see what it is they are being stopped from doing.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	bool bCanGather = false;
};

/**
 * The deposit prompt, floating above the reticle while on foot.
 *
 * Above the reticle because it describes what you are looking at rather than what you are — which is
 * also why it collapses entirely with nothing in reach, rather than leaving a header hanging in the
 * middle of an empty field.
 *
 * <strong>It does not set its own visibility</strong>; see
 * <c>ASpaceMMOPlayerController::UpdateHudContext</c>.
 */
UCLASS()
class SPACEMMOBACKEND_API USpaceMMODepositPrompt : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * Words the prompt. Pure, static and tested without a widget, a world or a deposit.
	 *
	 * Asks the same questions the server asks, in the same way — a broken tool does not count as
	 * carried, because <c>GuardToolAsync</c> ignores condition zero, and a prompt that counted one
	 * would promise a gather the server then refuses.
	 */
	static FSpaceMMODepositPromptText Build(
		const FBackendResourceNode& Node,
		const TArray<FBackendSkill>& Skills,
		const TArray<FBackendItemInstance>& Instances,
		const FString& GatherKey);

	/** The key bound to the Gather action, or empty if it is unbound. */
	static FString FindGatherKey();

	/** Whether anything is in reach. Bind the root container's visibility to this. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	bool bHasDeposit = false;

	/** Whether the gather would be allowed, for dimming the key hint. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	bool bCanGather = false;

	/** Whether this deposit demands a tool, so the tool row can be hidden for ones that do not. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	bool bNeedsTool = false;

	/**
	 * Whether there is a level blocker to show.
	 *
	 * Bind the blocker row's visibility to this. An empty text block still occupies a full line of
	 * height in a vertical box, so a rock you can happily mine would otherwise show a blank gap
	 * where the reason you cannot would have been.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	bool bHasLevelBlocker = false;

	/** Whether there is a tool blocker to show. Same reasoning as bHasLevelBlocker. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	bool bHasToolBlocker = false;

protected:
	virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;

	/**
	 * The thing that gets moved to sit over the deposit.
	 *
	 * Must be a direct child of a Canvas Panel, because its canvas slot is what carries the
	 * position. Everything inside it is the Blueprint's.
	 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UWidget> PromptRoot;

	/**
	 * How far above the deposit to float, as a multiple of the deposit's own bounding radius.
	 *
	 * Editable because how high is right is taste rather than fact, and deposit meshes differ.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SpaceMMO|HUD")
	float HeightScale = 1.0f;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> ItemNameText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> RequirementText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> LevelBlockerText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> ToolText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> ToolBlockerText;

	/** Just the key, so the Blueprint can render it as "[E] gather" however it likes. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> GatherKeyText;
};
