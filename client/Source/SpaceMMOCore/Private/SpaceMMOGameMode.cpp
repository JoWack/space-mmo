#include "SpaceMMOGameMode.h"

#include "SpaceMMOShipPawn.h"

ASpaceMMOGameMode::ASpaceMMOGameMode()
{
	DefaultPawnClass = ASpaceMMOShipPawn::StaticClass();
}
