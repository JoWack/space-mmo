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
	// An environment variable first, because the default relative path only works when the project
	// is being run from the repository. A staged server lives in
	// Saved/StagedBuilds/WindowsServer/SpaceMMO, so "../secrets" points inside the staged build and
	// finds nothing — and the failure surfaces two steps away, as every player being refused their
	// own character. A variable rather than a command-line flag because arguments to this process
	// have been observed arriving mangled, and a path full of dots is exactly what gets mangled.
	FString SecretPath = FPlatformMisc::GetEnvironmentVariable(TEXT("SPACEMMO_SERVICE_SECRET_FILE"));

	if (SecretPath.IsEmpty())
	{
		SecretPath = FPaths::Combine(
			FPaths::ProjectDir(), TEXT(".."), TEXT("secrets"), TEXT("service-secret.txt"));
	}

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

void USpaceMMOBackendClient::DockAsServer(const int32 CharacterId, const int32 StationId)
{
	if (ServiceSecret.IsEmpty())
	{
		UE_LOG(LogSpaceMMOBackend, Warning,
			TEXT("Dock refused: this machine holds no service credential."));

		return;
	}

	const FString Body = FString::Printf(
		TEXT("{\"characterId\":%d,\"stationId\":%d}"), CharacterId, StationId);

	Send(
		TEXT("POST"),
		TEXT("/docking/dock"),
		Body,
		false,
		[CharacterId, StationId](const FString&)
		{
			UE_LOG(LogSpaceMMOBackend, Log,
				TEXT("Character %d docked at station %d."), CharacterId, StationId);
		},
		// The credential is a parameter, not something Send reads off the subsystem. Omitting it
		// sends no header at all and the API refuses the call as a player pretending to be the
		// simulation — which is the right answer to the request that was actually made.
		ServiceSecret);
}

void USpaceMMOBackendClient::UndockAsServer(const int32 CharacterId)
{
	if (ServiceSecret.IsEmpty())
	{
		return;
	}

	const FString Body = FString::Printf(TEXT("{\"characterId\":%d}"), CharacterId);

	Send(
		TEXT("POST"),
		TEXT("/docking/undock"),
		Body,
		false,
		[CharacterId](const FString&)
		{
			UE_LOG(LogSpaceMMOBackend, Log, TEXT("Character %d undocked."), CharacterId);
		},
		ServiceSecret);
}

void USpaceMMOBackendClient::FetchDockedStation(const int32 CharacterId)
{
	Send(
		TEXT("GET"),
		FString::Printf(TEXT("/docking/%d"), CharacterId),
		FString(),
		true,
		[this](const FString& Body)
		{
			// Absent means not docked, which is a state rather than a failure — a player in
			// flight is the ordinary case, and treating it as an error would log a warning every
			// two seconds for the whole time somebody is travelling.
			int32 Station = 0;

			FSpaceMMOBackendProtocol::ParseDockedStation(Body, Station);

			// Logged on change, because the on-screen "Docked at X" is the simulation announcing
			// its own intent and this is the only place the API's answer becomes visible. When the
			// two disagreed, the message read as success and the market refused anyway, with
			// nothing in the client log to tell them apart.
			if (Station != DockedStationId)
			{
				UE_LOG(LogSpaceMMOBackend, Log,
					TEXT("Server says docked station is %d (was %d)."), Station, DockedStationId);
			}

			DockedStationId = Station;
		});
}

