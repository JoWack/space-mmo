#pragma once

#include "CoreMinimal.h"
#include "SpaceMMOBackendTypes.h"
#include "SpaceMMOPanelRow.h"

#include "SpaceMMOStationWork.generated.h"

class USpaceMMOStationOverlay;
class UButton;
class UHorizontalBox;
class UProgressBar;
class UTextBlock;

/** One input of the selected recipe: what it takes for the chosen count, and what this station holds. */
struct SPACEMMOBACKEND_API FSpaceMMORecipeInputText
{
	FString Name;
	int32 Held = 0;
	int32 Needed = 0;

	/**
	 * Whether this is the recipe's main input, whose style the output takes (task 187). Its count is of
	 * the picked style only, because the whole of it comes from one style.
	 */
	bool bMain = false;

	/** The main input's style: the picked one, or a raw material's world. Common otherwise, and untagged. */
	EBackendItemStyle Style = EBackendItemStyle::Common;

	/** Whether more than one style of the main input is held here, so the line offers ◂ ▸. */
	bool bCanPick = false;

	bool IsShort() const { return Held < Needed; }
};

/** One recipe's row, already worded. */
struct SPACEMMOBACKEND_API FSpaceMMORecipeRowText
{
	/** Its place in the catalogue, which is what selecting it names. */
	int32 Index = 0;

	/** "Ferrite Plate ×2": one run's output. */
	FString Title;

	/** "40 s": one run's time. */
	FString Time;

	/** "refining lv 2". */
	FString Skill;

	bool bSelected = false;

	/** The selected recipe's inputs for the chosen count. Empty on every other row. */
	TArray<FSpaceMMORecipeInputText> Inputs;

	/** "tool: Crude Mining Laser", or empty. Selected row only. */
	FString Tool;
};

/** One job's row. */
struct SPACEMMOBACKEND_API FSpaceMMOJobRowText
{
	int64 JobId = 0;

	/** "Ferrite Plate ×6": everything the job makes. */
	FString Title;

	/**
	 * The title in two parts, "Ferrite Plate" and "×6", so the style tag goes between them as Joe
	 * approved on 10 October: "Crude Thruster [TER] ×1" (task 187).
	 */
	FString Name;
	FString Count;

	/** The style the output will have. */
	EBackendItemStyle Style = EBackendItemStyle::Common;

	bool bReady = false;

	/** "1 m 20 s" while it runs; empty when ready. */
	FString Remaining;

	/** How far along, 0 to 1, from its recipe's time and runs. One when it cannot be known. */
	float Progress = 1.0f;
};

enum class ESpaceMMOQuestRowKind : uint8
{
	Active,
	Ready,
	Offered,
};

/** One quest's row: held, finished, or on offer. */
struct SPACEMMOBACKEND_API FSpaceMMOQuestRowText
{
	FString QuestKey;
	FString Name;
	ESpaceMMOQuestRowKind Kind = ESpaceMMOQuestRowKind::Active;

	/** "3/5" while active; empty otherwise. */
	FString Progress;

	float Fraction = 0.0f;

	/** The current step, or for an offer the first one. */
	FString Description;

	/** "750.00 cr", or empty when it pays nothing. */
	FString Reward;
};

/** The bar under the recipes: how many, what that comes to, and the start button's words. */
struct SPACEMMOBACKEND_API FSpaceMMOStartBarText
{
	/** False only with no recipe to start. Being short of inputs does not turn it off; see BuildStartBar. */
	bool bHasRecipe = false;

	int32 Runs = 1;

	/** The most this station's hangar can supply, or zero when it is short of something for one. */
	int32 MostRuns = 0;

	/**
	 * "most you can make: 3 · 2 m 0 s", or what it is short of: the inputs by name when one or two are,
	 * otherwise how many -- the recipe row above names each one in red.
	 */
	FString Note;

	/** Whether the note is a shortfall, for colouring it. */
	bool bShort = false;

	/** "Start Ferrite Plate [TER] ×6". */
	FString Label;

	/**
	 * Which style of the main input Start makes it from (task 187): the pick if it is held here, otherwise
	 * the style held most of. Unset when the recipe has no main input or none of a crafted one is held,
	 * which leaves the choice to the server.
	 */
	TOptional<EBackendItemStyle> MainInputStyle;

	/** The style the output will have, for the label's tag. */
	EBackendItemStyle OutputStyle = EBackendItemStyle::Common;
};

/**
 * The station's Industry and Quests tabs as rows, worked with the mouse (task 173, Joe 3 October:
 * "I'd prefer to just click/select and click buttons"). They were lines of text driven by R, X, Z and J.
 *
 * Pure and static, like every panel builder here, so the wording and the arithmetic are tested
 * without a backend -- those tests are the only automated coverage the panels' words have.
 *
 * <strong>Quantities, never verdicts.</strong> The rows show what a recipe takes against what is held,
 * and the count is capped by it, but nothing here decides a job cannot be started: the skill, tool,
 * material and fee gates are the server's, and a second copy would be free to disagree with them.
 * Start stays pressable when short, and the server's answer is what the player reads.
 */
struct SPACEMMOBACKEND_API FSpaceMMOStationWork
{
	/** The most runs the count goes to when nothing limits it -- a recipe with no inputs. */
	static constexpr int32 MaxRuns = 99;

	/** "40 s", "1 m 20 s", "1 h 5 m". */
	static FString Duration(int32 Seconds);

	/**
	 * What this station's hangar holds of an item. Only that hangar: a job takes its inputs from the
	 * hangar where it is started, so ore in the ship's hold or another station's hangar is not "held"
	 * for this purpose, and counting it offered numbers the server would then refuse.
	 */
	static int32 HeldHere(const TArray<FBackendInventoryItem>& Inventory, const FString& ItemKey, int32 StationId);

