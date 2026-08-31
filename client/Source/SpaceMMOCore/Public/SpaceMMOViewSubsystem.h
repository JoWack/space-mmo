#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SpaceMMOViewSubsystem.generated.h"

/**
 * How far back each camera was left, across the pawns that come and go.
 *
 * <strong>A character pawn does not survive being boarded.</strong> Stepping into a ship possesses
 * the ship and destroys the character; stepping out spawns a new one. Anything remembered on the
 * pawn is therefore forgotten every time somebody flies anywhere, and a zoom level that reset itself
 * on every trip would be worse than one that never zoomed.
 *
 * On the game instance rather than the player controller because it is per client and outlives
 * everything: it is a preference about how somebody likes to look at the game.
 *
 * Never replicated, never consulted by the simulation. `design-bible.md` §8: "the camera is a client
 * concern only -- it must never affect server-side validation, which is why interaction range is
 * checked against the pawn, never the camera."
 */
UCLASS()
class SPACEMMOCORE_API USpaceMMOViewSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** How far back the camera sits behind a character on foot, in centimetres. */
	UPROPERTY(BlueprintReadWrite, Category = "SpaceMMO|View")
	double CharacterArmCentimetres = 400.0;

	/** And behind a ship. Its own number, because a hull wants a different distance from a body. */
	UPROPERTY(BlueprintReadWrite, Category = "SpaceMMO|View")
	double ShipArmCentimetres = 1200.0;
};
