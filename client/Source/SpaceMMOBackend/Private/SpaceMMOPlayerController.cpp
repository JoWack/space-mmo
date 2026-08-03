#include "SpaceMMOPlayerController.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"
#include "SpaceMMOBackendClient.h"
#include "SpaceMMOBackendLog.h"
#include "SpaceMMOGatheringComponent.h"

ASpaceMMOPlayerController::ASpaceMMOPlayerController()
{
	bReplicates = true;
}

void ASpaceMMOPlayerController::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Owner only. Who else is playing is not a secret, but it is not this controller's business to
	// broadcast it, and replicating identity to every connection would put every player's character
	// id in every other player's memory for no gain.
	DOREPLIFETIME_CONDITION(ASpaceMMOPlayerController, CharacterId, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ASpaceMMOPlayerController, CharacterName, COND_OwnerOnly);
}

void ASpaceMMOPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// Only the machine sitting in front of the player has a token to present.
	if (IsLocalController())
	{
		BeginIdentifying();
	}
}

void ASpaceMMOPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	RefreshPossessedPawn();
}

void ASpaceMMOPlayerController::BeginIdentifying()
{
	const UWorld* World = GetWorld();

	const UGameInstance* GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;

	USpaceMMOBackendClient* Backend =
		GameInstance != nullptr
			? GameInstance->GetSubsystem<USpaceMMOBackendClient>()
			: nullptr;

	if (Backend == nullptr)
	{
		return;
	}

	FParse::Value(FCommandLine::Get(), TEXT("CharacterId="), DesiredCharacterId);

	// Already signed in — a level transition, say — so there is nothing to wait for.
	if (Backend->IsSignedIn())
	{
		PresentCredentials();

		return;
	}

	FString Email;
	FString Password;

	if (!FindCredentials(Email, Password))
	{
		// Not an error yet. Without credentials this connection simply has no identity, and every
		// action that needs one says so at the point it is needed rather than here.
		UE_LOG(LogSpaceMMOBackend, Log,
			TEXT("No credentials found; this connection will have no character. "
				 "Write email and password on two lines in secrets\\player-login.txt."));

		return;
	}

	// The email is logged and the password never is. A mangled address is the single most likely
	// reason a login fails here, and it is invisible unless the value actually used is shown —
	// which cost a debugging round when "joe@gmail.com" arrived as "joe@gmail".
	UE_LOG(LogSpaceMMOBackend, Log, TEXT("Signing in as %s."), *Email);

	Backend->OnSessionChanged.AddDynamic(this, &ASpaceMMOPlayerController::HandleSessionChanged);
	Backend->OnCharactersLoaded.AddDynamic(this, &ASpaceMMOPlayerController::HandleCharactersLoaded);
	Backend->OnFailed.AddDynamic(this, &ASpaceMMOPlayerController::HandleBackendFailed);

	Backend->LogIn(Email, Password);
}

bool ASpaceMMOPlayerController::FindCredentials(FString& OutEmail, FString& OutPassword)
{
	const bool bFromCommandLine =
		FParse::Value(FCommandLine::Get(), TEXT("BackendEmail="), OutEmail)
		&& FParse::Value(FCommandLine::Get(), TEXT("BackendPassword="), OutPassword);

	if (bFromCommandLine)
	{
		return true;
	}

	// Overridable so two clients on one machine can be two different players — which is exactly
	// what testing a server needs, and impossible when both read the same file.
	FString Path;

	if (!FParse::Value(FCommandLine::Get(), TEXT("BackendLoginFile="), Path) || Path.IsEmpty())
	{
		Path = FPaths::Combine(
			FPaths::ProjectDir(), TEXT(".."), TEXT("secrets"), TEXT("player-login.txt"));
	}

	TArray<FString> Lines;

	if (!FFileHelper::LoadFileToStringArray(Lines, *Path) || Lines.Num() < 2)
	{
		return false;
	}

	OutEmail = Lines[0].TrimStartAndEnd();
	OutPassword = Lines[1].TrimStartAndEnd();

	return !OutEmail.IsEmpty() && !OutPassword.IsEmpty();
}

void ASpaceMMOPlayerController::HandleBackendFailed(const FBackendFailure& Failure)
{
	// Only worth reporting while still trying to sign in. Later failures belong to whatever asked.
	if (bPresented)
	{
		return;
	}

	UE_LOG(LogSpaceMMOBackend, Warning,
		TEXT("Sign-in failed (%d): %s. This connection will have no character, so gathering will "
			 "credit nobody. Check the address above is the one you registered, and that the "
			 "account exists."),
		Failure.HttpStatus,
		*Failure.Message);
}

