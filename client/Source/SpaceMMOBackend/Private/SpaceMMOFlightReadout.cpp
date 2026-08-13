#include "SpaceMMOFlightReadout.h"

#include "Components/TextBlock.h"
#include "SpaceMMOShipPawn.h"

namespace
{
	/** Centimetres per second to metres per second, which is what the readout speaks. */
	constexpr double CentimetresPerMetre = 100.0;

	FString MetresPerSecond(const double Centimetres)
	{
		return FString::Printf(TEXT("%.0f m/s"), Centimetres / CentimetresPerMetre);
	}
}

FSpaceMMOFlightReadoutText USpaceMMOFlightReadout::Build(
	const FSpaceMMOFlightReadoutInputs& Inputs)
{
	FSpaceMMOFlightReadoutText Text;

	// Metres up to a kilometre and kilometres beyond it. Landing happens in the last few metres and
	// "0.00 km" cannot show it; a transfer orbit happens over tens of kilometres and "48000 m" is
	// a number nobody reads as a distance.
	const double AltitudeMetres = Inputs.GroundAltitudeKilometres * 1000.0;

	Text.Altitude = FMath::Abs(AltitudeMetres) < 1000.0
		? FString::Printf(TEXT("%.0f m"), AltitudeMetres)
		: FString::Printf(TEXT("%.2f km"), Inputs.GroundAltitudeKilometres);

	Text.Speed = MetresPerSecond(Inputs.SpeedCentimetresPerSecond);

	// Nothing to orbit is a real state — deep space, between bodies — and an orbital speed of
	// "0 m/s" there would read as a ship somehow already in orbit of nothing.
	// Reported through bHasOrbital as well, so a Blueprint can hide the label beside it rather than
	// leaving one dangling over an empty value.
	Text.bHasOrbital = Inputs.OrbitalSpeedCentimetresPerSecond > 0.0;

	// A bare number, like every other value here: labels live in the Widget Blueprint, where they
	// can be reworded and restyled without a rebuild. C++ says what the value is; the designer says
	// what it is called.
	Text.Orbital = Text.bHasOrbital
		? MetresPerSecond(Inputs.OrbitalSpeedCentimetresPerSecond)
		: FString();

	Text.Proximity =
		Inputs.Proximity == EPlanetProximity::Surface ? TEXT("SURFACE")
		: Inputs.Proximity == EPlanetProximity::Atmospheric ? TEXT("ATMOSPHERE")
		: TEXT("ORBIT");

	Text.SystemPosition = Inputs.SystemPosition.ToString();

	// One line, because these are three symptoms of the same thing: whether the coordinate model is
	// behaving. The sphere altitude sits here rather than beside the ground one because they differ
	// by however tall the terrain is, and showing both to a pilot made "Altitude 0.34 km |
	// ATMOSPHERE" look like a contradiction when both halves were true.
	Text.Debug = FString::Printf(
		TEXT("world %s  ·  sphere alt %.2f km  ·  rebases %d"),
		*Inputs.WorldLocationCentimetres.ToCompactString(),
		Inputs.SphereAltitudeKilometres,
		Inputs.RebaseCount);

	return Text;
}

void USpaceMMOFlightReadout::NativeTick(const FGeometry& Geometry, const float DeltaSeconds)
{
	Super::NativeTick(Geometry, DeltaSeconds);

	// Pulled from the pawn rather than pushed by it, so the ship stays unaware that a HUD exists.
	// It already knows about a renderer more than it should.
	const APlayerController* Controller = GetOwningPlayer();

	const ASpaceMMOShipPawn* Ship =
		Controller != nullptr ? Cast<ASpaceMMOShipPawn>(Controller->GetPawn()) : nullptr;

	if (Ship == nullptr)
	{
		// On foot, or between pawns. The readout is about flying, so it says nothing rather than
		// holding the last thing it knew — a frozen altitude is worse than none.
		SetVisibility(ESlateVisibility::Collapsed);

		return;
	}

	SetVisibility(ESlateVisibility::HitTestInvisible);

	FSpaceMMOFlightReadoutInputs Inputs;
	Inputs.SystemPosition = Ship->GetSystemPosition();
	Inputs.SpeedCentimetresPerSecond =
		Ship->GetSpeedKilometresPerSecond() * SpaceMMO::Coordinates::CentimetresPerKilometre;
	Inputs.OrbitalSpeedCentimetresPerSecond = Ship->GetOrbitalSpeedHere();
	Inputs.GroundAltitudeKilometres = Ship->GetGroundAltitudeKilometres();
	Inputs.SphereAltitudeKilometres = Ship->GetAltitudeKilometres();
	Inputs.Proximity = Ship->GetProximity();
	Inputs.WorldLocationCentimetres = Ship->GetActorLocation();
	Inputs.RebaseCount = Ship->GetRebaseCount();

	// Follows the ship rather than being set once at creation, so toggling it takes effect without
	// restarting, and one switch governs the whole debugging session.
	bShowDebug = Ship->ShowsFlightDebug();

	const FSpaceMMOFlightReadoutText Text = Build(Inputs);

	auto Set = [](UTextBlock* Block, const FString& Value)
	{
		if (Block != nullptr)
		{
			Block->SetText(FText::FromString(Value));
		}
	};

	Set(AltitudeText, Text.Altitude);
	Set(SpeedText, Text.Speed);
	Set(OrbitalText, Text.Orbital);
	Set(ProximityText, Text.Proximity);
	Set(SystemPositionText, Text.SystemPosition);
	Set(DebugText, bShowDebug ? Text.Debug : FString());

	bHasOrbitalSpeed = Text.bHasOrbital;
}