void USpaceMMOBackendClient::GatherAsServer(
	const int32 CharacterId,
	const int64 ResourceNodeId,
	const int32 StationId,
	FOnGatherComplete OnComplete,
	FOnGatherFailed OnRefused)
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
		[this, CharacterId, OnComplete](const FString& ResponseBody)
		{
			FBackendGatherResult Result;

			if (!FSpaceMMOBackendProtocol::ParseGatherResult(ResponseBody, Result))
			{
				return;
			}

			// The caller hears first, so whatever asked can tell the player before anything else
			// reacts to the broadcast.
			OnComplete.ExecuteIfBound(Result);

			UE_LOG(LogSpaceMMOBackend, Log,
				TEXT("Character %d gathered %d unit(s) for %lld xp; node has %d left."),
				CharacterId, Result.Quantity, Result.XpAwarded, Result.NodeRemaining);

			OnGathered.Broadcast(CharacterId, Result);
		},
		ServiceSecret,
		[OnRefused](const FBackendFailure& Failure)
		{
			// Handed back rather than broadcast, because this request was made by the dedicated
			// server on a player's behalf and the player is on another machine entirely.
			OnRefused.ExecuteIfBound(Failure);
		});
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
	const FString& ServiceCredential,
	TFunction<void(const FBackendFailure&)> OnFailure)
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
		[WeakThis, OnSuccess, OnFailure](FHttpRequestPtr, FHttpResponsePtr Response, const bool bConnected)
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

				if (OnFailure)
				{
					OnFailure(Failure);

					return;
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
	// Cleared only when the character actually changes. Calling this again for the same character is
	// a refresh, and a refresh that empties the arrays first leaves the panel blank for a whole round
	// trip — twice over, since skills and inventory answer separately. Gathering refreshes on every
	// successful press, so that flicker would be the normal state of the display rather than a rare
	// one.
	if (CharacterId != SelectedCharacterId)
	{
		Skills.Reset();
		Inventory.Reset();
	}

	SelectedCharacterId = CharacterId;

	// Parsed into a local and swapped in whole. Parsing straight into the live array would leave it
	// briefly half-filled, which is the same flicker by a slower route. A failed request keeps the
	// previous answer, which is the better of the two lies available: stale is closer to true than
	// empty, and the request that follows will correct it.
	Send(
		TEXT("GET"),
		FString::Printf(TEXT("/characters/%d/skills"), CharacterId),
		FString(),
		true,
		[this](const FString& Body)
		{
			TArray<FBackendSkill> Parsed;

			FSpaceMMOBackendProtocol::ParseSkills(Body, Parsed);

			Skills = MoveTemp(Parsed);

			OnCharacterStateLoaded.Broadcast();
		});

	Send(
		TEXT("GET"),
		FString::Printf(TEXT("/characters/%d/inventory"), CharacterId),
		FString(),
		true,
		[this](const FString& Body)
		{
			TArray<FBackendInventoryItem> Parsed;
			TArray<FBackendItemInstance> ParsedInstances;
			TArray<FBackendInventoryContainer> ParsedContainers;

			FSpaceMMOBackendProtocol::ParseInventory(
				Body, Parsed, ParsedInstances, ParsedContainers);

			Inventory = MoveTemp(Parsed);
			ItemInstances = MoveTemp(ParsedInstances);
			Containers = MoveTemp(ParsedContainers);

			OnCharacterStateLoaded.Broadcast();
		});
}

void USpaceMMOBackendClient::FetchRecipes()
{
	Send(
		TEXT("GET"),
		TEXT("/industry/recipes"),
		FString(),
		false,
		[this](const FString& Body)
		{
			TArray<FBackendRecipe> Parsed;

			FSpaceMMOBackendProtocol::ParseRecipes(Body, Parsed);

			Recipes = MoveTemp(Parsed);

			UE_LOG(LogSpaceMMOBackend, Log, TEXT("Loaded %d recipe(s)."), Recipes.Num());

			OnIndustryChanged.Broadcast();
		});
}

void USpaceMMOBackendClient::FetchJobs(const int32 CharacterId)
{
	Send(
		TEXT("GET"),
		FString::Printf(TEXT("/industry/jobs?characterId=%d"), CharacterId),
		FString(),
		true,
		[this](const FString& Body)
		{
			// Swapped in whole rather than parsed into the live array, matching skills and
			// inventory: a half-filled list is a flicker, and this one refreshes on a timer.
			TArray<FBackendIndustryJob> Parsed;

			FSpaceMMOBackendProtocol::ParseIndustryJobs(Body, Parsed);

			Jobs = MoveTemp(Parsed);

			OnIndustryChanged.Broadcast();
		});
}

