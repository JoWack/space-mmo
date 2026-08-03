#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SpaceMMOGatheringComponent.generated.h"

/**
 * Lets the pawn it is attached to gather from a nearby deposit.
 *
 * <strong>A component rather than code on the pawn, because of the module boundary.</strong> The
 * character pawn lives in SpaceMMOCore, which is deliberately free of any notion that items, ore or
 * a backend exist. Gathering is nothing but those things. Attaching it from Backend keeps the
 * dependency pointing one way.
 *
 * <strong>The client names nothing.</strong> Pressing the key sends a bare request — no deposit id,
 * no quantity, no position. The server picks the nearest deposit within range of the position it
 * already considers authoritative, and asks the backend what that entitles the character to. A
 * client that sent a deposit id could name one on the far side of the planet; a client that sent a
 * quantity could name any number at all. This is the same shape as ServerEmbark, and for the same
 * reason.
 */
UCLASS(ClassGroup = (SpaceMMO), meta = (BlueprintSpawnableComponent))
class SPACEMMOBACKEND_API USpaceMMOGatheringComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USpaceMMOGatheringComponent();

	virtual void BeginPlay() override;

	/**
	 * How close a player must stand to work a deposit, in metres.
	 *
	 * Generous on purpose. The deposit is three metres tall and the check is against its base, so a
	 * player standing against it is already a couple of metres out; a tight radius would read as the
	 * key not working.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Gathering")
	double RangeMetres = 8.0;

	/** Which character the backend should credit. Set by whoever knows who is playing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Gathering")
	int32 CharacterId = 0;

	/** Where gathered material is stored. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Gathering")
	int32 StationId = 0;

	/**
	 * Binds the gather key on whichever input component the owning pawn ends up with.
	 *
	 * Safe to call more than once — it binds at most once — because there is no single moment that
	 * is reliably the right one. A pawn has no input component until it is possessed, so binding
	 * at BeginPlay or at attach time silently does nothing, which is exactly how this shipped
	 * broken the first time: no key, no log, nothing to see.
	 */
	UFUNCTION(BlueprintCallable, Category = "SpaceMMO|Gathering")
	void BindInput(class UInputComponent* InputComponent);

	/** Local intent. Does nothing but ask the server. */
	UFUNCTION(BlueprintCallable, Category = "SpaceMMO|Gathering")
	void RequestGather();

	/**
	 * The line a player reads after working a deposit.
	 *
	 * Pure and static so the wording can be tested without a world, a pawn or a server. The three
	 * cases it distinguishes — got some, too soon, deposit spent — all arrive as a 200 with a
	 * different quantity, and confusing "wait a moment" with "this is empty" would have a player
	 * standing at a dead rock indefinitely.
	 */
	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Gathering")
	static FString FormatGatherMessage(
		int32 Quantity, int64 XpAwarded, int32 NodeRemaining, const FString& ItemName);

	/** How long a gather message stays on screen, in seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Gathering")
	float MessageSeconds = 3.0f;

private:
	/**
	 * Tells the player what the server decided.
	 *
	 * <strong>Sent from the server, not concluded by the client.</strong> The client could not
	 * work out the yield if it wanted to — the rate limit is wall-clock time the server measures,
	 * and how much is left in the deposit is shared with everyone else mining it. So the outcome
	 * comes back the same way it was decided.
	 *
	 * Until this existed the whole feature was invisible in-game: it worked, wrote to the
	 * database, and told the player nothing, so the only way to know you had mined anything was to
	 * read the server's log.
	 */
	UFUNCTION(Client, Reliable)
	void ClientGatherResult(
		int32 Quantity, int64 XpAwarded, int32 NodeRemaining, const FString& ItemName);

	/** Possession is what creates the input component, so that is when binding can succeed. */
	UFUNCTION()
	void HandlePawnRestarted(APawn* Pawn);

	UFUNCTION(Server, Reliable)
	void ServerGather();

	/** Guards against double-binding, since binding is attempted from several places. */
	bool bInputBound = false;

	/** Nearest deposit within range of the owner, or null. Server-side truth. */
	class ASpaceMMODepositActor* FindDepositInRange() const;
};
