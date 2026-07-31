#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"

/**
 * Backend traffic.
 *
 * Its own category, separate from LogSpaceMMO, so network noise can be turned up while debugging
 * a request without drowning out flight and coordinate logging.
 *
 * <strong>Never log a token or a password.</strong> Logs get pasted into bug reports.
 */
SPACEMMOBACKEND_API DECLARE_LOG_CATEGORY_EXTERN(LogSpaceMMOBackend, Log, All);
