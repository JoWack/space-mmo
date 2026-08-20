#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"

/**
 * Log category for the world authoring tools.
 *
 * Its own category for the same reason LogSpaceMMO has one: what the tool read, what it decided
 * had moved, and what it wrote have to be greppable out of an editor log that is otherwise tens of
 * thousands of engine lines. Every write says what it did and to which file.
 */
SPACEMMOAUTHORING_API DECLARE_LOG_CATEGORY_EXTERN(LogSpaceMMOAuthoring, Log, All);
