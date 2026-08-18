#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "SpaceMMOBackendTypes.h"

#include "SpaceMMOLoginScreen.generated.h"

/**
 * Signing in, from the game.
 *
 * <strong>An overlay rather than a menu level.</strong> It sits on top of whatever the world is
 * doing and hides once there is a session, which is a fraction of the work of a separate map and
 * leaves task 110 free to build the real menu without unpicking this first.
 *
 * <strong>The credentials file still wins.</strong> A machine with <c>secrets/player-login.txt</c>,
 * or a <c>-BackendEmail=</c> on the command line, never sees this screen: those exist so that two
 * clients on one desktop can be two different players, and making a test run type a password would
 * cost that outright.
 *
 * <strong>It does not set its own visibility</strong>; see
 * <c>ASpaceMMOPlayerController::UpdateHudContext</c>, and the note on
 * <c>USpaceMMOFlightReadout::NativeTick</c> for why a widget cannot.
 */
UCLASS()
class SPACEMMOBACKEND_API USpaceMMOLoginScreen : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * Signs in with what was typed.
	 *
	 * Trimmed, because an address copied out of an email client arrives with a space on it often
	 * enough to matter, and the failure it produces is a 401 indistinguishable from a wrong
	 * password — which is the same class of thing that made a command-line address arrive as
	 * "joe@gmail .com" and cost a debugging round.
	 */
	UFUNCTION(BlueprintCallable, Category = "SpaceMMO|Identity")
	void SignIn(const FString& Email, const FString& Password, bool bRemember);

	/**
	 * Words a failure for the person typing. Pure, static and tested without a widget or a server.
	 *
	 * A 401 here has exactly one meaning and "Unauthorized" is not it. Everything else is shown as
	 * the backend said it, because a server that is not running and a wrong password produce the
	 * same blank stare otherwise — and on this project the API refusing to start over an unapplied
	 * migration has already once read as "cannot identify my character".
	 */
	static FString DescribeFailure(const FBackendFailure& Failure);

	/** Why the last attempt failed, or empty. Bind a message line to this. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Identity")
	FString FailureText;

	/**
	 * Whether a sign-in is in flight. Bind the button's enabled state to the opposite.
	 *
	 * A request takes long enough to press the button twice, and two sign-ins racing produce two
	 * sessions where the second silently replaces the first.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Identity")
	bool bSigningIn = false;

	/** Whether to remember the session. Bind a tick box to this; on by default. */
	UPROPERTY(BlueprintReadWrite, Category = "SpaceMMO|Identity")
	bool bRememberMe = true;

protected:
	virtual void NativeConstruct() override;

	virtual void NativeDestruct() override;

	/** The email box, so a restored address can be put back in it and focused. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UEditableTextBox> EmailBox;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UEditableTextBox> PasswordBox;

private:
	UFUNCTION()
	void HandleSessionChanged(bool bIsSignedIn);

	UFUNCTION()
	void HandleFailed(const FBackendFailure& Failure);

	class USpaceMMOBackendClient* Backend() const;
};
