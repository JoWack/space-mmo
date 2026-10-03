#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"

#include "SpaceMMONoticeSink.generated.h"

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class USpaceMMONoticeSink : public UInterface
{
	GENERATED_BODY()
};

/**
 * Whatever shows the player a short notice: a dock result, an airspace refusal, a step-out refusal.
 *
 * <strong>Why an interface.</strong> The ship decides these in this module and the message stack that
 * shows them is the Backend's player controller, which this module cannot see. They used to be the
 * engine's orange debug text, top left, for exactly that reason (task 173, Joe 3 October: notices go
 * into the message stack above the player). A ship whose controller is not a sink still falls back to
 * the debug text, so a notice is never simply lost.
 */
class SPACEMMOCORE_API ISpaceMMONoticeSink
{
	GENERATED_BODY()

public:
	/** Says something to the player for a few seconds. Succeeded is an ice edge, otherwise red. */
	virtual void ShowNotice(const FString& Message, bool bSucceeded) {}
};
