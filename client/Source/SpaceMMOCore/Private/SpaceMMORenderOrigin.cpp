#include "SpaceMMORenderOrigin.h"

void USpaceMMORenderOriginSubsystem::SetRenderOrigin(const FSystemCoordinate& NewOrigin)
{
	if (NewOrigin == RenderOrigin)
	{
		return;
	}

	RenderOrigin = NewOrigin;
	++Revision;
}