void USpaceMMOBackendClient::TransferStack(
	const int32 CharacterId,
	const int64 FromInventoryId,
	const int64 ToInventoryId,
	const int32 ItemDefId,
	const int32 Quantity,
	TFunction<void(const FBackendFailure&)> OnFailure)
{
	TWeakObjectPtr<USpaceMMOBackendClient> WeakThis(this);

	Send(
		TEXT("POST"),
		FString::Printf(TEXT("/characters/%d/inventory/transfer"), CharacterId),
		FSpaceMMOBackendProtocol::MakeTransferBody(
			FromInventoryId, ToInventoryId, ItemDefId, Quantity),
		true,
		[WeakThis, CharacterId](const FString&)
		{
			if (USpaceMMOBackendClient* Self = WeakThis.Get())
			{
				// Asked for, not applied locally. The server decided what actually moved -- a
				// partial stack, a refusal on the last unit -- and its answer is the only one worth
				// showing.
				Self->SelectCharacter(CharacterId);
			}
		},
		FString(),
		MoveTemp(OnFailure));
}

void USpaceMMOBackendClient::TransferInstance(
	const int32 CharacterId,
	const int64 ItemInstanceId,
	const int64 ToInventoryId,
	TFunction<void(const FBackendFailure&)> OnFailure)
{
	TWeakObjectPtr<USpaceMMOBackendClient> WeakThis(this);

	Send(
		TEXT("POST"),
		FString::Printf(TEXT("/characters/%d/inventory/transfer-instance"), CharacterId),
		FSpaceMMOBackendProtocol::MakeTransferInstanceBody(ItemInstanceId, ToInventoryId),
		true,
		[WeakThis, CharacterId](const FString&)
		{
			if (USpaceMMOBackendClient* Self = WeakThis.Get())
			{
				Self->SelectCharacter(CharacterId);
			}
		},
		FString(),
		MoveTemp(OnFailure));
}

void USpaceMMOBackendClient::StartJob(
	const int32 CharacterId, const int32 RecipeId, const int32 StationId, const int32 Runs)
{
	TWeakObjectPtr<USpaceMMOBackendClient> WeakThis(this);

	Send(
		TEXT("POST"),
		TEXT("/industry/jobs"),
		FSpaceMMOBackendProtocol::MakeStartJobBody(CharacterId, RecipeId, StationId, Runs),
		true,
		[WeakThis, CharacterId](const FString&)
		{
			if (USpaceMMOBackendClient* Self = WeakThis.Get())
			{
				Self->OnIndustryMessage.Broadcast(TEXT("Job started"), true);

				// Refetched rather than assumed. The server decided when this finishes, and its
				// answer is the only one the claim will be judged against.
				Self->FetchJobs(CharacterId);

				// The fee comes out of the balance at start, so the wallet is stale the instant this
				// succeeds. Without this the displayed credits only moved when something else
				// happened to refresh them -- gathering did, crafting did not -- so the number drifted
				// further from the truth with every job and looked frozen rather than wrong.
				Self->FetchCharacters();
			}
		});
}

void USpaceMMOBackendClient::ClaimJob(const int32 CharacterId, const int64 JobId)
{
	TWeakObjectPtr<USpaceMMOBackendClient> WeakThis(this);

	Send(
		TEXT("POST"),
		TEXT("/industry/jobs/claim"),
		FSpaceMMOBackendProtocol::MakeClaimJobBody(CharacterId, JobId),
		true,
		[WeakThis, CharacterId](const FString&)
		{
			if (USpaceMMOBackendClient* Self = WeakThis.Get())
			{
				Self->OnIndustryMessage.Broadcast(TEXT("Job claimed"), true);

				Self->FetchJobs(CharacterId);

				// The outputs landed in the hangar, so the panel's holdings are now stale — and a
				// craft objective may have advanced on the server without anything here noticing.
				Self->SelectCharacter(CharacterId);
				Self->FetchQuests(CharacterId);

				// Claiming does not move credits today. Refreshed anyway, because the rule worth
				// following is that anything which *might* touch the wallet refreshes it: a stale
				// balance is invisible until a refusal contradicts it, which is far too late to
				// work out which action forgot.
				Self->FetchCharacters();
			}
		});
}

