#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SpaceMMOBackendTypes.h"
#include "SpaceMMOPlanet.h"
#include "SpaceMMOPlanetTerrain.h"
#include "SpaceMMOStationActor.generated.h"

class UStaticMeshComponent;
struct FSpaceMMODockMark;

/**
 * A station in the world, wherever the server said it is.
 *
 * <strong>Placed two ways, and told which.</strong> A station on a body is put on the ground by
 * evaluating the same height function the terrain mesh and the physics use, so it stands on the
 * surface rather than at a transmitted altitude that could disagree with it. A station that
 * orbits nothing is placed at its system coordinate directly, because there is no ground to
 * stand on.
 *
 * Like everything with a system coordinate, its Unreal transform is recomputed against the render
 * origin rather than stored, so it holds still while the ship flies past and the origin jumps
 * beneath it.
 */
UCLASS()
class SPACEMMOBACKEND_API ASpaceMMOStationActor : public AActor
{
	GENERATED_BODY()

public:
	ASpaceMMOStationActor();

	virtual void Tick(float DeltaSeconds) override;

	/**
	 * Works out where this station stands and moves it there.
	 *
	 * The planet is passed in rather than looked up so that a station on a body cannot be placed
	 * against a different planet's radius than the one it is standing on.
	 */
	void Configure(
		const FBackendStation& InStation,
		const FPlanetConfig& InPlanet,
		const FPlanetTerrainConfig& InTerrain);

	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Station")
	const FBackendStation& GetStation() const { return Station; }

	/** Where it ended up, in system space. */
	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Station")
	FSystemCoordinate GetSystemPosition() const { return SystemPosition; }

	/**
	 * Whether a point is close enough to dock.
	 *
	 * Pure and static so the same question is answered identically here and anywhere else that
	 * needs it — a client that drew "dock available" on a different rule than the server enforces
	 * would offer a button that refuses.
	 */
	static bool IsWithinDockingRange(
		const FBackendStation& Station,
		const FSystemCoordinate& StationPosition,
		const FSystemCoordinate& Position);

	/**
	 * A patch of ground beside this station, for putting something down next to it.
	 *
	 * <strong>The ground is asked where it is, rather than assumed to be at the station's own
	 * height</strong>: a station sits on the terrain under it, and thirty metres away the terrain is
	 * somewhere else. Any tangent direction will do — there is no side of a station that is its
	 * front — so a world axis is a perfectly good thing to offset along.
	 *
	 * False when no planet is under it, which is not a fault: Deepdock orbits nothing, and the
	 * honest answer for a deep-space station is that there is nowhere to stand. Both callers treat
	 * it as a decision rather than an error — a summoned ship stays in its hangar, and a docked one
	 * stays alongside (task 153).
	 */
	bool GroundPositionBeside(
		double OffsetKilometres,
		double LiftKilometres,
		FSystemCoordinate& OutPosition) const;

	/**
	 * The settlement this station is drawn as, once its Blueprint has been built -- Borlash -- or null
	 * for a station drawn as a single building.
	 */
	const class ASpaceMMOSettlementActor* GetSettlement() const;

	/** How many berths this station's settlement has. Zero means ships dock by range, as everywhere else. */
	int32 GetBerthCount() const;

	/** Its landing pads as the flight readout names them (task 169). Empty for every other station. */
	void GetDockMarks(TArray<FSpaceMMODockMark>& OutDocks) const;

	/**
	 * Whether a ship here is on one of this station's berths (task 169).
	 *
	 * Within a little of the pad and not far above it: a ship hovering over its landing pad has
	 * arrived, and one over the city has not.
	 */
	bool IsAtBerth(const FSystemCoordinate& ShipPosition) const;

	/**
	 * Where a pilot docking here is set down, near where their ship was.
	 *
	 * At a settlement, on the dock of the nearest berth, between its pad and its hub -- not at the
	 * station's own position, which for Borlash is the middle of the city. Anywhere else, beside the
	 * station as before.
	 */
	bool PilotArrivalNear(const FSystemCoordinate& Near, double OffsetKilometres, FSystemCoordinate& OutPosition) const;

	/** Where a summoned ship is stood: on the nearest berth's pad at a settlement, beside the station elsewhere. */
	bool ShipPlacementNear(
		const FSystemCoordinate& Near, double OffsetKilometres, double LiftKilometres, FSystemCoordinate& OutPosition) const;

protected:
	virtual void BeginPlay() override;

	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	/**
	 * Puts the configured building on the station: a Blueprint if one is named, otherwise a mesh,
	 * otherwise the placeholder cube it was constructed with.
	 */
	void ApplyConfiguredLook();

	/**
	 * The assembled building, when a kind names a Blueprint rather than a mesh.
	 *
	 * A child actor component rather than a spawned actor kept in a pointer, because it already
	 * solves the three things doing it by hand gets wrong: it spawns the class, attaches it, and
	 * destroys it with this actor. Configure runs before FinishSpawning, which is the worst moment
	 * to be spawning something else.
	 */
	UPROPERTY(VisibleAnywhere, Category = "SpaceMMO|Station")
	TObjectPtr<class UChildActorComponent> Structure;

	void ApplyRenderTransform();

	FBackendStation Station;

	FPlanetConfig Planet;

	FPlanetTerrainConfig Terrain;

	FSystemCoordinate SystemPosition;

	/** How far to raise the mesh so its base rests on the ground rather than its middle. */
	double BaseLiftCentimetres = 0.0;

	UPROPERTY(VisibleAnywhere, Category = "SpaceMMO|Station")
	TObjectPtr<UStaticMeshComponent> Hull;

	/** Render-origin revision the transform was last built against. */
	int32 BuiltAtRevision = -1;

	/** A berth's pad, in system space, with the way out from the settlement there. */
	struct FBerthPlace
	{
		FSystemCoordinate Pad;
		FVector Outward = FVector::ZeroVector;
		double PadRadiusKilometres = 0.0;
		FString ShortName;
	};

	/** Every berth's pad in system space. Empty until the settlement exists, and for every other station. */
	TArray<FBerthPlace> BerthPlaces() const;

	/** Index of the berth nearest a position, or INDEX_NONE. */
	static int32 NearestBerth(const TArray<FBerthPlace>& Berths, const FSystemCoordinate& Near);

	/**
	 * Hands the settlement's airspace to USpaceMMOAirspaceSubsystem once its Blueprint exists (task 169).
	 *
	 * From Tick, not Configure: Configure runs before FinishSpawning, when the child actor carrying the
	 * settlement has not been built, and its berths are components on that actor.
	 */
	void RegisterSettlementAirspace();

	bool bSettlementRegistered = false;

	/**
	 * Whether the draw-state line has been said yet.
	 *
	 * Said once, from Tick, after the render proxy exists -- on frame one it does not, and a
	 * report taken then reads "has proxy 0" for every station in the world. The same measurement
	 * the terrain patch keeps (task 84): what is actually on the component, not what configured it.
	 */
	bool bReportedDrawState = false;
};
