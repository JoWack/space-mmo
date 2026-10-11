#pragma once

#include "CoreMinimal.h"
#include "SpaceMMOBackendTypes.h"

class UTextBlock;

/**
 * The coloured tag after a good's name that says which world's look it has (task 187).
 *
 * <strong>The arrangement Joe approved on 10 October:</strong> a short tag after the name on inventory,
 * hangar, industry and market rows -- <c>[TER]</c>, <c>[MAR]</c>, <c>[VER]</c>, <c>[GRM]</c> -- in that
 * world's colour from the Visual Bible. Ores carry their world's tag; anything Common carries none, the
 * same as scrap.
 *
 * <strong>Colours.</strong> Terra's sky blue, Ares' oxide pushed towards red, Verdance's bioluminescent
 * jade, Grimhold's ember. Ares' oxide and Grimhold's ember are both orange in the Visual Bible, so
 * Verdance gives up amber for jade to keep four hues apart.
 */
namespace SpaceMMO::ItemStyleTag
{
	/** "[TER]", "[MAR]", "[VER]", "[GRM]", or empty for Common. */
	SPACEMMOBACKEND_API FString Text(EBackendItemStyle World);

	/** The style's colour. Common has none worth drawing and answers the ordinary text colour. */
	SPACEMMOBACKEND_API FLinearColor Colour(EBackendItemStyle World);

	/** Whether a style is shown at all. Common is not. */
	inline bool IsShown(const EBackendItemStyle World) { return World != EBackendItemStyle::Common; }

	/**
	 * Fills a tag text: its words, its colour, and collapsed for Common so it leaves no gap. A null text
	 * is ignored, so a row whose Blueprint has no tag yet still draws without one.
	 */
	SPACEMMOBACKEND_API void Apply(UTextBlock* Tag, EBackendItemStyle World);
}
