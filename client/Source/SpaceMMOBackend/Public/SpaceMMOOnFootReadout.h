#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"

#include "SpaceMMOOnFootReadout.generated.h"

/**
 * Who you are and what you can afford, already worded.
 *
 * A struct of finished strings rather than values, so the wording is a pure function testable
 * without a widget, a world or a backend — the same arrangement as the flight readout and the panel
 * builders.
 */
USTRUCT(BlueprintType)
struct SPACEMMOBACKEND_API FSpaceMMOOnFootReadoutText
{
	GENERATED_BODY()

	/** The character's name, or a plain statement that there is not one yet. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	FString Name;

	/**
	 * The balance, with its unit.
	 *
	 * Empty until the character list has been read. An empty string draws nothing; a confident
	 * "0 cr" would be indistinguishable from being broke, which is the same reasoning the debug
	 * panel already used.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	FString Credits;

	/** Whether Credits means anything, so a row can be hidden rather than left blank. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	bool bHasCredits = false;
};

/**
 * The always-on identity block, shown while on foot.
 *
 * Shares the flight readout's corner: the two are never on screen together, because the controller
 * shows exactly one of them. <strong>It does not decide that itself</strong> — see
 * <c>ASpaceMMOPlayerController::UpdateHudContext</c>, and the note on
 * <c>USpaceMMOFlightReadout::NativeTick</c> for why a widget cannot.
 */
UCLASS()
class SPACEMMOBACKEND_API USpaceMMOOnFootReadout : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * Words the block. Pure, static and tested without a widget.
	 *
	 * @param CharacterName As the backend gave it, or empty before the character list has arrived.
	 * @param Balance       Already formatted by FBackendCharacter::FormatBalance, without a unit.
	 */
	static FSpaceMMOOnFootReadoutText Build(const FString& CharacterName, const FString& Balance);

	/** Whether there is a balance to show. Bind a row's visibility to this. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|HUD")
	bool bHasCredits = false;

protected:
	virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;

	/**
	 * Bound by name from the Widget Blueprint.
	 *
	 * Optional, so a Blueprint that omits one still compiles and runs: a HUD that refuses to appear
	 * because somebody deleted a row is worse than a HUD missing a row.
	 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> NameText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> CreditsText;
};
