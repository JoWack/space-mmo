#include "SpaceMMOOnFootReadout.h"

#include "Components/TextBlock.h"
#include "SpaceMMOBackendClient.h"
#include "SpaceMMOPlayerController.h"

FSpaceMMOOnFootReadoutText USpaceMMOOnFootReadout::Build(
	const FString& CharacterName,
	const FString& Balance)
{
	FSpaceMMOOnFootReadoutText Text;

	// Upper case here rather than in the Blueprint because UMG has no text transform: a designer can
	// change the font, the colour and the size of this line but cannot change its case. One call to
	// delete if it is ever unwanted.
	Text.Name = CharacterName.IsEmpty() ? TEXT("Not identified") : CharacterName.ToUpper();

	Text.bHasCredits = !Balance.IsEmpty();

	// The unit lives here, like "m/s" on the flight readout. Labels belong to the Blueprint; units
	// are part of the value, and a bare "12,480" beside a name reads as an identifier.
	Text.Credits = Text.bHasCredits ? Balance + TEXT(" cr") : FString();

	return Text;
}

void USpaceMMOOnFootReadout::NativeTick(const FGeometry& Geometry, const float DeltaSeconds)
{
	Super::NativeTick(Geometry, DeltaSeconds);

	// Never call SetVisibility on this widget from here. Slate drives NativeTick from Paint
	// (SWidget.cpp:1505) and arranges children through an EVisibility::Visible filter
	// (SCompoundWidget.cpp:24), so a hidden widget stops ticking and can never show itself again.
	// ASpaceMMOPlayerController::UpdateHudContext owns that decision.
	const ASpaceMMOPlayerController* Controller = Cast<ASpaceMMOPlayerController>(GetOwningPlayer());

	if (Controller == nullptr)
	{
		return;
	}

	const FSpaceMMOOnFootReadoutText Text =
		Build(Controller->GetCharacterName(), Controller->GetCharacterBalance());

	auto Set = [](UTextBlock* Block, const FString& Value)
	{
		if (Block != nullptr)
		{
			Block->SetText(FText::FromString(Value));
		}
	};

	Set(NameText, Text.Name);
	Set(CreditsText, Text.Credits);

	bHasCredits = Text.bHasCredits;
}
