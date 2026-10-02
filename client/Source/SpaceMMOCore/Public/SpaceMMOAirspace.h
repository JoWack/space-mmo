#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SpaceMMOCoordinates.h"

#include "SpaceMMOAirspace.generated.h"

/**
 * The airspace over a settlement, and the platform under it (task 169).
 *
 * Joe, 2 October: "You should not be able to fly or disembark your ship directly above or inside the
 * confines of the city, only outside and at the docking stations."
 *
 * <strong>Plain numbers in the settlement's own frame</strong>, so the rules below are pure functions
 * with tests and no world: X and Y across the platform, Z up from its floor. The station that carries
 * the settlement fills one in from its building and hands it to USpaceMMOAirspaceSubsystem.
 */
/** A landing pad in a settlement's airspace, where a ship docks (task 169). */
USTRUCT()
struct SPACEMMOCORE_API FSpaceMMOAirspaceBerth
{
	GENERATED_BODY()

	/** The pad's centre, on its surface, in system kilometres. */
	UPROPERTY()
	FSystemCoordinate Pad;

	UPROPERTY()
	double PadRadiusKilometres = 0.0;
};

USTRUCT()
struct SPACEMMOCORE_API FSpaceMMOAirspaceZone
{
	GENERATED_BODY()

	/** The station it belongs to, so it can be replaced and named. */
	UPROPERTY()
	FString Key;

	/** "Borlash", for the messages. */
	UPROPERTY()
	FString Name;

	/** The settlement's origin: the levelled ground at its centre, in system kilometres. */
	UPROPERTY()
	FSystemCoordinate Origin;

	/** The settlement's frame, from its local axes to system axes. */
	UPROPERTY()
	FQuat Rotation = FQuat::Identity;

	/** The walking surface above the origin, in kilometres. */
	UPROPERTY()
	double FloorKilometres = 0.0;

	/** No ship may fly within this of the settlement's vertical axis... */
	UPROPERTY()
	double NoFlyRadiusKilometres = 0.0;

	/** ...below this height above its floor. */
	UPROPERTY()
	double NoFlyCeilingKilometres = 0.0;

	/** Half-width of the square platform a pilot may not step out onto. */
	UPROPERTY()
	double PlatformHalfWidthKilometres = 0.0;

	/** Its landing pads: what the messages count, and where stepping out is answered differently. */
	UPROPERTY()
	TArray<FSpaceMMOAirspaceBerth> Berths;
};

/**
 * The airspace rules themselves. Pure, and the same on a dedicated server as on the client.
 */
class SPACEMMOCORE_API FSpaceMMOAirspace
{
public:
	/** A position in the zone's own frame, in kilometres: across the platform, and up from its floor. */
	static FVector ToLocal(const FSpaceMMOAirspaceZone& Zone, const FSystemCoordinate& Position);

	/** Whether a ship of this radius, here, is inside the no-fly cylinder. */
	static bool IsInNoFly(
		const FSpaceMMOAirspaceZone& Zone,
		const FSystemCoordinate& Position,
		double HullRadiusKilometres);

	/**
	 * Moves a ship out of the no-fly cylinder by the shortest way, and removes the part of its velocity
	 * carrying it back in. A ship that came down through the top goes back up; one that flew into the
	 * side goes back out sideways. Nothing else about its motion changes, so a ship skimming the edge
	 * slides round it rather than stopping dead -- the way ground contact treats a hillside.
	 *
	 * @param InOutVelocity  Centimetres per second, in system axes.
	 * @return True if the ship was inside and was moved.
	 */
	static bool ResolveNoFly(
		const FSpaceMMOAirspaceZone& Zone,
		double HullRadiusKilometres,
		FSystemCoordinate& InOutPosition,
		FVector& InOutVelocity);

	/** Whether a pilot may step out of a ship here: not anywhere over the platform. */
	static bool AllowsStepOut(const FSpaceMMOAirspaceZone& Zone, const FSystemCoordinate& Position);

	/**
	 * Whether a ship here has arrived at a berth: within a little of its landing pad, and not far above
	 * it. A pilot hovering over the pad with the key pressed has arrived; one over the city has not.
	 */
	static bool IsAtBerth(
		const FSystemCoordinate& Pad,
		const FVector& Up,
		double PadRadiusKilometres,
		const FSystemCoordinate& ShipPosition);

	/**
	 * Where a pilot docking at a berth is set down: just inboard of the pad, between it and the dock's
	 * hub, on the dock's floor and half a metre up so the character drops onto it.
	 */
	static FSystemCoordinate PilotArrivalBesidePad(
		const FSystemCoordinate& Pad,
		const FVector& Outward,
		const FVector& Up,
		double PadRadiusKilometres);

	/** How far past a pad's edge a ship has still arrived: the dock's rim beyond the pad. */
	static constexpr double BerthReachKilometres = 0.008;

	/** How high above a pad a ship has still arrived: a ship coming in to land. */
	static constexpr double BerthHeightKilometres = 0.06;

	/** "Borlash's airspace is closed. Dock at one of its four corner docking stations." */
	static FString ClosedAirspaceMessage(const FSpaceMMOAirspaceZone& Zone);

	/** "Ships dock at Borlash's four corner docking stations, not over the city." */
	static FString DockAtBerthsMessage(const FString& Name, int32 BerthCount);

	/** Whether a ship here is at any of the zone's berths. */
	static bool IsAtAnyBerth(const FSpaceMMOAirspaceZone& Zone, const FSystemCoordinate& ShipPosition);

	/**
	 * What a pilot is told when stepping out is refused here: on a landing pad, that ships are not left
	 * there; anywhere else on the platform, where to go instead. Both approved by Joe, 2 October.
	 */
	static FString StepOutRefusal(
		const FSpaceMMOAirspaceZone& Zone, const FSystemCoordinate& ShipPosition, const FString& DockKey);

	/** "You can't step out here. Land at a docking station and press G to dock." */
	static FString NoStepOutMessage(const FString& DockKey);

	/** "Ships can't be left on a landing pad. Press G to dock." */
	static FString PadStepOutMessage(const FString& DockKey);

	/** "four", for counts a sentence reads better spelled. */
	static FString CountWord(int32 Count);
};

/**
 * Every settlement's airspace in this world, set by the stations that carry them and asked by ships.
 *
 * <strong>Here, in Core, because the ship is here.</strong> The settlement and its station belong to
 * the backend module, which Core must never depend on (SpaceMMOBackend.Build.cs), so the zones are
 * handed down as plain numbers rather than looked up from above.
 */
UCLASS()
class SPACEMMOCORE_API USpaceMMOAirspaceSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Adds a zone, or replaces the one with the same key. */
	void SetZone(const FSpaceMMOAirspaceZone& Zone);

	void RemoveZone(const FString& Key);

	const TArray<FSpaceMMOAirspaceZone>& GetZones() const { return Zones; }

private:
	TArray<FSpaceMMOAirspaceZone> Zones;
};
