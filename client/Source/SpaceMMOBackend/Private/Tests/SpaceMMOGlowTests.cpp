#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameters.h"
#include "Misc/AutomationTest.h"
#include "SpaceMMOGlow.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The glow's colour goes where the material reads it (tasks 103 and 194), measured against the asset itself.
 *
 * The bolt set its colour on a material instance from 194 on, which <c>M_SimpleGlow</c> never reads -- its colour and
 * its switch are custom primitive data -- and every check anyone made passed, because none of them looked at what the
 * material takes. This reads the parameters out of the loaded asset, and what a part holds once it glows.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOGlowColourGoesWhereReadTest,
	"SpaceMMO.Glow.TheColourGoesWhereTheMaterialReadsIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOGlowColourGoesWhereReadTest::RunTest(const FString& Parameters)
{
	UMaterialInterface* Glow = SpaceMMO::Glow::Material();

	if (!TestNotNull(TEXT("The glow loads"), Glow))
	{
		return false;
	}

	FMaterialParameterMetadata Colour;
	FMaterialParameterMetadata Switch;

	if (TestTrue(TEXT("It has a Color"),
		Glow->GetParameterValue(EMaterialParameterType::Vector, FMemoryImageMaterialParameterInfo(TEXT("Color")), Colour)))
	{
		TestEqual(TEXT("Color is read from the part, at the index written"), Colour.PrimitiveDataIndex, SpaceMMO::Glow::ColourIndex);
	}

	if (TestTrue(TEXT("It has a switch"),
		Glow->GetParameterValue(
			EMaterialParameterType::Scalar, FMemoryImageMaterialParameterInfo(TEXT("Custom Prim Switch")), Switch)))
	{
		TestEqual(TEXT("The switch is read from the part, at the index written"), Switch.PrimitiveDataIndex, SpaceMMO::Glow::SwitchIndex);
	}

	// And a part that glows holds the colour there, with the switch thrown towards it.
	UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>();
	SpaceMMO::Glow::Apply(Part, FLinearColor(6.0f, 1.2f, 0.25f, 0.5f));

	const TArray<float>& Data = Part->GetCustomPrimitiveData().Data;

	if (TestTrue(TEXT("Five floats written"), Data.Num() >= SpaceMMO::Glow::ColourIndex + 4))
	{
		TestEqual(TEXT("The switch on"), Data[SpaceMMO::Glow::SwitchIndex], 1.0f);
		TestEqual(TEXT("Red"), Data[SpaceMMO::Glow::ColourIndex], 6.0f);
		TestEqual(TEXT("Green"), Data[SpaceMMO::Glow::ColourIndex + 1], 1.2f);
		TestEqual(TEXT("Blue"), Data[SpaceMMO::Glow::ColourIndex + 2], 0.25f);
		TestEqual(TEXT("Alpha, the opacity"), Data[SpaceMMO::Glow::ColourIndex + 3], 0.5f);
	}

	TestEqual(TEXT("The glow is the part's material"), Part->GetMaterial(0), Glow);

	return true;
}

#endif
