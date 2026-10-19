#pragma once

#include "CoreMinimal.h"
#include "SpaceMMOCoordinates.h"
#include "Subsystems/WorldSubsystem.h"

#include "SpaceMMOLootSubsystem.generated.h"

/**
 * What dying costs, on the game server, and the crates it drops (task 103).
 *
 * <para>
 * <strong>Costing a death.</strong> When a hit kills, the combat component hands the body here
 * (<see cref="CostDeath"/>). This works out where the crate goes -- a metre beside the body, on the ground -- and asks
 * the API to take what the death costs there; the answer puts the crate in the world and tells the dead player what
 * they lost. A respawn asked for meanwhile waits for it (<see cref="WhenCosted"/>), or the API would cost the death
 * itself at the character's last recorded whereabouts, without a crate in the world or a word on the panel.
 * </para>
 * <para>
 * <strong>Crates already lying about</strong> -- from before a server restart, or costed by a respawn -- are fetched
 * from the API and put in the world (<see cref="SyncCrates"/>).
 * </para>
 * <para>
 * Also gives every character pawn a <c>USpaceMMOLootComponent</c>, on the server, which replicates it.
 * </para>
 */
UCLASS()
class SPACEMMOBACKEND_API USpaceMMOLootSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	virtual void Deinitialize() override;

	/** Server: takes what a death costs, with the crate beside the body. */
	void CostDeath(class USpaceMMOCombatComponent* Dead);

	/** Whether a character's death is still being costed. */
	bool IsCosting(int32 CharacterId) const { return Costing.Contains(CharacterId); }

	/** Runs once the character's death has been costed: at once when it is not being. */
	void WhenCosted(int32 CharacterId, TFunction<void()> Then);

	/** Server: fetches every live crate from the API, puts in the world those not in it, and removes the rest. */
	void SyncCrates();

	/** The nearest crate within reach of a point, on any machine, or null. */
	class ASpaceMMOLootCrateActor* NearestCrate(
		const FSystemCoordinate& Where, double RangeMetres, double* OutDistanceMetres = nullptr) const;

	/** Server: puts a crate the API described in the world, or refreshes the one already there. */
	class ASpaceMMOLootCrateActor* SpawnCrate(const struct FBackendLootContainer& Container);

private:
	/** Gives a character pawn the ability to loot, as it appears; authority only, as gathering is attached. */
	void AttachLoot(AActor* Actor);

	/** Which way is up at a point: the ground's normal on the nearest planet. */
	FVector UpAt(const FSystemCoordinate& Where) const;

	double ServerNowSeconds() const;

	/** Characters whose deaths are being costed, and what waits for each. */
	TMap<int32, TArray<TFunction<void()>>> Costing;

	FDelegateHandle ActorSpawnedHandle;

	FTimerHandle SyncTimer;
};