void USpaceMMOBackendClient::SellToFaction(
	const int32 CharacterId, const int32 StationId, const int32 ItemDefId, const int32 Quantity)
{
	TWeakObjectPtr<USpaceMMOBackendClient> WeakThis(this);

	Send(
		TEXT("POST"),
		TEXT("/market/faction-orders/sell"),
		FString::Printf(
			TEXT("{\"characterId\":%d,\"stationId\":%d,\"itemDefId\":%d,\"quantity\":%d}"),
			CharacterId, StationId, ItemDefId, Quantity),
		true,
		[WeakThis, CharacterId](const FString& Body)
		{
			USpaceMMOBackendClient* Self = WeakThis.Get();

			if (Self == nullptr)
			{
				return;
			}

			int32 Sold = 0;
			int64 Paid = 0;

			FSpaceMMOBackendProtocol::ParseFactionSale(Body, Sold, Paid);

			// Reports what the server did, not what was asked for. The daily faucet budget can cut
			// a sale short, and a message that echoed the request would tell a player they had sold
			// material that is still sitting in their hangar.
			Self->OnIndustryMessage.Broadcast(
				Sold > 0
					? FString::Printf(
						TEXT("Sold %d for %s cr"), Sold, *FSpaceMMOBackendProtocol::FormatCredits(Paid))
					: FString(TEXT("Faction bought nothing - daily limit reached")),
				Sold > 0);

			Self->SelectCharacter(CharacterId);
			Self->FetchCharacters();
		});
}

void USpaceMMOBackendClient::FetchBook(const int32 StationId, const int32 ItemDefId)
{
	TWeakObjectPtr<USpaceMMOBackendClient> WeakThis(this);

	Send(
		TEXT("GET"),
		FString::Printf(TEXT("/market/book?stationId=%d&itemDefId=%d"), StationId, ItemDefId),
		FString(),
		false,
		[WeakThis, ItemDefId](const FString& Body)
		{
			USpaceMMOBackendClient* Self = WeakThis.Get();

			if (Self == nullptr)
			{
				return;
			}

			TArray<FBackendBookEntry> Parsed;

			FSpaceMMOBackendProtocol::ParseBook(Body, Parsed);

			Self->Book = MoveTemp(Parsed);

			// Recorded alongside, so a book that arrives after the player has moved on is known to
			// be for the wrong item rather than displayed as the right one's. Two requests in
			// flight can land in either order.
			Self->BookItemDefId = ItemDefId;

			Self->OnIndustryChanged.Broadcast();
		});
}

void USpaceMMOBackendClient::PlaceOrder(
	const int32 CharacterId,
	const int32 StationId,
	const int32 ItemDefId,
	const EBackendOrderSide Side,
	const int64 LimitPriceMinorUnits,
	const int32 Quantity)
{
	TWeakObjectPtr<USpaceMMOBackendClient> WeakThis(this);

	Send(
		TEXT("POST"),
		TEXT("/market/orders"),
		FSpaceMMOBackendProtocol::MakePlaceOrderBody(
			CharacterId, StationId, ItemDefId, Side, LimitPriceMinorUnits, Quantity),
		true,
		[WeakThis, CharacterId, StationId, ItemDefId, Side](const FString&)
		{
			USpaceMMOBackendClient* Self = WeakThis.Get();

			if (Self == nullptr)
			{
				return;
			}

			Self->OnIndustryMessage.Broadcast(
				Side == EBackendOrderSide::Sell ? TEXT("Listed for sale") : TEXT("Buy order placed"),
				true);

			// An order can match the instant it is placed, so goods, credits and the book can all
			// have changed by the time this returns. Asking for all three is cheaper than reasoning
			// about which of them a fill touched.
			Self->SelectCharacter(CharacterId);
			Self->FetchCharacters();
			Self->FetchBook(StationId, ItemDefId);
		});
}

