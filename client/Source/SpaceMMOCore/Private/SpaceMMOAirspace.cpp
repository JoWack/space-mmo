#include "SpaceMMOAirspace.h"

#include "SpaceMMOLog.h"
#include "SpaceMMOSurfaces.h"

FVector FSpaceMMOAirspace::ToLocal(const FSpaceMMOAirspaceZone& Zone, const FSystemCoordinate& Position)
{
	const FVector Local = Zone.Rotation.UnrotateVector(Position.Kilometres - Zone.Origin.Kilometres);

	return FVector(Local.X, Local.Y, Local.Z - Zone.FloorKilometres);
}

bool FSpaceMMOAirspace::IsInNoFly(
	const FSpaceMMOAirspaceZone& Zone,
	const FSystemCoordinate& Position,
	const double HullRadiusKilometres)
{
	if (Zone.NoFlyRadiusKilometres <= 0.0)
	{
		return false;
	}

	const FVector Local = ToLocal(Zone, Position);
	const double Hull = FMath::Max(0.0, HullRadiusKilometres);

	return FVector2D(Local.X, Local.Y).Size() < Zone.NoFlyRadiusKilometres + Hull
		&& Local.Z < Zone.NoFlyCeilingKilometres + Hull;
}

bool FSpaceMMOAirspace::ResolveNoFly(
	const FSpaceMMOAirspaceZone& Zone,
	const double HullRadiusKilometres,
	FSystemCoordinate& InOutPosition,
	FVector& InOutVelocity)
{
	if (!IsInNoFly(Zone, InOutPosition, HullRadiusKilometres))
	{
		return false;
	}

	const double Hull = FMath::Max(0.0, HullRadiusKilometres);
	const FVector Local = ToLocal(Zone, InOutPosition);
	const double Across = FVector2D(Local.X, Local.Y).Size();

	// Put a hair past the edge rather than on it, as ground contact does. Exactly on it, the trip back
	// through the city's frame lands a ship a nanometre inside as often as outside, and one hovering
	// at the edge was told the airspace was closed every few seconds without moving.
	const double Skin =
		SpaceMMO::Surfaces::SeparationCentimetres(0.0) / SpaceMMO::Coordinates::CentimetresPerKilometre;

	// How far it would have to move to be out through the side, and through the top. The shorter
	// is the way it came in: nobody crosses a hundred metres of a cylinder's wall in one frame.
	const double ThroughSide = (Zone.NoFlyRadiusKilometres + Hull) - Across;
	const double ThroughTop = (Zone.NoFlyCeilingKilometres + Hull) - Local.Z;

	FVector Out;
	FVector Normal;

	if (ThroughTop <= ThroughSide)
	{
		Out = FVector(Local.X, Local.Y, Zone.NoFlyCeilingKilometres + Hull + Skin);
		Normal = FVector::UpVector;
	}
	else
	{
		// Straight out from the axis. A ship exactly on it has no "out", so it leaves along the
		// settlement's +X rather than not at all.
		const FVector2D Radial = Across > UE_DOUBLE_SMALL_NUMBER
			? FVector2D(Local.X, Local.Y) / Across
			: FVector2D(1.0, 0.0);

		Out = FVector(
			Radial.X * (Zone.NoFlyRadiusKilometres + Hull + Skin),
			Radial.Y * (Zone.NoFlyRadiusKilometres + Hull + Skin),
			Local.Z);
		Normal = FVector(Radial.X, Radial.Y, 0.0);
	}

	InOutPosition = FSystemCoordinate(
		Zone.Origin.Kilometres
		+ Zone.Rotation.RotateVector(FVector(Out.X, Out.Y, Out.Z + Zone.FloorKilometres)));

	// Only the part carrying it back in. Reflecting it would bounce a ship off the air, and removing
	// all of it would freeze a ship that only grazed the edge.
	const FVector LocalVelocity = Zone.Rotation.UnrotateVector(InOutVelocity);
	const double Inward = FVector::DotProduct(LocalVelocity, Normal);

	if (Inward < 0.0)
	{
		InOutVelocity = Zone.Rotation.RotateVector(LocalVelocity - Normal * Inward);
	}

	return true;
}

bool FSpaceMMOAirspace::AllowsStepOut(const FSpaceMMOAirspaceZone& Zone, const FSystemCoordinate& Position)
{
	if (Zone.PlatformHalfWidthKilometres <= 0.0)
	{
		return true;
	}

	const FVector Local = ToLocal(Zone, Position);

	// Over the platform at any height a ship could be resting at; a ship high above it is flying,
	// and the ground rule refuses that already.
	const bool bOverPlatform = FMath::Abs(Local.X) <= Zone.PlatformHalfWidthKilometres
		&& FMath::Abs(Local.Y) <= Zone.PlatformHalfWidthKilometres;

	return !bOverPlatform;
}

