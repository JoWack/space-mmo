#pragma once

#include "CoreMinimal.h"

class UMaterialInterface;
class UStaticMeshComponent;

/**
 * The prototyping kit's glow on a mesh, in a colour: the bolt's streak and flash (task 194), a dropped crate's band and
 * beam (task 103).
 *
 * <para>
 * <strong>Its colour is custom primitive data, not a material parameter.</strong> <c>M_SimpleGlow</c>'s
 * <c>Color</c> and its <c>Custom Prim Switch</c> both read the component's custom primitive data (indices 1-4 and
 * 0), so a dynamic instance's <c>SetVectorParameterValue</c> is ignored without a word. Its emissive is then
 * <c>Color</c> where the switch is 1, and the particle colour where it is 0 -- white on a mesh -- and its opacity
 * is <c>Color</c>'s alpha. The bolt set its red-orange that way from 194 on and drew in that grey-white instead; the
 * crate's first render was the same grey, measured off the capture, and read out of the asset with the editor's
 * Python. <c>SpaceMMO.Glow.TheColourGoesWhereTheMaterialReadsIt</c> keeps the indices honest against the asset.
 * </para>
 */
namespace SpaceMMO::Glow
{
	/** Where the material reads its switch: 1 for this colour, 0 for the particle colour. */
	constexpr int32 SwitchIndex = 0;

	/** Where it reads the colour, four floats from here; the alpha is the opacity. */
	constexpr int32 ColourIndex = 1;

	/** The material, or null if the prototyping kit is not in the project. */
	SPACEMMOBACKEND_API UMaterialInterface* Material();

	/** Puts the glow on a part in a colour. */
	SPACEMMOBACKEND_API void Apply(UStaticMeshComponent* Part, const FLinearColor& Colour);

	/** Changes the colour of a part that already glows: dimmer, and with its alpha, fainter. */
	SPACEMMOBACKEND_API void SetColour(UStaticMeshComponent* Part, const FLinearColor& Colour);
}
