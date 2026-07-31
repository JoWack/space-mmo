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
