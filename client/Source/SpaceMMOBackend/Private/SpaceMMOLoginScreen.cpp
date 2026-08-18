#include "SpaceMMOLoginScreen.h"

#include "Components/EditableTextBox.h"
#include "Engine/GameInstance.h"
#include "SpaceMMOBackendClient.h"
#include "SpaceMMOBackendLog.h"

void USpaceMMOLoginScreen::NativeConstruct()
{
	Super::NativeConstruct();

	USpaceMMOBackendClient* Client = Backend();

	if (Client == nullptr)
	{
		return;
	}

	Client->OnSessionChanged.AddDynamic(this, &USpaceMMOLoginScreen::HandleSessionChanged);
	Client->OnFailed.AddDynamic(this, &USpaceMMOLoginScreen::HandleFailed);
}

void USpaceMMOLoginScreen::NativeDestruct()
{
	if (USpaceMMOBackendClient* Client = Backend())
	{
		// Removed explicitly. The subsystem outlives this widget, and a delegate left pointing at a
		// destroyed one is a crash on the next failure rather than at the point of the mistake.
		Client->OnSessionChanged.RemoveDynamic(this, &USpaceMMOLoginScreen::HandleSessionChanged);
		Client->OnFailed.RemoveDynamic(this, &USpaceMMOLoginScreen::HandleFailed);
	}

	Super::NativeDestruct();
}

void USpaceMMOLoginScreen::SignIn(
	const FString& Email, const FString& Password, const bool bRemember)
{
	USpaceMMOBackendClient* Client = Backend();

	if (Client == nullptr || bSigningIn)
	{
		return;
	}

	// Trimmed here rather than trusted. An address pasted out of a mail client carries a space often
	// enough to matter, and what it produces is a 401 that reads exactly like a wrong password.
	const FString CleanEmail = Email.TrimStartAndEnd();

	if (CleanEmail.IsEmpty() || Password.IsEmpty())
	{
		FailureText = TEXT("Enter an email and a password.");

		return;
	}

	bRememberMe = bRemember;
	bSigningIn = true;
	FailureText.Empty();

	// The email is logged and the password never is. A mangled address is the likeliest reason a
	// sign-in fails, and it is invisible unless the value actually used is shown.
	UE_LOG(LogSpaceMMOBackend, Log, TEXT("Signing in as %s from the login screen."), *CleanEmail);

	Client->LogIn(CleanEmail, Password);
}

void USpaceMMOLoginScreen::HandleSessionChanged(const bool bIsSignedIn)
{
	bSigningIn = false;

	if (!bIsSignedIn)
	{
		return;
	}

	FailureText.Empty();

	if (bRememberMe)
	{
		if (USpaceMMOBackendClient* Client = Backend())
		{
			Client->RememberSession();
		}
	}

	// The password is cleared whatever happens next. It has done its job, and a box still holding it
	// is a password on screen for as long as anybody leaves this open.
	if (PasswordBox != nullptr)
	{
		PasswordBox->SetText(FText::GetEmpty());
	}
}

void USpaceMMOLoginScreen::HandleFailed(const FBackendFailure& Failure)
{
	if (!bSigningIn)
	{
		// Somebody else's failure -- a refused craft, a market call -- reaching this screen while it
		// is not waiting on anything. Those belong to whatever the player just did, and putting one
		// here would answer a question nobody asked.
		return;
	}

	bSigningIn = false;

	FailureText = DescribeFailure(Failure);

	if (PasswordBox != nullptr)
	{
		PasswordBox->SetText(FText::GetEmpty());
	}
}

FString USpaceMMOLoginScreen::DescribeFailure(const FBackendFailure& Failure)
{
	// Worded for the person typing rather than repeating the server. A 401 here has exactly one
	// meaning and "Unauthorized" is not it.
	if (Failure.HttpStatus == 401)
	{
		return TEXT("Wrong email or password.");
	}

	// Everything else verbatim, and something rather than nothing when there is no message at all.
	// A backend that is not running produces no status and no body, which without this is a blank
	// line under the password box -- indistinguishable from a wrong password, and pointing at the
	// wrong thing to go and check.
	return Failure.Message.IsEmpty() ? TEXT("Could not reach the server.") : Failure.Message;
}

USpaceMMOBackendClient* USpaceMMOLoginScreen::Backend() const
{
	const UGameInstance* GameInstance = GetGameInstance();

	return GameInstance != nullptr
		? GameInstance->GetSubsystem<USpaceMMOBackendClient>()
		: nullptr;
}
