#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "SpaceMMOGameMode.generated.h"

/**
 * Spawns the player onto their planet, on foot.
 *
 * <strong>Task 120, and the first sentence of ADR-0012.</strong> Every connection used to spawn
 * flying, which contradicted the design — nobody starts with a ship — and made the ground expensive
 * to look at, since terrain exists only within 32 km of the planet's centre. On foot the horizon is
 * 283 m and the terrain patch is simply the world.
 *
 * Summoning, active-ship state and the questline that hands over a first hull stay in task 115. Until
 * that lands a starter ship is spawned alongside, unpossessed, for somebody to walk over and board:
 * flight is the most-tested thing in this project and losing casual access to it would be a poor
 * trade for a change about where a character stands.
 */
UCLASS()
class SPACEMMOCORE_API ASpaceMMOGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ASpaceMMOGameMode();

	/**
	 * Chooses the player controller class before any controller is spawned.
	 *
	 * Not the constructor. A game mode's constructor first runs while its class default object is
	 * being created, which happens as this module loads — before SpaceMMOBackend's classes are
	 * registered, so resolving one by path there fails and warns. It appeared to work only because
	 * the constructor runs again when the game mode is actually spawned. InitGame runs once, at
	 * spawn, with every module loaded, which is when the answer is knowable.
	 */
	virtual void InitGame(
		const FString& MapName, const FString& Options, FString& ErrorMessage) override;

	virtual void StartPlay() override;

	/**
	 * Spawns the default pawn against the height function rather than at a placed transform.
	 *
	 * Deferred, because <c>ASpaceMMOCharacterPawn::BeginPlay</c> resolves the ground and aligns to
	 * it: a position set after <c>FinishSpawning</c> arrives too late and the first frame is spent
	 * somewhere else entirely. That ordering is documented on
	 * <c>SetStartingSystemPosition</c> and it is the reason this override exists at all.
	 */
	virtual APawn* SpawnDefaultPawnAtTransform_Implementation(
		AController* NewPlayer, const FTransform& SpawnTransform) override;

protected:
	/**
	 * Spawns marker cubes and lighting on play.
	 *
	 * On until there is real content. The default map is the engine's empty one, and an unlit void
	 * with nothing in it makes flight impossible to evaluate — a ship at rest and a ship at two
	 * kilometres a second look identical.
	 */
	UPROPERTY(EditAnywhere, Category = "SpaceMMO")
	bool bSpawnTestScene = true;

	/**
	 * Which way from the planet's centre a new character stands, and how far out the planet is.
	 *
	 * Configured rather than passed on a command line: this machine mangles Unreal arguments — an
	 * email has arrived split on a space — and a starting position that silently lands somewhere
	 * else is a bug that looks like terrain.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "SpaceMMO")
	FVector StartingDirection = FVector(0.0, 0.0, 1.0);

	/** How far above the surface to drop from, in kilometres. */
	UPROPERTY(EditAnywhere, Config, Category = "SpaceMMO")
	double StartingDropKilometres = 0.05;

	/**
	 * Whether to leave a ship on the ground for the player to board.
	 *
	 * Scaffolding until 115 makes summoning real, and deliberately a separate flag so that turning
	 * it off is how "nobody starts with a ship" gets tested before the questline exists.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "SpaceMMO")
	bool bSpawnStarterShip = true;

private:
	void SpawnTestScene();

	/** How far terrain can rise above the nominal radius, so a drop clears the hills. */
	static double MaxTerrainRise();
};
