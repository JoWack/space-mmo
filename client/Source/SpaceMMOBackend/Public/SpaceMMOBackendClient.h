#pragma once

#include "CoreMinimal.h"
#include "SpaceMMOBackendTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SpaceMMOBackendClient.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBackendSessionChanged, bool, bIsSignedIn);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBackendFailed, const FBackendFailure&, Failure);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnBackendCharactersLoaded);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnBackendCharacterStateLoaded);

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

	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Backend")
	int32 GetSelectedCharacterId() const { return SelectedCharacterId; }

	UPROPERTY(BlueprintAssignable, Category = "SpaceMMO|Backend")
	FOnBackendSessionChanged OnSessionChanged;

	UPROPERTY(BlueprintAssignable, Category = "SpaceMMO|Backend")
	FOnBackendFailed OnFailed;

	UPROPERTY(BlueprintAssignable, Category = "SpaceMMO|Backend")
	FOnBackendCharactersLoaded OnCharactersLoaded;

	UPROPERTY(BlueprintAssignable, Category = "SpaceMMO|Backend")
	FOnBackendCharacterStateLoaded OnCharacterStateLoaded;

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
	void Send(
		const FString& Verb,
		const FString& Path,
		const FString& Body,
		bool bAuthenticated,
		FOnBody OnSuccess);

	void HandleSession(const FString& Body);

	FString BaseUrl = TEXT("http://localhost:5000");

	FBackendSession Session;

	UPROPERTY()
	TArray<FBackendCharacter> Characters;

	UPROPERTY()
	TArray<FBackendSkill> Skills;

	UPROPERTY()
	TArray<FBackendInventoryItem> Inventory;

	int32 SelectedCharacterId = 0;
};
