#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SpaceMMODepositSubsystem.generated.h"

/**
 * Places the world's deposits, on every machine that has a world.
 *
 * A world subsystem rather than anything on the game mode, for the reason written up in
 * USpaceMMOWorldSubsystem: a game mode exists only on the server, so anything it spawns exists only
 * there, and a client would see bare ground where the ore is. OnWorldBeginPlay runs on the server
 * and on every client, which is exactly the audience that needs the deposits to exist.
 *
 * <strong>Both machines place them, and neither transmits them.</strong> The server needs the
 * positions to decide whether a player is close enough to gather; the client needs them to draw
 * something. Both derive the same answer from the same served direction and the same terrain
 * function, so replicating a position would be paying to send what both ends already know — and
 * would introduce the possibility of them disagreeing, which deriving cannot.
 *
 * A separate subsystem from the scenery one because this is the module boundary: SpaceMMOCore is
 * deliberately free of any notion that items or a backend exist, and a deposit is nothing but those
 * things. Core supplies the geometry; this supplies the meaning.
 */
UCLASS()
class SPACEMMOBACKEND_API USpaceMMODepositSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Deposit")
	int32 GetPlacedCount() const { return PlacedDeposits.Num(); }

private:
	/**
	 * Gives a character pawn the ability to gather, as it appears in the world.
	 *
	 * Attached from here rather than built into the pawn, because the pawn is a SpaceMMOCore type
	 * and Core does not know that ore exists. See USpaceMMOGatheringComponent.
	 */
	void AttachGathering(AActor* Actor);

	/** Gives any player-controlled pawn a docking component, on the authority only. */
	void AttachDocking(AActor* Actor);

	UFUNCTION()
	void HandleBodiesLoaded();

	UFUNCTION()
	void HandleDepositsLoaded();

	/**
	 * Places deposits once they, the bodies, and the ground they stand on are all known.
	 *
	 * The same three-way gate stations wait behind, for the same reason. Deposits used to be
	 * fetched only after the bodies had resolved a single scene body, which made the wait implicit:
	 * a second round trip cannot land before the first. Now they are asked for up front like
	 * stations, so they can arrive first, and placing them then would measure every rock against a
	 * planet that does not exist yet.
	 */
	void PlaceDepositsWhenReady();

	/**
	 * Spawns an actor per loaded deposit, each on the planet standing in for its own body.
	 *
	 * <strong>There is no scene body any more.</strong> Until 13 September this subsystem resolved
	 * one -- the <c>BodyKey</c> in <c>DefaultGame.ini</c> -- fetched that body's deposits, and
	 * placed them on that body's planet. With one planet in the scene that was the whole world;
	 * with five (task 157) it was the Capital's ore and four worlds you could land on and find
	 * nothing (task 158). Every deposit now names its body, and the planet it stands on is looked
	 * up the way a station's is.
	 */
	void PlaceDeposits();

	/**
	 * Dev affordance: -GatherSelfTest fires one gather against the first placed deposit, skipping
	 * the pawn, the key and the range check. Runs after placement, which is no longer the same
	 * moment as the deposits arriving.
	 */
	void RunGatherSelfTest() const;

	UFUNCTION()
	void HandleStationsLoaded();

	/**
	 * The planets have the shape they will keep, so anything standing on the ground may go down.
	 *
	 * The third thing station placement waits for. Stations are positioned by asking the terrain
	 * function where the ground is, and that terrain arrives from content on the same broadcast
	 * this subsystem listens to -- so without this the two race, and losing leaves a station
	 * measured against the compiled-in default with the real ground reshaped out from under it.
	 */
	UFUNCTION()
	void HandlePlanetsPainted();

	/**
	 * Places stations once they, the bodies, and the ground they stand on are all known.
	 *
	 * All three arrive in any order, and acting on whichever lands first goes wrong two different
	 * ways: on the ordering where stations beat bodies, every body-relative station is compared
	 * against no planet at all and silently dropped; on the ordering where they beat the terrain,
	 * they are placed against the compiled-in default and left floating when the real ground
	 * arrives.
	 */
	void PlaceStationsWhenReady();

	/**
	 * Puts every placed station in the world.
	 *
	 * Here rather than in a subsystem of its own because this one already resolves the planet a
	 * body-relative position needs, and a second copy of that lookup would be a second chance to
	 * read a different planet's radius.
	 */
	void PlaceStations();

	/**
	 * The planet standing in for each loaded body, by body id.
	 *
	 * One lookup shared by stations and deposits, because the two used to answer "which planet"
	 * differently -- stations by this map, deposits by a configured scene body -- and a second
	 * copy of the lookup is a second chance to read a different planet's radius. A body content has
	 * not placed has no planet and no entry, which is what a caller checks for.
	 */
	TMap<int32, const class ASpaceMMOPlanetActor*> PlanetsByBody(
		const class USpaceMMOBackendClient& Backend) const;

	/** Whether the three things placement waits for have all arrived. */
	bool IsGroundReady() const;

	UPROPERTY()
	TArray<TObjectPtr<class ASpaceMMODepositActor>> PlacedDeposits;

	UPROPERTY()
	TArray<TObjectPtr<class ASpaceMMOStationActor>> PlacedStations;

	/**
	 * Whether the body list has arrived.
	 *
	 * This was a scene body id, zero until bodies had loaded -- and until the configured body had
	 * been found among them, so a world seeded without it never placed a station and said nothing.
	 * Nothing needs a scene body now; what the gate needs is this.
	 */
	bool bBodiesLoaded = false;

	/** Whether the station list has arrived. */
	bool bStationsLoaded = false;

	/** Whether stations have already been placed, so a second trigger does not duplicate them. */
	bool bStationsPlaced = false;

	/** Whether the deposit list has arrived. */
	bool bDepositsLoaded = false;

	/** Whether deposits have already been placed, so a second trigger does not duplicate them. */
	bool bDepositsPlaced = false;

	/** Handle for the spawn callback, so it can be released when the world goes away. */
	FDelegateHandle ActorSpawnedHandle;
};