void USpaceMMOBackendClient::FetchQuests(const int32 CharacterId)
{
	Send(
		TEXT("GET"),
		FString::Printf(TEXT("/quests/journal/%d"), CharacterId),
		FString(),
		true,
		[this](const FString& Body)
		{
			TArray<FBackendJournalEntry> Parsed;

			FSpaceMMOBackendProtocol::ParseJournal(Body, Parsed);

			Journal = MoveTemp(Parsed);

			OnIndustryChanged.Broadcast();
		});

	Send(
		TEXT("GET"),
		FString::Printf(TEXT("/quests/available/%d"), CharacterId),
		FString(),
		true,
		[this](const FString& Body)
		{
			TArray<FBackendAvailableQuest> Parsed;

			FSpaceMMOBackendProtocol::ParseAvailableQuests(Body, Parsed);

			AvailableQuests = MoveTemp(Parsed);

			OnIndustryChanged.Broadcast();
		});
}

void USpaceMMOBackendClient::AcceptQuest(const int32 CharacterId, const FString& QuestKey)
{
	TWeakObjectPtr<USpaceMMOBackendClient> WeakThis(this);

	Send(
		TEXT("POST"),
		TEXT("/quests/accept"),
		FSpaceMMOBackendProtocol::MakeAcceptQuestBody(CharacterId, QuestKey),
		true,
		[WeakThis, CharacterId, QuestKey](const FString&)
		{
			if (USpaceMMOBackendClient* Self = WeakThis.Get())
			{
				Self->OnIndustryMessage.Broadcast(
					FString::Printf(TEXT("Accepted %s"), *QuestKey), true);

				// Both lists change: the quest leaves the available list and joins the journal.
				Self->FetchQuests(CharacterId);
			}
		});
}

void USpaceMMOBackendClient::ResolveCharacterAsServer(
	const FString& Token, const int32 ClaimedCharacterId, FOnCharacterResolved OnResolved)
{
	if (ServiceSecret.IsEmpty())
	{
		UE_LOG(LogSpaceMMOBackend, Warning,
			TEXT("Cannot identify players: this machine holds no service credential."));

		OnResolved.ExecuteIfBound(0, 0, FString());

		return;
	}

	// The token goes in the body rather than the URL, matching the API: URLs reach access logs.
	const FString Body = FString::Printf(
		TEXT("{\"token\":\"%s\",\"characterId\":%d}"),
		*Token.ReplaceCharWithEscapedChar(),
		ClaimedCharacterId);

	Send(
		TEXT("POST"),
		TEXT("/accounts/resolve-character"),
		Body,
		false,
		[OnResolved](const FString& ResponseBody)
		{
			FBackendResolvedCharacter Resolved;

			if (!FSpaceMMOBackendProtocol::ParseResolvedCharacter(ResponseBody, Resolved))
			{
				OnResolved.ExecuteIfBound(0, 0, FString());

				return;
			}

			OnResolved.ExecuteIfBound(
				Resolved.AccountId, Resolved.CharacterId, Resolved.CharacterName);
		},
		ServiceSecret);
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

void USpaceMMOBackendClient::FetchStations()
{
	Stations.Reset();

	// Unauthenticated, like the deposits and the order book: where you can dock is not a secret,
	// and this has to work on a dedicated server, which holds no session at all.
	Send(
		TEXT("GET"),
		TEXT("/world/stations"),
		FString(),
		false,
		[this](const FString& Body)
		{
			FSpaceMMOBackendProtocol::ParseStations(Body, Stations);

			int32 Placed = 0;

			for (const FBackendStation& Station : Stations)
			{
				Placed += Station.bPlaced ? 1 : 0;
			}

			// Both numbers, because they differ exactly when content is unfinished, and a station
			// that cannot be docked at is worth noticing before somebody flies to it.
			UE_LOG(LogSpaceMMOBackend, Log, TEXT("Loaded %d station(s), %d placed."),
				Stations.Num(), Placed);

			OnStationsLoaded.Broadcast();
		});
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