bool FSpaceMMOAirspace::IsAtBerth(
	const FSystemCoordinate& Pad,
	const FVector& Up,
	const double PadRadiusKilometres,
	const FSystemCoordinate& ShipPosition)
{
	const FVector Normal = Up.GetSafeNormal();
	const FVector Offset = ShipPosition.Kilometres - Pad.Kilometres;
	const double Height = FVector::DotProduct(Offset, Normal);
	const double Across = (Offset - (Normal * Height)).Size();

	return Across <= PadRadiusKilometres + BerthReachKilometres
		&& Height >= -BerthReachKilometres
		&& Height <= BerthHeightKilometres;
}

FSystemCoordinate FSpaceMMOAirspace::PilotArrivalBesidePad(
	const FSystemCoordinate& Pad,
	const FVector& Outward,
	const FVector& Up,
	const double PadRadiusKilometres)
{
	// A metre and a half inboard of the pad's edge: on the dock, clear of the hub's wall beyond it.
	constexpr double InboardKilometres = 0.0015;
	constexpr double LiftKilometres = 0.0005;

	return FSystemCoordinate(
		Pad.Kilometres
		- (Outward.GetSafeNormal() * (PadRadiusKilometres + InboardKilometres))
		+ (Up.GetSafeNormal() * LiftKilometres));
}

FString FSpaceMMOAirspace::CountWord(const int32 Count)
{
	static const TCHAR* const Words[] = {
		TEXT("no"), TEXT("one"), TEXT("two"), TEXT("three"), TEXT("four"), TEXT("five"), TEXT("six"),
		TEXT("seven"), TEXT("eight")};

	return Count >= 0 && Count < UE_ARRAY_COUNT(Words) ? FString(Words[Count]) : FString::FromInt(Count);
}

FString FSpaceMMOAirspace::ClosedAirspaceMessage(const FSpaceMMOAirspaceZone& Zone)
{
	// Wording approved by Joe on 2 October (task 169).
	return FString::Printf(
		TEXT("%s's airspace is closed. Dock at one of its %s corner docking stations."),
		*Zone.Name, *CountWord(Zone.Berths.Num()));
}

FString FSpaceMMOAirspace::DockAtBerthsMessage(const FString& Name, const int32 BerthCount)
{
	return FString::Printf(
		TEXT("Ships dock at %s's %s corner docking stations, not over the city."),
		*Name, *CountWord(BerthCount));
}

FString FSpaceMMOAirspace::NoStepOutMessage(const FString& DockKey)
{
	return FString::Printf(
		TEXT("You can't step out here. Land at a docking station and press %s to dock."), *DockKey);
}

FString FSpaceMMOAirspace::PadStepOutMessage(const FString& DockKey)
{
	// Wording approved by Joe on 2 October, for a pilot already on a pad: the over-the-city sentence told
	// them to land at a docking station while they were standing on one.
	return FString::Printf(TEXT("Ships can't be left on a landing pad. Press %s to dock."), *DockKey);
}

bool FSpaceMMOAirspace::IsAtAnyBerth(const FSpaceMMOAirspaceZone& Zone, const FSystemCoordinate& ShipPosition)
{
	const FVector Up = Zone.Rotation.RotateVector(FVector::UpVector);

	for (const FSpaceMMOAirspaceBerth& Berth : Zone.Berths)
	{
		if (IsAtBerth(Berth.Pad, Up, Berth.PadRadiusKilometres, ShipPosition))
		{
			return true;
		}
	}

	return false;
}

FString FSpaceMMOAirspace::StepOutRefusal(
	const FSpaceMMOAirspaceZone& Zone, const FSystemCoordinate& ShipPosition, const FString& DockKey)
{
	return IsAtAnyBerth(Zone, ShipPosition) ? PadStepOutMessage(DockKey) : NoStepOutMessage(DockKey);
}

void USpaceMMOAirspaceSubsystem::SetZone(const FSpaceMMOAirspaceZone& Zone)
{
	RemoveZone(Zone.Key);
	Zones.Add(Zone);

	// Said once per zone, with its size, because an invisible wall nobody can see the edge of is
	// exactly the kind of thing a playtest reports as "my ship stopped for no reason".
	UE_LOG(LogSpaceMMO, Log,
		TEXT("Airspace over %s (%s): no flying within %.0f m of its centre below %.0f m; no stepping "
			"out over its %.0f m platform."),
		*Zone.Name, *Zone.Key, Zone.NoFlyRadiusKilometres * 1000.0, Zone.NoFlyCeilingKilometres * 1000.0,
		Zone.PlatformHalfWidthKilometres * 2000.0);
}

void USpaceMMOAirspaceSubsystem::RemoveZone(const FString& Key)
{
	Zones.RemoveAll([&Key](const FSpaceMMOAirspaceZone& Zone) { return Zone.Key == Key; });
}
