#include "SpaceMMOPlayerController.h"

#include "Components/InputComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"
#include "SpaceMMOBackendClient.h"
#include "SpaceMMOBackendLog.h"
#include "SpaceMMOGatheringComponent.h"

ASpaceMMOPlayerController::ASpaceMMOPlayerController()
{
	bReplicates = true;

	// The panel is redrawn from current state each frame, the same way the pawns draw their
	// navigation readouts. Controllers do not tick by default.
	PrimaryActorTick.bCanEverTick = true;
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

void ASpaceMMOPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (InputComponent != nullptr)
	{
		InputComponent->BindAction(
			TEXT("ToggleCharacterPanel"),
			IE_Pressed,
			this,
			&ASpaceMMOPlayerController::ToggleCharacterPanel);
	}
}

void ASpaceMMOPlayerController::ToggleCharacterPanel()
{
	bShowCharacterPanel = !bShowCharacterPanel;

	if (!bShowCharacterPanel && GEngine != nullptr)
	{
		// Cleared explicitly. On-screen messages persist until they expire or are overwritten, and
		// these are drawn with an infinite lifetime, so simply not drawing them leaves the last frame
		// on screen forever.
		for (int32 Line = 0; Line < PanelMaxLines; ++Line)
		{
			GEngine->RemoveOnScreenDebugMessage(PanelMessageKey + Line);
		}
	}
}

void ASpaceMMOPlayerController::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bShowCharacterPanel && IsLocalController())
	{
		DrawCharacterPanel();
	}
}

void ASpaceMMOPlayerController::OnRep_CharacterId()
{
	RefreshPossessedPawn();

	RefreshCharacterState();
}

void ASpaceMMOPlayerController::RefreshCharacterState()
{
	if (CharacterId == 0 || !IsLocalController())
	{
		return;
	}

	const UWorld* World = GetWorld();

	const UGameInstance* GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;

	USpaceMMOBackendClient* Backend =
		GameInstance != nullptr
			? GameInstance->GetSubsystem<USpaceMMOBackendClient>()
			: nullptr;

	if (Backend == nullptr || !Backend->IsSignedIn())
	{
		return;
	}

	Backend->SelectCharacter(CharacterId);
}

void ASpaceMMOPlayerController::DrawCharacterPanel()
{
	if (GEngine == nullptr)
	{
		return;
	}

	const UWorld* World = GetWorld();

	const UGameInstance* GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;

	const USpaceMMOBackendClient* Backend =
		GameInstance != nullptr
			? GameInstance->GetSubsystem<USpaceMMOBackendClient>()
			: nullptr;

	if (Backend == nullptr)
	{
		return;
	}

	TArray<FString> Lines = BuildCharacterPanel(
		CharacterName, Backend->GetSkills(), Backend->GetInventory());

	// Says so rather than silently dropping the tail. A panel that quietly stops listing at forty
	// rows would read as "I do not own that", which is the one thing an inventory display must never
	// get wrong.
	if (Lines.Num() > PanelMaxLines)
	{
		const int32 Hidden = Lines.Num() - PanelMaxLines + 1;

		Lines.SetNum(PanelMaxLines);
		Lines[PanelMaxLines - 1] = FString::Printf(TEXT("   ... and %d more"), Hidden);
	}

	for (int32 Line = 0; Line < PanelMaxLines; ++Line)
	{
		const int32 Key = PanelMessageKey + Line;

		if (Line >= Lines.Num())
		{
			// Removed rather than blanked. A shrinking list -- the last of an ore spent, say --
			// would otherwise leave its final row on screen indefinitely.
			GEngine->RemoveOnScreenDebugMessage(Key);

			continue;
		}

		GEngine->AddOnScreenDebugMessage(Key, 0.0f, FColor::White, Lines[Line]);
	}
}

FString ASpaceMMOPlayerController::GroupDigits(const int64 Value)
{
	const FString Digits = FString::Printf(TEXT("%lld"), FMath::Abs(Value));

	FString Grouped;

	for (int32 Index = 0; Index < Digits.Len(); ++Index)
	{
		// Counted from the right, so the leading group is the short one: 1234567 groups as
		// 1,234,567 rather than 123,456,7.
		if (Index > 0 && (Digits.Len() - Index) % 3 == 0)
		{
			Grouped.AppendChar(TEXT(','));
		}

		Grouped.AppendChar(Digits[Index]);
	}

	// XP is never negative today, but a formatter that silently drops a sign is a formatter that
	// lies the first time it is reused for a balance or a delta.
	return Value < 0 ? TEXT("-") + Grouped : Grouped;
}

TArray<FString> ASpaceMMOPlayerController::BuildCharacterPanel(
	const FString& CharacterName,
	const TArray<FBackendSkill>& Skills,
	const TArray<FBackendInventoryItem>& Inventory)
{
	TArray<FString> Lines;

	Lines.Add(CharacterName.IsEmpty()
		? TEXT("Not identified")
		: FString::Printf(TEXT("== %s =="), *CharacterName));

	// Only trained skills. A character has a row for every skill in the game from creation, and
	// listing thirty untouched zeroes would bury the one line that changed.
	TArray<FBackendSkill> Trained = Skills.FilterByPredicate(
		[](const FBackendSkill& Skill) { return Skill.Xp > 0; });

	// Sorted here rather than trusted from the response. JSON array order is whatever the query
	// returned, and a list that reorders itself between refreshes is unreadable precisely when it is
	// being watched -- which, for this panel, is always.
	Trained.Sort([](const FBackendSkill& A, const FBackendSkill& B) { return A.Name < B.Name; });

	Lines.Add(TEXT("-- Skills --"));

	if (Trained.Num() == 0)
	{
		Lines.Add(TEXT("   nothing trained yet"));
	}

	for (const FBackendSkill& Skill : Trained)
	{
		Lines.Add(FString::Printf(
			TEXT("   %s  lv %d  (%s xp)"),
			*Skill.Name,
			Skill.Level,
			*GroupDigits(Skill.Xp)));
	}

	TArray<FBackendInventoryItem> Held = Inventory;

	Held.Sort([](const FBackendInventoryItem& A, const FBackendInventoryItem& B)
		{ return A.Name < B.Name; });

	// "Holdings", not "Hold". The endpoint returns every stack the character owns across every
	// inventory -- ship holds and station hangars alike -- and gathered ore lands in a hangar. A
	// panel headed "Hold" would have a player looking in their cargo bay for ore that is on a
	// different planet.
	Lines.Add(TEXT("-- Holdings --"));

	if (Held.Num() == 0)
	{
		Lines.Add(TEXT("   empty"));
	}

	for (const FBackendInventoryItem& Item : Held)
	{
		Lines.Add(FString::Printf(TEXT("   %s  x%d"), *Item.Name, Item.Quantity));
	}

	return Lines;
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

	// Also here, not only in OnRep_CharacterId. A replication callback does not fire on the machine
	// that owns the property, so in standalone play -- where the controller is its own authority --
	// OnRep never runs and the panel would sit empty forever. On a dedicated server this is a no-op,
	// because the connection's controller is not local there.
	RefreshCharacterState();
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

			// Logged because this is the last link in the chain and the only one that was
			// previously invisible: identity could resolve correctly and still fail to reach the
			// thing that spends it, and the symptom would be ore credited to nobody. With two
			// players on a server it also shows, at a glance, that each pawn got its own.
			if (CharacterId != 0)
			{
				UE_LOG(LogSpaceMMOBackend, Log, TEXT("%s will gather as character %d (%s)."),
					*GetNameSafe(Possessed), CharacterId, *CharacterName);
			}
		}
	}
}
