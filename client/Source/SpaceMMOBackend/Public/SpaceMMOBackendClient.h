#pragma once

#include "CoreMinimal.h"
#include "SpaceMMOBackendTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SpaceMMOBackendClient.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBackendSessionChanged, bool, bIsSignedIn);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBackendFailed, const FBackendFailure&, Failure);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnBackendCharactersLoaded);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnBackendCharacterStateLoaded);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBackendDepositsLoaded, int32, BodyId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnBackendBodiesLoaded);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnBackendStationsLoaded);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnBackendGathered, int32, CharacterId, const FBackendGatherResult&, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnBackendIndustryChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnBackendIndustryMessage, const FString&, Message, bool, bSucceeded);

/**
 * The client's connection to the game server.
 *
 * A game instance subsystem, so the session survives level transitions — travelling to a new map
 * must not log the player out — while still being torn down cleanly when the game ends.
 *
 * <strong>Everything here is a request, never an assertion.</strong> The client asks the server
 * for state and renders what comes back. There is no method to set a balance, award XP, or
 * complete a quest, because the client is not permitted to know those things before the server
 * does (ADR-0003).
 */
UCLASS()
class SPACEMMOBACKEND_API USpaceMMOBackendClient : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	virtual void Deinitialize() override;

	/** Where the API lives. Defaults to the local development server. */
	UFUNCTION(BlueprintCallable, Category = "SpaceMMO|Backend")
	void SetBaseUrl(const FString& InBaseUrl);

	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Backend")
	FString GetBaseUrl() const { return BaseUrl; }

	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Backend")
	bool IsSignedIn() const { return Session.IsValid(); }

	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Backend")
	int32 GetAccountId() const { return Session.AccountId; }

	UFUNCTION(BlueprintCallable, Category = "SpaceMMO|Backend")
	void Register(const FString& Email, const FString& Password);

	UFUNCTION(BlueprintCallable, Category = "SpaceMMO|Backend")
	void LogIn(const FString& Email, const FString& Password);

	/** Forgets the session locally. The token stays valid server-side until it expires. */
	UFUNCTION(BlueprintCallable, Category = "SpaceMMO|Backend")
	void LogOut();

	UFUNCTION(BlueprintCallable, Category = "SpaceMMO|Backend")
	void FetchCharacters();

	UFUNCTION(BlueprintCallable, Category = "SpaceMMO|Backend")
	void CreateCharacter(const FString& Name, EBackendRace Race);

	/** Loads skills and inventory for a character. */
	UFUNCTION(BlueprintCallable, Category = "SpaceMMO|Backend")
	void SelectCharacter(int32 CharacterId);

	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Backend")
	const TArray<FBackendCharacter>& GetCharacters() const { return Characters; }

	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Backend")
	const TArray<FBackendSkill>& GetSkills() const { return Skills; }

	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Backend")
	const TArray<FBackendInventoryItem>& GetInventory() const { return Inventory; }

	/**
	 * Owned items that do not stack — tools, weapons, hulls.
	 *
	 * Held apart from the stacks because they are apart on the wire and apart in the database, and
	 * flattening them here would lose the condition that makes each one itself.
	 */
	const TArray<FBackendItemInstance>& GetItemInstances() const { return ItemInstances; }

	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Backend")
	int32 GetSelectedCharacterId() const { return SelectedCharacterId; }

	/**
	 * Gathers on behalf of a character, as the game server rather than as that player.
	 *
	 * <strong>Only ever call this with authority.</strong> It presents the game server's service
	 * credential, which lets it act for any character — the whole point being that the server has
	 * already checked the player is standing next to the deposit, which is a fact no client can be
	 * trusted to report. Called from a client it would still be refused, because a client has no
	 * secret to present, but it has no business being called there at all.
	 *
	 * @param StationId Where the ore is stored.
	 */
	DECLARE_DELEGATE_OneParam(FOnGatherComplete, const FBackendGatherResult&);

	/**
	 * A refused gather, handed back to whoever asked for it.
	 *
	 * Needed because gathering is a <em>service</em> call: the dedicated server makes it, so the
	 * refusal arrives on the server and OnFailed broadcasts there, where there is no player to tell.
	 * On a listen server or in standalone the same machine is both and the notice appeared anyway,
	 * which is exactly why this was believed to work.
	 */
	DECLARE_DELEGATE_OneParam(FOnGatherFailed, const FBackendFailure&);

	void GatherAsServer(
		int32 CharacterId,
		int64 ResourceNodeId,
		int32 StationId,
		FOnGatherComplete OnComplete = FOnGatherComplete(),
		FOnGatherFailed OnRefused = FOnGatherFailed());

	/** True if a service secret was found, so the server can actually act. */
	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Backend")
	bool HasServiceSecret() const { return !ServiceSecret.IsEmpty(); }

	/** The session token, so the client can present it to the game server. Never logged. */
	FString GetSessionToken() const { return Session.Token; }

	/** Reports a resolved identity, or zero when the backend refused. */
	DECLARE_DELEGATE_ThreeParams(FOnCharacterResolved, int32 /*AccountId*/, int32 /*CharacterId*/, const FString& /*Name*/);

	/**
	 * Asks the backend whether a token really entitles its bearer to a character.
	 *
	 * Server-side only: it presents the service credential, because this is the game server's
	 * question. See ResolveCharacterAsync on the API for why it is not a player's to ask.
	 */
	void ResolveCharacterAsServer(
		const FString& Token, int32 ClaimedCharacterId, FOnCharacterResolved OnResolved);

	/** Loads every body in the starting system. Unauthenticated, like the deposits. */
	UFUNCTION(BlueprintCallable, Category = "SpaceMMO|Backend")
	void FetchBodies();

	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Backend")
	const TArray<FBackendBody>& GetBodies() const { return Bodies; }

	/**
	 * Finds a body by its content key, e.g. <c>body_capital</c>.
	 *
	 * By key rather than by id, because ids are assigned by the database and differ between any two
	 * seeded environments. A hard-coded id works until the day the database is rebuilt in a
	 * different order, and then places the world's deposits on the wrong planet.
	 *
	 * @return False if no such body has been loaded.
	 */
	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Backend")
	bool FindBodyByKey(const FString& Key, FBackendBody& OutBody) const;

	/**
	 * Loads the deposits on a body.
	 *
	 * Unauthenticated, and deliberately so — the endpoint is public, which means the dedicated
	 * server can call it without holding any player's token. The server is not a player and has no
	 * business having a session; if placing the world required one, it would need credentials of
	 * its own purely to ask where the ore is.
	 */
	UFUNCTION(BlueprintCallable, Category = "SpaceMMO|Backend")
	void FetchDeposits(int32 BodyId);

	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Backend")
	const TArray<FBackendResourceNode>& GetDeposits() const { return Deposits; }

	/**
	 * Loads every station in the system, wherever it is.
	 *
	 * All of them at once rather than per body, because the stations that matter most to a pilot
	 * are the ones attached to no body at all, and a route keyed by body could never return those.
	 */
	UFUNCTION(BlueprintCallable, Category = "SpaceMMO|Backend")
	void FetchStations();

	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Backend")
	const TArray<FBackendStation>& GetStations() const { return Stations; }

	/**
	 * Records that a character has docked, as the game server.
	 *
	 * Service-credential only, like gathering. Where a ship is is the simulation's to know, and a
	 * client asking to be docked is a client asserting a position (ADR-0003).
	 */
	void DockAsServer(int32 CharacterId, int32 StationId);

	void UndockAsServer(int32 CharacterId);

	/** Reads where this character is docked. A player's own token, since it is their business. */
	UFUNCTION(BlueprintCallable, Category = "SpaceMMO|Backend")
	void FetchDockedStation(int32 CharacterId);

	/** The station last reported, or zero when not docked. */
	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Backend")
	int32 GetDockedStationId() const { return DockedStationId; }

	/**
	 * Loads the recipe catalog. Unauthenticated, like bodies and deposits.
	 *
	 * What can be built is authored content, identical for everyone, so requiring a token would
	 * only mean everyone reads it with one.
	 */
	UFUNCTION(BlueprintCallable, Category = "SpaceMMO|Industry")
	void FetchRecipes();

	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Industry")
	const TArray<FBackendRecipe>& GetRecipes() const { return Recipes; }

	/** Loads this character's running jobs. Needs the player's own token. */
	UFUNCTION(BlueprintCallable, Category = "SpaceMMO|Industry")
	void FetchJobs(int32 CharacterId);

	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Industry")
	const TArray<FBackendIndustryJob>& GetJobs() const { return Jobs; }

	/**
	 * Starts a job, as the player rather than as the game server.
	 *
	 * <strong>Unlike gathering, this needs no help from the game server.</strong> Gathering had to
	 * be relayed because "is this player standing next to that deposit" is a fact only the game
	 * server knows. Crafting has no such precondition today: the backend already checks ownership,
	 * skill, tools, materials and funds, and it checks them against its own state rather than
	 * against anything the client says.
	 *
	 * That changes the day docking exists. The station should then come from the server's view of
	 * where the player is, not from this call.
	 */
	UFUNCTION(BlueprintCallable, Category = "SpaceMMO|Industry")
	void StartJob(int32 CharacterId, int32 RecipeId, int32 StationId, int32 Runs);

	UFUNCTION(BlueprintCallable, Category = "SpaceMMO|Industry")
	void ClaimJob(int32 CharacterId, int64 JobId);

	/**
	 * Sells raw material to a faction standing order — the way back from an empty balance.
	 *
	 * Deliberately the worst deal available. It exists so that a player holding nothing but ore is
	 * never stuck, not as a way to make money: selling to another player should always beat it.
	 */
	UFUNCTION(BlueprintCallable, Category = "SpaceMMO|Industry")
	void SellToFaction(int32 CharacterId, int32 StationId, int32 ItemDefId, int32 Quantity);

	/** Loads one station's order book for one item. Unauthenticated: a book is public. */
	UFUNCTION(BlueprintCallable, Category = "SpaceMMO|Market")
	void FetchBook(int32 StationId, int32 ItemDefId);

	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Market")
	const TArray<FBackendBookEntry>& GetBook() const { return Book; }

	/** Which item the loaded book is for, so a stale one is not read as the wrong item's. */
	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Market")
	int32 GetBookItemDefId() const { return BookItemDefId; }

	/**
	 * Places an order.
	 *
	 * The server escrows, matches and settles; this only asks. A sell needs the goods at that
	 * station and a buy needs the credits, and both are checked there rather than here.
	 */
	UFUNCTION(BlueprintCallable, Category = "SpaceMMO|Market")
	void PlaceOrder(
		int32 CharacterId,
		int32 StationId,
		int32 ItemDefId,
		EBackendOrderSide Side,
		int64 LimitPriceMinorUnits,
		int32 Quantity);

	/** Loads the journal and the list of quests that could be taken. */
	UFUNCTION(BlueprintCallable, Category = "SpaceMMO|Quests")
	void FetchQuests(int32 CharacterId);

	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Quests")
	const TArray<FBackendJournalEntry>& GetJournal() const { return Journal; }

	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Quests")
	const TArray<FBackendAvailableQuest>& GetAvailableQuests() const { return AvailableQuests; }

	/**
	 * Accepts a quest.
	 *
	 * <strong>The only thing about a quest a player gets to assert.</strong> Progress is a
	 * consequence of what they actually did, recorded by whichever service did it; there is no way
	 * from here to claim a step is finished, and there should never be one.
	 */
	UFUNCTION(BlueprintCallable, Category = "SpaceMMO|Quests")
	void AcceptQuest(int32 CharacterId, const FString& QuestKey);

	/** Fires when the catalog or the job list changes. */
	UPROPERTY(BlueprintAssignable, Category = "SpaceMMO|Industry")
	FOnBackendIndustryChanged OnIndustryChanged;

	/** Carries a line worth showing the player: what was started, claimed, or refused. */
	UPROPERTY(BlueprintAssignable, Category = "SpaceMMO|Industry")
	FOnBackendIndustryMessage OnIndustryMessage;

	UPROPERTY(BlueprintAssignable, Category = "SpaceMMO|Backend")
	FOnBackendSessionChanged OnSessionChanged;

	UPROPERTY(BlueprintAssignable, Category = "SpaceMMO|Backend")
	FOnBackendFailed OnFailed;

	UPROPERTY(BlueprintAssignable, Category = "SpaceMMO|Backend")
	FOnBackendCharactersLoaded OnCharactersLoaded;

	UPROPERTY(BlueprintAssignable, Category = "SpaceMMO|Backend")
	FOnBackendCharacterStateLoaded OnCharacterStateLoaded;

	UPROPERTY(BlueprintAssignable, Category = "SpaceMMO|Backend")
	FOnBackendDepositsLoaded OnDepositsLoaded;

	UPROPERTY(BlueprintAssignable, Category = "SpaceMMO|Backend")
	FOnBackendBodiesLoaded OnBodiesLoaded;

	UPROPERTY(BlueprintAssignable, Category = "SpaceMMO|Backend")
	FOnBackendStationsLoaded OnStationsLoaded;

	/** Fires on the server, where gathering is decided. */
	UPROPERTY(BlueprintAssignable, Category = "SpaceMMO|Backend")
	FOnBackendGathered OnGathered;