void ASpaceMMOPlayerController::HandleSessionChanged(const bool bIsSignedIn)
{
	if (!bIsSignedIn)
	{
		return;
	}

	const UWorld* World = GetWorld();

	const UGameInstance* GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;

	USpaceMMOBackendClient* Backend =
		GameInstance != nullptr
			? GameInstance->GetSubsystem<USpaceMMOBackendClient>()
			: nullptr;

	if (Backend == nullptr)
	{
		return;
	}

	// A named character still has to be confirmed as one this account owns, and the list is how the
	// client discovers that. It is not a security check — the server repeats it — but sending a
	// claim the account plainly cannot back is a guaranteed refusal.
	Backend->FetchCharacters();
}

void ASpaceMMOPlayerController::HandleCharactersLoaded()
{
	PresentCredentials();
}

void ASpaceMMOPlayerController::PresentCredentials()
{
	if (bPresented)
	{
		return;
	}

	const UWorld* World = GetWorld();

	const UGameInstance* GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;

	const USpaceMMOBackendClient* Backend =
		GameInstance != nullptr
			? GameInstance->GetSubsystem<USpaceMMOBackendClient>()
			: nullptr;

	if (Backend == nullptr || !Backend->IsSignedIn())
	{
		return;
	}

	int32 Claimed = DesiredCharacterId;

	if (Claimed == 0)
	{
		const TArray<FBackendCharacter>& Characters = Backend->GetCharacters();

		if (Characters.Num() == 0)
		{
			UE_LOG(LogSpaceMMOBackend, Warning,
				TEXT("Signed in but this account has no characters; nothing to play as."));

			return;
		}

		Claimed = Characters[0].Id;
	}

	bPresented = true;

	UE_LOG(LogSpaceMMOBackend, Log, TEXT("Claiming character %d."), Claimed);

	ServerIdentify(Backend->GetSessionToken(), Claimed);
}

void ASpaceMMOPlayerController::ServerIdentify_Implementation(
	const FString& Token, const int32 ClaimedCharacterId)
{
	const UWorld* World = GetWorld();

	const UGameInstance* GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;

	USpaceMMOBackendClient* Backend =
		GameInstance != nullptr
			? GameInstance->GetSubsystem<USpaceMMOBackendClient>()
			: nullptr;

	if (Backend == nullptr)
	{
		return;
	}

	// Nothing about the claim is trusted here. The backend decides, using the token as proof, and
	// the id the client sent is only the question being asked.
	TWeakObjectPtr<ASpaceMMOPlayerController> WeakThis(this);

	Backend->ResolveCharacterAsServer(
		Token,
		ClaimedCharacterId,
		USpaceMMOBackendClient::FOnCharacterResolved::CreateLambda(
			[WeakThis, ClaimedCharacterId](
				const int32 AccountId, const int32 ResolvedCharacterId, const FString& Name)
			{
				ASpaceMMOPlayerController* Controller = WeakThis.Get();

				if (Controller == nullptr)
				{
					return;
				}

				if (ResolvedCharacterId == 0)
				{
					// Logged, and the connection simply stays anonymous. Kicking would be the
					// harsher option and is worth considering once there is a login screen to send
					// somebody back to.
					UE_LOG(LogSpaceMMOBackend, Warning,
						TEXT("Refused claim on character %d: the token does not entitle it."),
						ClaimedCharacterId);

					return;
				}

				Controller->AdoptIdentity(ResolvedCharacterId, Name);

				UE_LOG(LogSpaceMMOBackend, Log,
					TEXT("Connection identified as character %d (%s) on account %d."),
					ResolvedCharacterId, *Name, AccountId);
			}));
}

void ASpaceMMOPlayerController::AdoptIdentity(
	const int32 ResolvedCharacterId, const FString& ResolvedName)
{
	CharacterId = ResolvedCharacterId;
	CharacterName = ResolvedName;

	RefreshPossessedPawn();
}

void ASpaceMMOPlayerController::RefreshPossessedPawn()
{
	// Identity can arrive before or after a pawn — the backend round trip races possession — so
	// both orders have to work. This handles "identity last"; the component asks the controller
	// when it is spawned, which handles "identity first".
	if (APawn* Possessed = GetPawn())
	{
		if (USpaceMMOGatheringComponent* Gathering =
			Possessed->FindComponentByClass<USpaceMMOGatheringComponent>())
		{
			Gathering->CharacterId = CharacterId;
			Gathering->StationId = StationId;
		}
	}
}
