#pragma once

#include "Components/SceneComponent.h"
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "SpaceMMOSettlement.generated.h"

/**
 * Where a ship may dock at a settlement: one landing pad (task 169).
 *
 * <strong>Local geometry, like a door.</strong> The importer places one at each pad from the city's
 * manifest, relative to the settlement, so the Blueprint still never knows where it is in the world
 * (task 124's rule): the station's authored direction places the settlement, and the settlement says
 * where on itself a ship comes down.
 */
UCLASS(ClassGroup = (SpaceMMO), meta = (BlueprintSpawnableComponent))
class SPACEMMOBACKEND_API USpaceMMOBerthComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	/** "dock_ne", from the manifest. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpaceMMO|Settlement")
	FString BerthKey;

	/** "Docking Station NE". */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpaceMMO|Settlement")
	FString DisplayName;

	/** "NE dock": what the flight readout calls it after the city's name, "Borlash NE dock" (task 169). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpaceMMO|Settlement")
	FString ShortName;

	/** The landing pad's radius, in metres: a ship docks within this of the component. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpaceMMO|Settlement")
	double PadRadiusMetres = 12.0;

	/** The dock platform's centre, relative to the settlement actor, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpaceMMO|Settlement")
	FVector DockCentre = FVector::ZeroVector;

	/** The dock platform's radius, in metres. A ship docks on the pad; nobody steps out of one here. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpaceMMO|Settlement")
	double DockRadiusMetres = 32.0;
};

/**
 * A place a player stands to use one of a building's services: a market terminal, an industry bay,
 * a bank counter (task 170).
 *
 * Carried now so the Blueprint is whole the day 170 reads it; nothing reads it yet. Services the game
 * does not have are marked unavailable with the reason, rather than left out (task 171).
 */
UCLASS(ClassGroup = (SpaceMMO), meta = (BlueprintSpawnableComponent))
class SPACEMMOBACKEND_API USpaceMMOServicePointComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	/** The building it is in: "Market", "RefiningHall", "Dock_NE". */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpaceMMO|Settlement")
	FString Building;

	/** "market", "industry", "storage", "quests", "ships", or one the game does not have yet. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpaceMMO|Settlement")
	FString Service;

	/** "Market terminal 3". */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpaceMMO|Settlement")
	FString Label;

	/** How far from the component a player may be and still use it, in metres. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpaceMMO|Settlement")
	double ReachMetres = 2.5;

	/** For industry: which skills' recipes run here. Empty for everything else. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpaceMMO|Settlement")
	TArray<FString> Skills;

	/** False for a service that is drawn but does not exist yet; Note says why. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpaceMMO|Settlement")
	bool bAvailable = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpaceMMO|Settlement")
	FString Note;
};

/**
 * A walled settlement drawn for one station: Borlash, on the Capital (tasks 167-170).
 *
 * <strong>The parent of a generated Blueprint, not something placed by hand.</strong>
 * USpaceMMOImportSettlementsCommandlet builds BP_Station_Borlash from the city's FBX and manifest:
 * one static mesh component per mesh, a berth per landing pad, a service point per counter, and the
 * numbers below. Regenerate it rather than editing it; anything hand-made belongs in a child
 * Blueprint, which the station's BlueprintsByKey can then name instead.
 *
 * Its origin is the levelled ground at the city's centre (task 168's pad), and the walking surface is
 * FloorMetres above that, so the terrain is never in the plane of the paving.
 */
UCLASS(Blueprintable)
class SPACEMMOBACKEND_API ASpaceMMOSettlementActor : public AActor
{
	GENERATED_BODY()

public:
	ASpaceMMOSettlementActor();

	/** The walking surface above this actor's origin, in metres. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpaceMMO|Settlement")
	double FloorMetres = 0.0;

	/** Ships may not fly within this many metres of the origin (task 169). Zero for no zone. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpaceMMO|Settlement")
	double NoFlyRadiusMetres = 0.0;

	/** How far above the floor the no-fly zone reaches, in metres. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpaceMMO|Settlement")
	double NoFlyCeilingMetres = 0.0;

	/** Half-width of the square platform a pilot may not step out onto, except at a dock. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpaceMMO|Settlement")
	double PlatformHalfWidthMetres = 0.0;

	/** The berths on this settlement, in the order the components were authored. */
	void GetBerths(TArray<const USpaceMMOBerthComponent*>& OutBerths) const;

	/** The service points on this settlement. */
	void GetServicePoints(TArray<const USpaceMMOServicePointComponent*>& OutPoints) const;
};
