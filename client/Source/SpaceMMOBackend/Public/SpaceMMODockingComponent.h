#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "SpaceMMOCoordinates.h"
#include "SpaceMMODockingComponent.generated.h"

/**
 * Docks and undocks the pawn it is attached to.
 *
 * <strong>The decision is the server's, always.</strong> Pressing the key sends an intent and
 * nothing else — no station, no position, no claim about being in range. The server knows where
 * every ship is because it moved them, and it is the only party that can answer "is this player
 * actually alongside that station" (ADR-0003). The same shape as gathering, for the same reason.
 *
 * It also undocks on its own when a ship leaves. Without that, docked is a state you enter and
 * never exit, and the market gate it feeds would mean nothing after the first visit.
 */
UCLASS(ClassGroup = (SpaceMMO), meta = (BlueprintSpawnableComponent))
class SPACEMMOBACKEND_API USpaceMMODockingComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USpaceMMODockingComponent();

	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	/** Binds the dock key to whatever input component the pawn ends up with. */
	void BindInput(UInputComponent* InputComponent);

	/** Possession is what creates the input component, so that is when binding can succeed. */
	UFUNCTION()
	void HandlePawnRestarted(APawn* Pawn);

	/** Which character this pawn acts for. Zero until the player is identified. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Docking")
	int32 CharacterId = 0;

	/** The station this component last asked the backend to dock at, or zero. */
	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Docking")
	int32 GetDockedStationId() const { return DockedStationId; }

	/**
	 * Puts a ship back at the station its character was left docked at.
	 *
	 * <strong>Server-side, and it moves the ship rather than clearing the record.</strong> The
	 * record survives a restart and the ship does not: a character who quit while docked used to
	 * respawn at a default position with the backend still saying they were at a station, so the
	 * station overlay opened from anywhere and presence -- which is enforced against the record
	 * alone -- made a restart a free trip to that market (task 114).
	 *
	 * Undocking on login would also close that, in one line, and was rejected: it discards state
	 * the player had and leaves somebody who quit inside a station floating outside it.
	 *
	 * The station may not exist in the world yet. Station actors are spawned from backend data and
	 * identity resolves on its own schedule, so this records the intent and the tick below carries
	 * it out once the actor appears — and says so, or says it gave up.
	 */
	void ResumeDockedAt(int32 StationId);

	/**
	 * <strong>Records where the backend says this character is docked. Moves nothing.</strong>
	 *
	 * It used to move the ship to the station, which was task 114's answer to a ship that did not
	 * survive a restart. Task 147 restores a player at the exact position they left, so there is
	 * nothing for this to correct -- and the repositioning fired on every possession rather than
	 * once at sign-in, so boarding a ship parked outside a station teleported it inside.
	 */

protected:
	virtual void BeginPlay() override;

	/**
	 * Adopts a docking the backend already knows about, without asking it again.
	 *
	 * <strong>How the record survives the pawn (task 153).</strong> Docking a ship destroys the
	 * pawn this component is on, and the character who steps out gets a fresh one holding zero — so
	 * the key that was "undock" a moment ago would be "dock" again, and the range check that keeps
	 * a docking honest would not run at all.
	 */
	void AdoptDocking(int32 StationId);

private:
	/** One key, toggling. Docking when docked and undocking when not are both no-ops worth avoiding. */
	void RequestToggleDock();

	/**
	 * Puts the ship away, if this component is on one, and stands the pilot beside the station.
	 *
	 * <strong>Docking is the other half of summoning (ADR-0012, task 153).</strong> Summoning takes
	 * a hull out of a hangar and puts a pawn in the world; docking puts it back, and until it did
	 * the ship stood on the apron while its row said it was inside.
	 *
	 * Called only where there is ground to stand a pilot on; a station with none -- Deepdock, which
	 * orbits nothing -- leaves the ship alongside, and the caller says so on screen.
	 *
	 * @return True when the ship was actually put away, which is what decides whether the record is
	 *         told about it. The record follows the world here rather than leading it.
	 */
	bool StowShipAt(
		const class ASpaceMMOStationActor& Station, const FSystemCoordinate& Ashore);

	/** Places the ship at ResumeStationId if that station exists yet. True when it did. */
	bool TryResume();

	UFUNCTION(Server, Reliable)
	void ServerToggleDock();

	/** Tells the player what happened, since the backend's answer arrives out of band. */
	UFUNCTION(Client, Reliable)
	void ClientDockResult(const FString& Message, bool bSucceeded);

	/** The nearest station this pawn is within docking range of, or null. */
	class ASpaceMMOStationActor* FindStationInRange() const;

	/** Where this pawn is, in system space, or false if it cannot be worked out. */
	bool TryGetSystemPosition(FSystemCoordinate& OutPosition) const;

	/** Server-side only. Which station we believe this character is at. */
	int32 DockedStationId = 0;

	/** A station to be put back at once it exists in the world, or 0. */
	int32 ResumeStationId = 0;

	/** How long the resume has been waiting for its station actor. */
	double SecondsWaitingToResume = 0.0;

	/**
	 * How long to wait before giving up on it, in seconds.
	 *
	 * Bounded, and loud when it expires. A resume that silently never happens leaves the player
	 * exactly where this task found them -- somewhere else, with the record saying otherwise -- and
	 * nothing on screen to distinguish it from the bug being fixed.
	 */
	static constexpr double ResumeTimeoutSeconds = 30.0;

	/**
	 * How far from the station a docking pilot is set down, in kilometres.
	 *
	 * Fifteen metres, inside the thirty a summoned ship parks at, so stepping off a ship you have
	 * just docked does not put you where the next one you summon will appear.
	 */
	static constexpr double DockArrivalOffsetKilometres = 0.015;

	/**
	 * How often the server re-checks that a docked ship is still alongside.
	 *
	 * Not every frame: this walks the station actors, and the answer only changes at the speed a
	 * ship flies. A second is far finer than anybody can exploit and far coarser than a tick.
	 */
	static constexpr double RangeCheckSeconds = 1.0;

	double SecondsSinceRangeCheck = 0.0;

	/**
	 * Which input component the key is bound on, so a replaced one can be bound again.
	 *
	 * <strong>Not a bool.</strong> A flag records that binding happened once and then refuses to do
	 * it again — but possession creates a <em>new</em> input component, so a ship that is boarded,
	 * left and boarded again ends up with its key bound to a dead one. The symptom is the worst kind:
	 * the key does nothing, silently, because no handler runs to say anything.
	 */
	TWeakObjectPtr<class UInputComponent> BoundInput;
};