private:
	/**
	 * Drives login → characters → skills and inventory from the command line, logging each step.
	 *
	 * Exists because the protocol layer is tested against fixtures and the API against its own
	 * integration suite, and neither proves the two agree. This walks the real client code across
	 * a real socket to a real server, which is the only thing that does.
	 *
	 * Runs only when <c>-BackendSmokeTest</c> is passed, so a normal session never touches it.
	 */
	void BeginSmokeTest();

	UFUNCTION()
	void OnSmokeSessionChanged(bool bIsSignedIn);

	UFUNCTION()
	void OnSmokeCharactersLoaded();

	UFUNCTION()
	void OnSmokeCharacterStateLoaded();

	UFUNCTION()
	void OnSmokeFailed(const FBackendFailure& Failure);

	/** True once a character's state has been requested, so the summary is logged only once. */
	bool bSmokeStateRequested = false;

	/** Called with the response body once a request succeeds. */
	using FOnBody = TFunction<void(const FString&)>;

	/**
	 * Issues a request.
	 *
	 * @param bAuthenticated Attaches the bearer token. A request that needs one and has none is
	 *                       failed locally rather than sent, since the server would only 401.
	 *
	 * The completion handler captures a weak pointer to this subsystem. An in-flight request whose
	 * response arrives after the game instance is gone must not call into freed memory, and during
	 * shutdown that is not a rare case.
	 */
	/**
	 * @param OnFailure Called instead of broadcasting OnFailed, when a caller wants to handle its
	 *                  own refusal. A request that handles its failure is not also an unhandled
	 *                  one, and broadcasting as well would show a standalone player the same
	 *                  message twice -- once from the global handler and once from its own path.
	 */
	void Send(
		const FString& Verb,
		const FString& Path,
		const FString& Body,
		bool bAuthenticated,
		FOnBody OnSuccess,
		const FString& ServiceCredential = FString(),
		TFunction<void(const FBackendFailure&)> OnFailure = nullptr);

	/**
	 * Loads the game server's credential, if this machine has one.
	 *
	 * Read from a file outside version control rather than compiled in or passed on a command
	 * line, since a command line is visible to every process on the box. Empty on a player's
	 * machine, which is correct: only the server should hold this.
	 */
	void LoadServiceSecret();

	/** Empty unless this machine is running the game server. */
	FString ServiceSecret;

	void HandleSession(const FString& Body);

	/**
	 * Matches the port scripts\api.bat actually serves on.
	 *
	 * This was 5000 — Kestrel's default, but not the one this project uses. Nothing noticed while
	 * the backend was only reached by the opt-in smoke test, and it would have gone on unnoticed:
	 * a world that fetches its deposits from the wrong port simply has no deposits in it, which
	 * looks like a placement bug rather than a configuration one.
	 */
	FString BaseUrl = TEXT("http://localhost:5080");

	FBackendSession Session;

	UPROPERTY()
	TArray<FBackendCharacter> Characters;

	UPROPERTY()
	TArray<FBackendSkill> Skills;

	UPROPERTY()
	TArray<FBackendInventoryItem> Inventory;

	TArray<FBackendItemInstance> ItemInstances;

	UPROPERTY()
	TArray<FBackendResourceNode> Deposits;

	UPROPERTY()
	TArray<FBackendBody> Bodies;

	UPROPERTY()
	TArray<FBackendStation> Stations;

	/** Where this client's character is docked, or zero. */
	int32 DockedStationId = 0;

	UPROPERTY()
	TArray<FBackendRecipe> Recipes;

	UPROPERTY()
	TArray<FBackendIndustryJob> Jobs;

	UPROPERTY()
	TArray<FBackendJournalEntry> Journal;

	UPROPERTY()
	TArray<FBackendAvailableQuest> AvailableQuests;

	UPROPERTY()
	TArray<FBackendBookEntry> Book;

	/** Which item Book holds orders for. Zero until one has been read. */
	int32 BookItemDefId = 0;

	int32 SelectedCharacterId = 0;
};
