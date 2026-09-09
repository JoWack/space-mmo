#include "SpaceMMOOnFootReadout.h"

#include "Components/TextBlock.h"
#include "SpaceMMOBackendClient.h"
#include "SpaceMMOBackendLog.h"
#include "SpaceMMODockingComponent.h"
#include "SpaceMMOStationMarkers.h"
#include "SpaceMMOPlayerController.h"

FSpaceMMOOnFootReadoutText USpaceMMOOnFootReadout::Build(
	const FString& CharacterName,
	const FString& Balance,
	const FString& StationLine)
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

	// Passed through already worded rather than formatted here, because the ship's readout says the
	// same thing and the two must not drift -- FSpaceMMOStationLine owns the sentence.
	Text.Station = StationLine;

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

	// The same selection the chevrons draw from, asked of the pawn's docking component -- which is
	// on the character as well as the ship, which is what lets the marker persist on foot at all.
	FString StationLine;

	if (const APawn* const Pawn = Controller->GetPawn())
	{
		if (const USpaceMMODockingComponent* const Docking =
			Pawn->FindComponentByClass<USpaceMMODockingComponent>())
		{
			TArray<FSpaceMMOStationMarkerView> Markers;
			int32 Named = INDEX_NONE;

			if (Docking->BuildStationMarkers(Markers, Named) && Markers.IsValidIndex(Named))
			{
				StationLine = FSpaceMMOStationLine::Format(
					Markers[Named].Name,
					Markers[Named].DistanceKilometres,
					Markers[Named].DockingRangeKilometres,
					Markers[Named].bOnBody,
					Markers[Named].BodyName);
			}
		}
	}

	const FSpaceMMOOnFootReadoutText Text = Build(
		Controller->GetCharacterName(), Controller->GetCharacterBalance(), StationLine);

	auto Set = [](UTextBlock* Block, const FString& Value)
	{
		if (Block != nullptr)
		{
			Block->SetText(FText::FromString(Value));
		}
	};

	Set(NameText, Text.Name);
	Set(CreditsText, Text.Credits);
	Set(StationText, Text.Station);

	// Said once. A Widget Blueprint without a StationText block shows no station line and no error,
	// which is indistinguishable from the marker not working -- and would be looked for in the
	// wrong file.
	if (StationText == nullptr && !Text.Station.IsEmpty() && !bReportedMissingStationBlock)
	{
		bReportedMissingStationBlock = true;

		UE_LOG(LogSpaceMMOBackend, Warning,
			TEXT("The on-foot readout has a station to name (\"%s\") and no StationText block to "
				"put it in. Add a text block called StationText to the on-foot readout's Widget "
				"Blueprint (task 160)."),
			*Text.Station);
	}

	bHasCredits = Text.bHasCredits;
}
