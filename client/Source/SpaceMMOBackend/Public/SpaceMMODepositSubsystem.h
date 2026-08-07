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

	/** Which body's deposits to place. Resolved to an id by key, never hard-coded as a number. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Deposit")
	FString BodyKey = TEXT("body_capital");

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

	UFUNCTION()
	void HandleBodiesLoaded();

	UFUNCTION()
	void HandleDepositsLoaded(int32 BodyId);

	/** Spawns an actor per loaded deposit, on the planet the scenery subsystem built. */
	void PlaceDeposits();

	UFUNCTION()
	void HandleStationsLoaded();

	/**
	 * Puts every placed station in the world.
	 *
	 * Here rather than in a subsystem of its own because this one already resolves the planet a
	 * body-relative position needs, and a second copy of that lookup would be a second chance to
	 * read a different planet's radius.
	 */
	void PlaceStations();

	UPROPERTY()
	TArray<TObjectPtr<class ASpaceMMODepositActor>> PlacedDeposits;

	UPROPERTY()
	TArray<TObjectPtr<class ASpaceMMOStationActor>> PlacedStations;

	/** Handle for the spawn callback, so it can be released when the world goes away. */
	FDelegateHandle ActorSpawnedHandle;
};
