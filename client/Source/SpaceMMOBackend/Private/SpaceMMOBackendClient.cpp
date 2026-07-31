#include "SpaceMMOBackendClient.h"

#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "SpaceMMOBackendLog.h"
#include "SpaceMMOBackendProtocol.h"

void USpaceMMOBackendClient::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Overridable from the command line so a packaged build can be pointed at a staging server
	// without a rebuild: -BackendUrl=https://...
	FString Override;

	if (FParse::Value(FCommandLine::Get(), TEXT("BackendUrl="), Override) && !Override.IsEmpty())
	{
		BaseUrl = Override;
	}

	UE_LOG(LogSpaceMMOBackend, Log, TEXT("Backend client ready, base URL %s"), *BaseUrl);
}

void USpaceMMOBackendClient::Deinitialize()
{
	// The token is deliberately not persisted anywhere, so dropping it here is the whole cleanup.
	Session = FBackendSession();

	Super::Deinitialize();
}

void USpaceMMOBackendClient::SetBaseUrl(const FString& InBaseUrl)
{
	BaseUrl = InBaseUrl;
}

void USpaceMMOBackendClient::Send(
	const FString& Verb,
	const FString& Path,
	const FString& Body,
	const bool bAuthenticated,
	FOnBody OnSuccess)
{
	if (bAuthenticated && !Session.IsValid())
	{
		FBackendFailure Failure;
		Failure.Error = EBackendError::Unauthenticated;
		Failure.Message = TEXT("Not signed in.");

		OnFailed.Broadcast(Failure);

		return;
	}

	const TSharedRef<IHttpRequest> Request = FHttpModule::Get().CreateRequest();

	Request->SetVerb(Verb);
	Request->SetURL(FSpaceMMOBackendProtocol::JoinUrl(BaseUrl, Path));
	Request->SetHeader(TEXT("Accept"), TEXT("application/json"));

	if (!Body.IsEmpty())
	{
		Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
		Request->SetContentAsString(Body);
	}

	if (bAuthenticated)
	{
		Request->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + Session.Token);
	}

	// Weak, not strong. A response can arrive after the game instance has been torn down, and a
	// strong capture would keep a dead subsystem alive to be called into.
	TWeakObjectPtr<USpaceMMOBackendClient> WeakThis(this);

	Request->OnProcessRequestComplete().BindLambda(
		[WeakThis, OnSuccess](FHttpRequestPtr, FHttpResponsePtr Response, const bool bConnected)
		{
			USpaceMMOBackendClient* Client = WeakThis.Get();

			if (Client == nullptr)
			{
				return;
			}

			const int32 Status = bConnected && Response.IsValid() ? Response->GetResponseCode() : 0;
			const FString ResponseBody = Response.IsValid() ? Response->GetContentAsString() : FString();

			const FBackendFailure Failure =
				FSpaceMMOBackendProtocol::ClassifyFailure(Status, ResponseBody);

			if (Failure.Error != EBackendError::None)
			{
				UE_LOG(LogSpaceMMOBackend, Warning,
					TEXT("Request failed (%d): %s"), Failure.HttpStatus, *Failure.Message);

				// A rejected token is stale by definition, so drop it rather than letting every
				// later request fail the same way with no explanation.
				if (Failure.Error == EBackendError::Unauthenticated && Client->Session.IsValid())
				{
					Client->LogOut();
				}

				Client->OnFailed.Broadcast(Failure);

				return;
			}

			OnSuccess(ResponseBody);
		});

	Request->ProcessRequest();
}

void USpaceMMOBackendClient::HandleSession(const FString& Body)
{
	FBackendSession Parsed;

	if (!FSpaceMMOBackendProtocol::ParseSession(Body, Parsed))
	{
		FBackendFailure Failure;
		Failure.Error = EBackendError::Server;
		Failure.Message = TEXT("Could not read the session response.");

		OnFailed.Broadcast(Failure);

		return;
	}

	Session = Parsed;

	UE_LOG(LogSpaceMMOBackend, Log, TEXT("Signed in as account %d."), Session.AccountId);

	OnSessionChanged.Broadcast(true);
}

void USpaceMMOBackendClient::Register(const FString& Email, const FString& Password)
{
	Send(
		TEXT("POST"),
		TEXT("/accounts/register"),
		FSpaceMMOBackendProtocol::MakeCredentialsBody(Email, Password),
		false,
		[this](const FString& Body) { HandleSession(Body); });
}

void USpaceMMOBackendClient::LogIn(const FString& Email, const FString& Password)
{
	Send(
		TEXT("POST"),
		TEXT("/accounts/login"),
		FSpaceMMOBackendProtocol::MakeCredentialsBody(Email, Password),
		false,
		[this](const FString& Body) { HandleSession(Body); });
}

void USpaceMMOBackendClient::LogOut()
{
	if (!Session.IsValid())
	{
		return;
	}

	Session = FBackendSession();
	Characters.Reset();
	Skills.Reset();
	Inventory.Reset();
	SelectedCharacterId = 0;

	OnSessionChanged.Broadcast(false);
}

void USpaceMMOBackendClient::FetchCharacters()
{
	Send(
		TEXT("GET"),
		TEXT("/characters/"),
		FString(),
		true,
		[this](const FString& Body)
		{
			if (FSpaceMMOBackendProtocol::ParseCharacterList(Body, Characters))
			{
				OnCharactersLoaded.Broadcast();
			}
		});
}

void USpaceMMOBackendClient::CreateCharacter(const FString& Name, const EBackendRace Race)
{
	Send(
		TEXT("POST"),
		TEXT("/characters/"),
		FSpaceMMOBackendProtocol::MakeCreateCharacterBody(Name, Race),
		true,
		[this](const FString&)
		{
			// Refetched rather than appending the response. The server decides faction and
			// starting body, and re-reading the list is how the client stays a mirror of the
			// server's view rather than a second opinion about it.
			FetchCharacters();
		});
}

void USpaceMMOBackendClient::SelectCharacter(const int32 CharacterId)
{
	SelectedCharacterId = CharacterId;

	Skills.Reset();
	Inventory.Reset();

	Send(
		TEXT("GET"),
		FString::Printf(TEXT("/characters/%d/skills"), CharacterId),
		FString(),
		true,
		[this](const FString& Body)
		{
			FSpaceMMOBackendProtocol::ParseSkills(Body, Skills);

			OnCharacterStateLoaded.Broadcast();
		});

	Send(
		TEXT("GET"),
		FString::Printf(TEXT("/characters/%d/inventory"), CharacterId),
		FString(),
		true,
		[this](const FString& Body)
		{
			FSpaceMMOBackendProtocol::ParseInventory(Body, Inventory);

			OnCharacterStateLoaded.Broadcast();
		});
}
