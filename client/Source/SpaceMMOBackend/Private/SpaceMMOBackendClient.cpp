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

	LoadServiceSecret();

	UE_LOG(LogSpaceMMOBackend, Log, TEXT("Backend client ready, base URL %s"), *BaseUrl);

	if (FParse::Param(FCommandLine::Get(), TEXT("BackendSmokeTest")))
	{
		BeginSmokeTest();
	}
}

void USpaceMMOBackendClient::BeginSmokeTest()
{
	FString Email;
	FString Password;

	if (!FParse::Value(FCommandLine::Get(), TEXT("BackendEmail="), Email)
		|| !FParse::Value(FCommandLine::Get(), TEXT("BackendPassword="), Password))
	{
		UE_LOG(LogSpaceMMOBackend, Error,
			TEXT("SMOKE: -BackendSmokeTest needs -BackendEmail= and -BackendPassword=."));

		return;
	}

	// Subscribed through the same delegates any UI would use, so this exercises the notification
	// path as well as the requests.
	OnSessionChanged.AddDynamic(this, &USpaceMMOBackendClient::OnSmokeSessionChanged);
	OnCharactersLoaded.AddDynamic(this, &USpaceMMOBackendClient::OnSmokeCharactersLoaded);
	OnCharacterStateLoaded.AddDynamic(this, &USpaceMMOBackendClient::OnSmokeCharacterStateLoaded);
	OnFailed.AddDynamic(this, &USpaceMMOBackendClient::OnSmokeFailed);

	UE_LOG(LogSpaceMMOBackend, Log, TEXT("SMOKE: logging in as %s at %s"), *Email, *BaseUrl);

	LogIn(Email, Password);
}

void USpaceMMOBackendClient::OnSmokeSessionChanged(const bool bIsSignedIn)
{
	if (!bIsSignedIn)
	{
		UE_LOG(LogSpaceMMOBackend, Warning, TEXT("SMOKE: signed out."));

		return;
	}

	// The token itself is never logged. Logs get pasted into bug reports.
	UE_LOG(LogSpaceMMOBackend, Log,
		TEXT("SMOKE: signed in as account %d; fetching characters."), Session.AccountId);

	FetchCharacters();
}

void USpaceMMOBackendClient::OnSmokeCharactersLoaded()
{
	UE_LOG(LogSpaceMMOBackend, Log, TEXT("SMOKE: %d character(s) returned."), Characters.Num());

	for (const FBackendCharacter& Character : Characters)
	{
		UE_LOG(LogSpaceMMOBackend, Log,
			TEXT("SMOKE:   #%d %s  race=%d faction=%d home=%d balance=%s cr"),
			Character.Id,
			*Character.Name,
			static_cast<int32>(Character.Race),
			static_cast<int32>(Character.Faction),
			Character.HomeBodyId,
			*Character.FormatBalance());
	}

	if (Characters.Num() == 0 || bSmokeStateRequested)
	{
		return;
	}

	bSmokeStateRequested = true;

	UE_LOG(LogSpaceMMOBackend, Log,
		TEXT("SMOKE: loading state for character %d."), Characters[0].Id);

	SelectCharacter(Characters[0].Id);
}

void USpaceMMOBackendClient::OnSmokeCharacterStateLoaded()
{
	// Fires twice — skills and inventory are separate requests — which is itself worth seeing.
	UE_LOG(LogSpaceMMOBackend, Log,
		TEXT("SMOKE: character %d state: %d skill(s), %d inventory stack(s)."),
		SelectedCharacterId,
		Skills.Num(),
		Inventory.Num());
}

void USpaceMMOBackendClient::OnSmokeFailed(const FBackendFailure& Failure)
{
	UE_LOG(LogSpaceMMOBackend, Error,
		TEXT("SMOKE: FAILED error=%d status=%d message=%s"),
		static_cast<int32>(Failure.Error),
		Failure.HttpStatus,
		*Failure.Message);
}

namespace
{
	/**
	 * A short fingerprint, so two machines can be compared without either one logging its secret.
	 *
	 * FNV-1a over the UTF-8 bytes. Must stay byte-for-byte identical to ServiceCredential.Fingerprint
	 * on the API side — the whole value of this is that the two can be compared by eye, and a hash
	 * that only agrees with itself would be worse than none.
	 */
	FString FingerprintOf(const FString& Value)
	{
		if (Value.IsEmpty())
		{
			return TEXT("<empty>");
		}

		constexpr uint64 Offset = 14695981039346656037ULL;
		constexpr uint64 Prime = 1099511628211ULL;

		const FTCHARToUTF8 Utf8(*Value);
		const uint8* Bytes = reinterpret_cast<const uint8*>(Utf8.Get());

		uint64 Hash = Offset;

		for (int32 Index = 0; Index < Utf8.Length(); ++Index)
		{
			Hash ^= Bytes[Index];
			Hash *= Prime;
		}

		return FString::Printf(TEXT("%016llx"), Hash);
	}
}

