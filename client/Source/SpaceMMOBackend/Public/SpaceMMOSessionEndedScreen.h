#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "SpaceMMOBackendTypes.h"

#include "SpaceMMOSessionEndedScreen.generated.h"

/**
 * Why this window's session is over (task 199): the account signed in somewhere else, or the server was lost.
 *
 * <para>
 * Shown by the controller on the map a disconnected client falls back to, in place of the HUD and the sign-in that
 * map would otherwise run. That sign-in is the fault this replaces: a client without its server became an offline
 * game of its own, claiming the character as its own server.
 * </para>
 * <para>
 * The death panel's shape, over a dark screen, as Joe approved it on 18 October. Quit is the only way on: a
 * Reconnect would bump the other window straight back. Built by the SpaceMMOBuildMenus commandlet as
 * WBP_SessionEnded.
 * </para>
 */
UCLASS()
class SPACEMMOBACKEND_API USpaceMMOSessionEndedScreen : public UUserWidget
{
	GENERATED_BODY()

public:
	/** The panel's title for a reason. */
	static FString TitleFor(ESpaceMMOSessionEnd Reason);

	/** And the line under it. */
	static FString NoteFor(ESpaceMMOSessionEnd Reason);

	/** Says why. */
	void Show(ESpaceMMOSessionEnd Reason);

protected:
	virtual void NativeConstruct() override;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> TitleText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> NoteText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UButton> QuitButton;

private:
	UFUNCTION()
	void HandleQuitClicked();
};
