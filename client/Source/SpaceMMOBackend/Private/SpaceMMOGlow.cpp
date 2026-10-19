#include "SpaceMMOGlow.h"

#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"

namespace SpaceMMO::Glow
{
	UMaterialInterface* Material()
	{
		return LoadObject<UMaterialInterface>(
			nullptr, TEXT("/Game/LevelPrototyping/Interactable/JumpPad/Assets/Materials/M_SimpleGlow.M_SimpleGlow"));
	}

	void SetColour(UStaticMeshComponent* Part, const FLinearColor& Colour)
	{
		if (Part == nullptr)
		{
			return;
		}

		Part->SetCustomPrimitiveDataFloat(SwitchIndex, 1.0f);
		Part->SetCustomPrimitiveDataVector4(ColourIndex, FVector4(Colour.R, Colour.G, Colour.B, Colour.A));
	}

	void Apply(UStaticMeshComponent* Part, const FLinearColor& Colour)
	{
		if (Part == nullptr)
		{
			return;
		}

		if (UMaterialInterface* Glow = Material())
		{
			Part->SetMaterial(0, Glow);
		}

		SetColour(Part, Colour);
	}
}
