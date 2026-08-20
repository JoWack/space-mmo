#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SpaceMMOWorldDocument.h"
#include "SpaceMMOPreviewMarker.generated.h"

class ASpaceMMOPreviewBody;
class UStaticMeshComponent;
class UTextRenderComponent;

/** What has happened to an entry since the file was read. */
enum class ESpaceMMOMarkerStatus : uint8
{
	Unchanged,

	Moved,

	/** Added in this session and not yet written. */
	Added,

	/** Marked for removal; the cut happens on write, not on the key press. */
	Removed,
};

/**
 * One authored thing, standing on the preview so it can be picked up and put somewhere else.
 *
 * <strong>The only thing being edited is a direction.</strong> Dragging gives a location, and a
 * location has three degrees of freedom where content has two — so the marker throws the third one
 * away every time it moves: the direction is taken from where the gizmo left it, and the marker is
 * then put back on the ground along that direction. Standing it wherever the mouse stopped would
 * let somebody carefully place a deposit ten metres in the air, write the file, and find it
 * somewhere else entirely in game, because the file cannot record what they did.
 *
 * The fields the Details panel edits are the rest of the entry. They are plain UPROPERTYs so the
 * ordinary Details panel can edit them, rather than a bespoke form that would have to be rebuilt
 * every time the content schema gains a field.
 */
UCLASS(NotPlaceable, Transient)
class SPACEMMOAUTHORING_API ASpaceMMOPreviewMarker : public AActor
{
	GENERATED_BODY()

public:
	ASpaceMMOPreviewMarker();

	/** Places the marker for an authored entry and remembers what it was read as. */
	void Setup(const FSpaceMMOAuthoredPlaceable& Entry, ASpaceMMOPreviewBody* InBody);

	/** The entry as it stands now, ready to be written. */
	FSpaceMMOAuthoredPlaceable ToPlaceable() const;

	/** The entry as it was read from the file. */
	const FSpaceMMOAuthoredPlaceable& GetOriginal() const { return Original; }

	ESpaceMMOMarkerStatus GetStatus() const;

	/** How far this has moved across the ground since it was read, in drawn kilometres. */
	double MovedKilometres() const;

	void SetAdded();

	void SetRemoved(bool bRemoved);

	bool IsRemoved() const { return bMarkedForRemoval; }

	/** True for an entry that was added in this session and has never been in the file. */
	bool IsAdded() const { return bAdded; }

	/** Puts the marker back on the ground along its current direction. */
	void SnapToSurface();

	virtual void Tick(float DeltaSeconds) override;

	/** Ticks in the level editor, so the label can keep facing whoever is looking at it. */
	virtual bool ShouldTickIfViewportsOnly() const override { return true; }

#if WITH_EDITOR
	virtual void PostEditMove(bool bFinished) override;
#endif

	/** The key as it appears in the file today, which is what finds the entry to rewrite. */
	UPROPERTY(VisibleAnywhere, Category = "SpaceMMO|Entry")
	FString OriginalKey;

	UPROPERTY(EditAnywhere, Category = "SpaceMMO|Entry")
	FString Key;

	UPROPERTY(VisibleAnywhere, Category = "SpaceMMO|Entry")
	FString Body;

	/** Where it lies from the body's centre. Derived from the gizmo; shown so it can be read off. */
	UPROPERTY(VisibleAnywhere, Category = "SpaceMMO|Entry")
	FVector Direction = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = "SpaceMMO|Deposit",
		meta = (EditCondition = "bIsDeposit", EditConditionHides))
	FString Item;

	UPROPERTY(EditAnywhere, Category = "SpaceMMO|Deposit",
		meta = (EditCondition = "bIsDeposit", EditConditionHides))
	FString Skill;

	/** Item key of a tool the character must hold, or empty for bare hands. */
	UPROPERTY(EditAnywhere, Category = "SpaceMMO|Deposit",
		meta = (EditCondition = "bIsDeposit", EditConditionHides))
	FString RequiredTool;

	UPROPERTY(EditAnywhere, Category = "SpaceMMO|Deposit",
		meta = (EditCondition = "bIsDeposit", EditConditionHides, ClampMin = "1"))
	int32 RequiredLevel = 1;

	UPROPERTY(EditAnywhere, Category = "SpaceMMO|Deposit",
		meta = (EditCondition = "bIsDeposit", EditConditionHides, ClampMin = "1"))
	int32 QuantityMax = 100;

	UPROPERTY(EditAnywhere, Category = "SpaceMMO|Deposit",
		meta = (EditCondition = "bIsDeposit", EditConditionHides, ClampMin = "1"))
	int32 RespawnSeconds = 600;

	UPROPERTY(EditAnywhere, Category = "SpaceMMO|Station",
		meta = (EditCondition = "bIsStation", EditConditionHides))
	FString Name;

	UPROPERTY(EditAnywhere, Category = "SpaceMMO|Station",
		meta = (EditCondition = "bIsStation", EditConditionHides))
	FString StationKind;

	UPROPERTY(EditAnywhere, Category = "SpaceMMO|Station",
		meta = (EditCondition = "bIsStation", EditConditionHides, ClampMin = "0.1"))
	double DockingRangeKilometres = 5.0;

	UPROPERTY()
	bool bIsDeposit = true;

	UPROPERTY()
	bool bIsStation = false;

private:
	/** Colours the label by status, so the viewport says the same thing the panel does. */
	void RefreshLabel();

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Pin;

	UPROPERTY()
	TObjectPtr<UTextRenderComponent> Label;

	UPROPERTY()
	TWeakObjectPtr<ASpaceMMOPreviewBody> PreviewBody;

	FSpaceMMOAuthoredPlaceable Original;

	FString SystemKey;

	bool bAdded = false;

	bool bMarkedForRemoval = false;
};
