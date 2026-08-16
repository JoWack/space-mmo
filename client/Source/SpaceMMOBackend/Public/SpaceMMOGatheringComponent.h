#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SpaceMMOTransientMessages.h"

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
	/**
	 * The deposit this pawn could work right now, or null.
	 *
	 * Public so the HUD and the gather key ask the same question of the same code. A panel with its
	 * own idea of "in reach" would eventually name one rock while E worked another, and a player
	 * would be told they are standing at something they are not.
	 */
	class ASpaceMMODepositActor* FindDepositInRange() const;

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

	/**
	 * Shortest gap between requests the client will send, in seconds.
	 *
	 * <strong>Not a rate limit</strong> — the server owns that, and it is measured in wall-clock
	 * time no client can influence. This only stops a held or hammered key turning into one HTTP
	 * round trip per press. Two players doing that queued dozens of transactions on the same rows
	 * and pushed response times from 30 ms to over eight seconds, which feels exactly like a
	 * broken key even though every answer was correct.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Gathering")
	float MinimumRequestSeconds = 0.5f;

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

	/**
	 * Whether a gather result is something gained or something to explain.
	 *
	 * Kept beside the wording and tested with it, because the two must agree: a message reading
	 * "+3 Ferrite Ore" in the colour of a refusal is worse than either alone. Quantity is the whole
	 * question — the server sends a 200 either way, and nothing else distinguishes them.
	 */
	static ESpaceMMOMessageTone GatherTone(int32 Quantity);

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

	/**
	 * A refusal, delivered to the player who pressed the key.
	 *
	 * Gathering is a service call made by the dedicated server, so a 409 arrives there and the
	 * global failure handler shows it to nobody. Player B pressed E at a ferrite deposit without a
	 * mining laser and got silence, while the server logged "You need a Crude Mining Laser to do
	 * this." In standalone the same machine is both ends, which is why this looked like it worked.
	 */
	UFUNCTION(Client, Reliable)
	void ClientGatherRefused(const FString& Reason);

	/** The controller to tell, or null when this pawn is not somebody's. */
	class ASpaceMMOPlayerController* OwningController() const;

	/** Possession is what creates the input component, so that is when binding can succeed. */
	UFUNCTION()
	void HandlePawnRestarted(APawn* Pawn);

	UFUNCTION(Server, Reliable)
	void ServerGather();

	/**
	 * Which input component the key is bound on, so a replaced one can be bound again.
	 *
	 * <strong>Not a bool.</strong> A flag cannot tell "already bound" from "bound to something that
	 * has been replaced", and possession builds a new input component every time — which is how the
	 * docking key came to do nothing at all after a ship was boarded twice.
	 */
	TWeakObjectPtr<class UInputComponent> BoundInput;

	/** When the last request went out, so a hammered key does not become a queue of round trips. */
	double LastRequestSeconds = -1000.0;

	/** Nearest deposit within range of the owner, or null. Server-side truth. */

};
