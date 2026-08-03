#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "SpaceMMOBackendTypes.h"
#include "SpaceMMOPlayerController.generated.h"

/**
 * A connection, and which character the server has agreed it may act as.
 *
 * <strong>Identity lives here rather than on a pawn.</strong> A player swaps between a ship and a
 * character every time they land, and each swap destroys and spawns pawns; identity that lived on
 * one would be lost the moment somebody stepped out of their ship. The controller outlives all of
 * it.
 *
 * <strong>The claim is checked, not believed.</strong> The client sends the character id it wants
 * along with its session token, and the server hands both to the backend, which reports the
 * character only if the token really belongs to the account that owns it. Ids are sequential
 * integers, so an unchecked claim would let any player join as any character in the game and spend
 * its inventory. The command-line -GatherCharacterId= that this replaces was exactly that hole,
 * kept deliberately narrow while it was single-player scaffolding.
 */
UCLASS()
class SPACEMMOBACKEND_API ASpaceMMOPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ASpaceMMOPlayerController();

	virtual void BeginPlay() override;

	/** Pushes identity onto each new pawn, since a player swaps between ship and character. */
	virtual void OnPossess(APawn* InPawn) override;

	virtual void GetLifetimeReplicatedProps(
		TArray<FLifetimeProperty>& OutLifetimeProps) const override;


	/**
	 * The character this connection may act as, or zero until the server has agreed.
	 *
	 * Server-authoritative and replicated to its owner only. A client reads it to know who it is;
	 * a client writing it changes nothing, because every decision that matters is taken on the
	 * server against the server's copy.
	 */
	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Identity")
	int32 GetCharacterId() const { return CharacterId; }

	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Identity")
	FString GetCharacterName() const { return CharacterName; }

	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Identity")
	bool IsIdentified() const { return CharacterId != 0; }

	/** Where this player's gathered material goes. Not yet chosen per player. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Identity")
	int32 StationId = 1;

private:
	/**
	 * Begins the client-side half: sign in if needed, then present the token to the server.
	 *
	 * Credentials come from the command line because there is no login UI yet. That is a
	 * placeholder for the UI, not for the security model — the token still has to be earned from
	 * the backend, and the server still checks it.
	 */
	void BeginIdentifying();

	/**
	 * Finds an email and password: command line first, then secrets/player-login.txt.
	 *
	 * <strong>The file exists because command lines are not reliable here.</strong> A launch that
	 * passed <c>-BackendEmail=someone@gmail.com</c> arrived as <c>someone@gmail .com</c> — a space
	 * inserted before the dot — and FParse stops at whitespace, so the client cheerfully tried to
	 * log in as "someone@gmail" and got a 401 that looked like a wrong password. The same mangling
	 * turned -ShipStartX=39.56 into 39. A file has no quoting, no escaping and no shell between it
	 * and the value.
	 *
	 * Two lines: email, then password. Same directory as the service secret, and git-ignored for
	 * the same reason.
	 */
	static bool FindCredentials(FString& OutEmail, FString& OutPassword);

	UFUNCTION()
	void HandleSessionChanged(bool bIsSignedIn);

	UFUNCTION()
	void HandleBackendFailed(const FBackendFailure& Failure);

	/**
	 * Gives keyboard focus back to the game viewport when this window is reactivated.
	 *
	 * <strong>Without this, a client that loses focus never gets input again.</strong> Launching a
	 * second client takes focus from the first; the first regains OS focus perfectly well when you
	 * click it — GetForegroundWindow confirms it — but Slate does not hand focus back to the game
	 * viewport, so every key press is dropped before it reaches a pawn. It reads as "only the window
	 * opened last responds", and it survives alt-tabbing and clicking alike.
	 *
	 * Measured, not assumed: with two clients up, four gather presses into the first window produced
	 * zero server-side attempts and four into the second produced four.
	 */
	void EnsureViewportFocus();

	/** Handle for the focus guard, so it stops with the controller. */
	FTimerHandle FocusGuardTimer;

	UFUNCTION()
	void HandleCharactersLoaded();

	/** Sends whatever token and character the client now holds. */
	void PresentCredentials();

	UFUNCTION(Server, Reliable)
	void ServerIdentify(const FString& Token, int32 ClaimedCharacterId);

	/** Applies a resolved identity and tells anything that was waiting for it. */
	void AdoptIdentity(int32 ResolvedCharacterId, const FString& ResolvedName);

	/** Pushes the identity onto whatever the player is currently possessing. */
	void RefreshPossessedPawn();

	UPROPERTY(Replicated)
	int32 CharacterId = 0;

	UPROPERTY(Replicated)
	FString CharacterName;

	/** Which character the client intends to play, from -CharacterId= or the first one it owns. */
	int32 DesiredCharacterId = 0;

	/** Guards against presenting twice when both delegates fire. */
	bool bPresented = false;
};
