#include "Modules/ModuleManager.h"
#include "SpaceMMOBackendLog.h"

DEFINE_LOG_CATEGORY(LogSpaceMMOBackend);

// A plain game module, not the primary one — SpaceMMOCore holds that. A project has exactly one
// primary module, and declaring a second is a link error rather than a warning.
IMPLEMENT_MODULE(FDefaultGameModuleImpl, SpaceMMOBackend);