void USpaceMMOBackendClient::LoadServiceSecret()
{
	// Always attempted. This used to bail unless the process was a dedicated server or the editor,
	// which silently disabled gathering in standalone play — where the one machine *is* the server
	// and both those checks are false. The secret not being distributed is what keeps it off a
	// player's machine; a runtime guard here only ever managed to lock out the legitimate case.
	const FString SecretPath =
		FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("secrets"), TEXT("service-secret.txt"));

	FString Contents;

	if (!FFileHelper::LoadFileToString(Contents, *SecretPath))
	{
		// Verbose, not a warning. On a real player's machine this file is meant to be absent, and
		// a warning every launch would train everyone to ignore it. The refusal at the point of
		// use is where it actually matters, and that one is loud.
		UE_LOG(LogSpaceMMOBackend, Verbose, TEXT("No service secret at %s."), *SecretPath);

		return;
	}

	// Trimmed, because an editor that helpfully appends a newline would otherwise produce a secret
	// that differs from the API's by one invisible character — a mismatch that reads as the wrong
	// value rather than as trailing whitespace.
	ServiceSecret = Contents.TrimStartAndEnd();

	// Length and a hash prefix, never the secret. Logs get pasted into bug reports, but without
	// some way to tell "the value I hold" from "the value the server holds" apart, a mismatch is
	// indistinguishable from the header not arriving at all — which is exactly the ambiguity that
	// cost a debugging round here.
	UE_LOG(LogSpaceMMOBackend, Log,
		TEXT("Service credential loaded (%d chars, fingerprint %s)."),
		ServiceSecret.Len(),
		*FingerprintOf(ServiceSecret));
}

void USpaceMMOBackendClient::GatherAsServer(
	const int32 CharacterId, const int64 ResourceNodeId, const int32 StationId)
{
	if (ServiceSecret.IsEmpty())
	{
		UE_LOG(LogSpaceMMOBackend, Warning,
			TEXT("Gather refused: this machine holds no service credential."));

		return;
	}

	const FString Body = FString::Printf(
		TEXT("{\"characterId\":%d,\"resourceNodeId\":%lld,\"stationId\":%d}"),
		CharacterId, ResourceNodeId, StationId);

	// bAuthenticated false: the game server presents its own credential, never a player's token,
	// and holds no session to present even if it wanted to.
	Send(
		TEXT("POST"),
		TEXT("/gathering/gather"),
		Body,
		false,
		[this, CharacterId](const FString& ResponseBody)
		{
			FBackendGatherResult Result;

			if (!FSpaceMMOBackendProtocol::ParseGatherResult(ResponseBody, Result))
			{
				return;
			}

			UE_LOG(LogSpaceMMOBackend, Log,
				TEXT("Character %d gathered %d unit(s) for %lld xp; node has %d left."),
				CharacterId, Result.Quantity, Result.XpAwarded, Result.NodeRemaining);

			OnGathered.Broadcast(CharacterId, Result);
		},
		ServiceSecret);
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
	FOnBody OnSuccess,
	const FString& ServiceCredential)
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

	// A separate header from Authorization on purpose, so a service call can never be mistaken for
	// a player session by anything reading this later.
	if (!ServiceCredential.IsEmpty())
	{
		Request->SetHeader(TEXT("X-SpaceMMO-Service"), ServiceCredential);
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

void USpaceMMOBackendClient::FetchBodies()
{
	Bodies.Reset();

	Send(
		TEXT("GET"),
		TEXT("/world/bodies"),
		FString(),
		false,
		[this](const FString& Body)
		{
			FSpaceMMOBackendProtocol::ParseBodies(Body, Bodies);

			UE_LOG(LogSpaceMMOBackend, Log, TEXT("Loaded %d body/bodies."), Bodies.Num());

			OnBodiesLoaded.Broadcast();
		});
}

bool USpaceMMOBackendClient::FindBodyByKey(const FString& Key, FBackendBody& OutBody) const
{
	for (const FBackendBody& Body : Bodies)
	{
		if (Body.Key == Key)
		{
			OutBody = Body;

			return true;
		}
	}

	return false;
}

void USpaceMMOBackendClient::FetchDeposits(const int32 BodyId)
{
	Deposits.Reset();

	// bAuthenticated is false. The world endpoints are public, so this works before anyone has
	// logged in and works on a dedicated server, which holds no session at all.
	Send(
		TEXT("GET"),
		FString::Printf(TEXT("/world/bodies/%d/nodes"), BodyId),
		FString(),
		false,
		[this, BodyId](const FString& Body)
		{
			FSpaceMMOBackendProtocol::ParseResourceNodes(Body, Deposits);

			UE_LOG(LogSpaceMMOBackend, Log, TEXT("Loaded %d deposit(s) for body %d."),
				Deposits.Num(), BodyId);

			OnDepositsLoaded.Broadcast(BodyId);
		});
}
