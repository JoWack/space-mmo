#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"

/**
 * Log category for everything in SpaceMMOCore.
 *
 * A category of our own rather than LogTemp, so `LogSpaceMMO` can be filtered on in a log that is
 * otherwise tens of thousands of engine lines — which is the difference between being able to
 * verify runtime behaviour headlessly and not.
 */
SPACEMMOCORE_API DECLARE_LOG_CATEGORY_EXTERN(LogSpaceMMO, Log, All);
