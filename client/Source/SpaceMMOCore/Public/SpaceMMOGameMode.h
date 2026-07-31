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
};
