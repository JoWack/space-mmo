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

	/**
	 * Which body's deposits and stations belong in this scene: whichever one the planet draws.
	 *
	 * <strong>Asked of the planet rather than configured again here.</strong> This was a second
	 * <c>BodyKey</c>, hard-coded to <c>body_capital</c> while the planet actor drew
	 * <c>body_ares</c> from <c>DefaultGame.ini</c> — so the world had Ares' terrain with the
	 * Capital's deposits standing on it, and neither setting looked wrong from where it was
	 * written. It surfaced when the authoring tool (task 96) made somebody ask which body to
	 * author against, and the honest answer needed both files and a playtest log.
	 *
	 * Two settings for one question is the bug. There is now one, and this reads it.
	 */
	FString SceneBodyKey() const;

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
	void HandleDepositsLoaded(int32 BodyId);

	/** Spawns an actor per loaded deposit, on the planet the scenery subsystem built. */
	void PlaceDeposits();

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
	 * Puts every placed station in the world.
	 *
	 * Here rather than in a subsystem of its own because this one already resolves the planet a
	 * body-relative position needs, and a second copy of that lookup would be a second chance to
	 * read a different planet's radius.
	 */
	/**
	 * Places stations once they, the scene's body, and the ground they stand on are all known.
	 *
	 * All three arrive in any order, and acting on whichever lands first goes wrong two different
	 * ways: on the ordering where stations beat bodies, every body-relative station is compared
	 * against a scene body of zero and silently dropped; on the ordering where they beat the
	 * terrain, they are placed against the compiled-in default and left floating when the real
	 * ground arrives.
	 */
	void PlaceStationsWhenReady();

	void PlaceStations();

	UPROPERTY()
	TArray<TObjectPtr<class ASpaceMMODepositActor>> PlacedDeposits;

	UPROPERTY()
	TArray<TObjectPtr<class ASpaceMMOStationActor>> PlacedStations;

	/** The body this scene actually has a planet for. Zero until bodies have loaded. */
	int32 SceneBodyId = 0;

	/** Whether the station list has arrived. */
	bool bStationsLoaded = false;

	/** Whether stations have already been placed, so a second trigger does not duplicate them. */
	bool bStationsPlaced = false;

	/** Handle for the spawn callback, so it can be released when the world goes away. */
	FDelegateHandle ActorSpawnedHandle;
};
