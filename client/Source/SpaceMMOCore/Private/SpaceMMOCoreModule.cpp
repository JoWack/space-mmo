#include "Modules/ModuleManager.h"
#include "SpaceMMOLog.h"

DEFINE_LOG_CATEGORY(LogSpaceMMO);

// SpaceMMOCore is the primary game module: coordinates, physics grids, and flight.
IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, SpaceMMOCore, "SpaceMMO");
