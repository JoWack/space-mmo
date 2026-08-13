#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "SpaceMMOBackendTypes.h"

#include "SpaceMMOTransientMessages.generated.h"

/** One message, worded, with its tone and the moment it stops being shown. */
USTRUCT(BlueprintType)
struct SPACEMMOBACKEND_API FSpaceMMOTransientMessage
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	FString Text;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	ESpaceMMOMessageTone Tone = ESpaceMMOMessageTone::Positive;

	/** World seconds. Held here rather than as a countdown so nothing depends on a tick rate. */
	double ExpiresAt = 0.0;
};

/** One message's row. Its own widget so the Blueprint owns what a message looks like. */
UCLASS()
class SPACEMMOBACKEND_API USpaceMMOTransientMessageRow : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Fills the row in. Called by the stack; nothing else needs it. */
	void SetMessage(const FSpaceMMOTransientMessage& Message);

	/** Bind colour to this. True for a yield, false for anything the player did not get. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	bool bIsPositive = true;

protected:
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> MessageText;
};

/**
 * Transient messages, floating above the pawn.
 *
 * Above the pawn rather than in a corner, because a yield or a refusal belongs where the player is
 * already looking. This replaces `ShowTransientLine`, which had to be folded into the debug panel to
 * get a stable order at all — on-screen debug messages are ordered by slot rather than key, and the
 * panel is redrawn every frame at zero display time, so a separate three-second message landed
 * wherever the free list put it, usually below dozens of panel lines and off the bottom of the
 * screen. The cost of that fix was colour, and this is what gets it back.
 *
 * <strong>Not shown or hidden by UpdateHudContext.</strong> Unlike the other four widgets this one
 * is always visible and always ticking, because it has to expire its own messages and because it
 * belongs to both contexts. It draws nothing when it holds nothing.
 */
UCLASS()
class SPACEMMOBACKEND_API USpaceMMOTransientMessages : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Adds a message, dropping the oldest if the stack is already full. */
	void Push(const FString& Text, ESpaceMMOMessageTone Tone);

	/**
	 * How many are kept at once.
	 *
	 * Three rather than one, because mining produces messages faster than they can be read: a yield
	 * followed immediately by "give it a moment" would otherwise erase the yield being looked for.
	 */
	static constexpr int32 MaxMessages = 3;

	/** How long each is shown for, in seconds. */
	static constexpr double SecondsShown = 3.0;

protected:
	virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;

	/**
	 * Fired when a message is added, so a Widget Animation can fade or rise it.
	 *
	 * C++ decides what is said and for how long; the Blueprint decides how it arrives and leaves.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "SpaceMMO|HUD")
	void OnMessageAdded();

	/**
	 * The thing that gets moved to follow the pawn.
	 *
	 * Must sit in a Canvas Panel, because its canvas slot is what carries the position. Everything
	 * inside it — layout, spacing, alignment — is the Blueprint's.
	 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UWidget> MessageRoot;

	/** The container rows are added to. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UPanelWidget> MessageRows;

	/** What one message looks like. Set this in the Widget Blueprint's class defaults. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SpaceMMO|HUD")
	TSubclassOf<USpaceMMOTransientMessageRow> RowClass;

	/**
	 * How far above the pawn to float, as a multiple of the pawn's own bounding radius.
	 *
	 * Scaled by the pawn rather than a fixed distance, so a ship and a character both get a sensible
	 * height with no per-pawn tuning — and left editable because how high is right is a matter of
	 * taste rather than a fact.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SpaceMMO|HUD")
	float HeightScale = 1.0f;

private:
	/** Drops everything past its expiry. Returns whether anything went. */
	bool ExpireOldMessages(double NowSeconds);

	/** Rebuilds the rows from Messages. */
	void RebuildRows();

	/** Puts MessageRoot over the pawn, or below the reticle when the pawn cannot be projected. */
	void FollowPawn();

	TArray<FSpaceMMOTransientMessage> Messages;

	/** So a missing MessageRows or RowClass is said once rather than sixty times a second. */
	bool bWarnedAboutWiring = false;
};
