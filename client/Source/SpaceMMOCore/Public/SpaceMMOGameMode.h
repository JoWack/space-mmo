#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "SpaceMMOGameMode.generated.h"

/**
 * Spawns the player into a ship.
 *
 * A placeholder for the real flow, in which a character is created on their race's starting planet
 * and does not own a ship until they have built one (design-bible §4). For now it exists so that
 * pressing Play produces something flyable rather than an empty camera.
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

private:
	void SpawnTestScene();
};