	/** What this station's hangar holds of an item in one style. */
	static int32 HeldHere(
		const TArray<FBackendInventoryItem>& Inventory, const FString& ItemKey, int32 StationId, EBackendItemStyle Style);

	/** The styles of an item this station's hangar holds any of, in style order. */
	static TArray<EBackendItemStyle> StylesHeldHere(
		const TArray<FBackendInventoryItem>& Inventory, const FString& ItemKey, int32 StationId);

	/**
	 * Which style of a recipe's main input a job here would be made from (task 187), as the server
	 * decides it: a raw material's world; else the pick, if any of it is held here; else the style held
	 * most of, ties in style order. Unset for a recipe without a main input, or a crafted one none of
	 * which is held.
	 */
	static TOptional<EBackendItemStyle> MainInputStyle(
		const FBackendRecipe& Recipe,
		const TArray<FBackendInventoryItem>& Inventory,
		int32 StationId,
		TOptional<EBackendItemStyle> Picked);

	/** The next style along from the current one among those held, wrapping: what ◂ (-1) and ▸ (+1) pick. */
	static TOptional<EBackendItemStyle> StepStyle(
		const TArray<EBackendItemStyle>& Held, TOptional<EBackendItemStyle> Current, int32 Direction);

	/**
	 * How many runs of a recipe the hangar here can supply: the scarcest input decides. The main input
	 * counts in its one style only, since a job never makes it up from two.
	 */
	static int32 MostRuns(
		const FBackendRecipe& Recipe,
		const TArray<FBackendInventoryItem>& Inventory,
		int32 StationId,
		TOptional<EBackendItemStyle> Picked = TOptional<EBackendItemStyle>());

	/** The selection, kept inside the catalogue. INDEX_NONE when it is empty. */
	static int32 ClampSelection(int32 Selected, int32 RecipeCount);

	static TArray<FSpaceMMORecipeRowText> BuildRecipeRows(
		const TArray<FBackendRecipe>& Recipes,
		const TArray<FBackendInventoryItem>& Inventory,
		int32 StationId,
		int32 Selected,
		int32 Runs,
		TOptional<EBackendItemStyle> PickedMainStyle = TOptional<EBackendItemStyle>());

	static FSpaceMMOStartBarText BuildStartBar(
		const TArray<FBackendRecipe>& Recipes,
		const TArray<FBackendInventoryItem>& Inventory,
		int32 StationId,
		int32 Selected,
		int32 Runs,
		TOptional<EBackendItemStyle> PickedMainStyle = TOptional<EBackendItemStyle>());

	static TArray<FSpaceMMOJobRowText> BuildJobRows(
		const TArray<FBackendIndustryJob>& Jobs, const TArray<FBackendRecipe>& Recipes);

	/** Held quests first, finished ones before running ones, then everything on offer. History is dropped. */
	static TArray<FSpaceMMOQuestRowText> BuildQuestRows(
		const TArray<FBackendJournalEntry>& Journal, const TArray<FBackendAvailableQuest>& Available);
};

/** A recipe. Clicking it selects it. */
UCLASS()
class SPACEMMOBACKEND_API USpaceMMORecipeRow : public USpaceMMOPanelRow
{
	GENERATED_BODY()

public:
	void SetRow(const FSpaceMMORecipeRowText& Row);

	void SetOwningOverlay(USpaceMMOStationOverlay* Overlay);

protected:
	virtual SpaceMMO::Style::ERowLook Look() const override;
	virtual void StyleTexts(SpaceMMO::Style::ERowLook InLook) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;

	/** The main input's ◂ and ▸ (task 187). */
	UFUNCTION()
	void PickPreviousStyle();

	UFUNCTION()
	void PickNextStyle();

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TimeText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SkillText;

	/** Filled with one text per input, so a short one can be red on its own. A wrap box, so they never run out of the row. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UPanelWidget> InputsBox;

private:
	FSpaceMMORecipeRowText Row;

	TWeakObjectPtr<USpaceMMOStationOverlay> OwningOverlay;
};

/** A job, with Claim once it is ready. */
UCLASS()
class SPACEMMOBACKEND_API USpaceMMOJobRow : public USpaceMMOPanelRow
{
	GENERATED_BODY()

public:
	void SetRow(const FSpaceMMOJobRowText& Row);

	void SetOwningOverlay(USpaceMMOStationOverlay* Overlay);

protected:
	virtual void NativeConstruct() override;
	virtual void StyleTexts(SpaceMMO::Style::ERowLook InLook) override;

	UFUNCTION()
	void Claim();

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StatusText;

	/** The output's style tag and the count after it (task 187). Built by SpaceMMOBuildMenus. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StyleTagText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CountText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> ClaimButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ClaimText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> ProgressBar;

private:
	FSpaceMMOJobRowText Row;

	TWeakObjectPtr<USpaceMMOStationOverlay> OwningOverlay;
};

/** A quest: Hand in when finished, Accept when offered, progress while running. */
UCLASS()
class SPACEMMOBACKEND_API USpaceMMOQuestRow : public USpaceMMOPanelRow
{
	GENERATED_BODY()

public:
	void SetRow(const FSpaceMMOQuestRowText& Row);

	void SetOwningOverlay(USpaceMMOStationOverlay* Overlay);

protected:
	virtual void NativeConstruct() override;
	virtual void StyleTexts(SpaceMMO::Style::ERowLook InLook) override;

	UFUNCTION()
	void Act();

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> NameText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StatusText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> ActionButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ActionText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DescriptionText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> RewardText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> ProgressBar;

private:
	FSpaceMMOQuestRowText Row;

	TWeakObjectPtr<USpaceMMOStationOverlay> OwningOverlay;
};
