#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SpaceMMOCoordinates.h"
#include "SpaceMMODeathWire.h"

#include "SpaceMMOLootCrateActor.generated.h"

struct FPlanetConfig;
struct FPlanetTerrainConfig;

/**
 * The dropped half of a death's materials, lying where the body fell (ADR-0016, task 103): a crate anyone may take
 * from for ten minutes.
 *
 * <para>
 * <strong>As Joe chose it, 19 October</strong>: a 0.6 m dark crate with an amber band, a metre beside the body, and a
 * thin amber beam about 15 m tall so it can be found from a distance on a corpse run, fading over its last minute.
 * Engine shapes until there is art.
 * </para>
 * <para>
 * <strong>Spawned by the server and replicated</strong>, unlike a deposit: a deposit is content every machine can
 * place by itself, and this is a fact only the server learns -- that somebody died here. What replicates is where it
 * is in system kilometres, which way is up, when it goes and what is in it; each machine puts it in its own frame
 * from its own render origin, as a deposit does, so the actor's own movement is never replicated.
 * </para>
 * <para>
 * What is in it is the API's (<c>DeathCostService</c>); the server mirrors the answer after every go at it, and
 * destroys the actor when the API says it is empty or gone, or when its time is up.
 * </para>
 */
UCLASS()
class SPACEMMOBACKEND_API ASpaceMMOLootCrateActor : public AActor
{
	GENERATED_BODY()

public:
	ASpaceMMOLootCrateActor();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	virtual void Tick(float DeltaSeconds) override;

	/** The crate's edge. */
	static constexpr double CrateMetres = 0.6;

	/** How tall the beam stands over it. */
	static constexpr double BeamMetres = 15.0;

	/** How long before it is gone the beam starts to fade. */
	static constexpr double BeamFadeSeconds = 60.0;

	/** How far beside the body it is put. */
	static constexpr double BesideMetres = 1.0;

	/** How strongly the beam shows with this long left: full until its last minute, then down to nothing. */
	static float BeamStrength(double SecondsLeft);

	/**
	 * Where the crate goes: a metre to the body's right, on the ground. Pure, for the tests.
	 *
	 * @param Feet Where the body fell, in system kilometres.
	 * @param Right The body's right, so the crate is beside it rather than under it.
	 */
	static FSystemCoordinate BesideTheBody(
		const FPlanetConfig& Planet, const FPlanetTerrainConfig& Terrain, const FSystemCoordinate& Feet, const FVector& Right);

	/**
	 * Server: what the API says it is, counting its time from the server's clock now. Call before FinishSpawning, so
	 * it never stands anywhere else first.
	 */
	void SetContainer(const FBackendLootContainer& Container, const FVector& Up, double ServerNowSeconds);

	/** Server: what is in it now, after a go at it. */
	void SetItems(const TArray<FBackendLootItem>& InItems);

	int64 GetContainerId() const { return ContainerId; }

	const FString& GetDroppedBy() const { return DroppedBy; }

	const TArray<FBackendLootItem>& GetItems() const { return Items; }

	/** Bumped whenever what is in it changes, on every machine, so a window can tell when to redraw. */
	int32 GetItemsRevision() const { return ItemsRevision; }

	FSystemCoordinate GetSystemPosition() const { return FSystemCoordinate(PositionKilometres); }

	/** How long until it is gone, on whichever machine asks, from the server's clock. */
	double SecondsLeft() const;

#if !UE_BUILD_SHIPPING
	/**
	 * SpaceMMOLookPanels Crate and Loot: a crate with sample contents, placed without a server and held beside a
	 * character -- the look harness has its character falling, and a crate left where it was put is soon out of view.
	 *
	 * @param Lasting How long it says it has left, held there: under a minute shows the beam fading.
	 */
	void ShowSampleForLook(const class ASpaceMMOCharacterPawn* Beside, double AheadMetres, double SideMetres, double Lasting);
#endif

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnRep_Placement();

	UFUNCTION()
	void OnRep_Items();

private:
	/** Puts it in this machine's frame: on its first frame, when it moves, and when the render origin rebases. */
	void Place();

	double ServerNowSeconds() const;

	UPROPERTY(Replicated)
	int64 ContainerId = 0;

	UPROPERTY(Replicated)
	FString DroppedBy;

	UPROPERTY(ReplicatedUsing = OnRep_Placement)
	FVector PositionKilometres = FVector::ZeroVector;

	UPROPERTY(ReplicatedUsing = OnRep_Placement)
	FVector Up = FVector::UpVector;

	/** The server's world time it is gone at. */
	UPROPERTY(Replicated)
	double ExpiresServerSeconds = 0.0;

	UPROPERTY(ReplicatedUsing = OnRep_Items)
	TArray<FBackendLootItem> Items;

	int32 ItemsRevision = 0;

	UPROPERTY()
	TObjectPtr<class UStaticMeshComponent> Crate;

	UPROPERTY()
	TObjectPtr<class UStaticMeshComponent> Band;

	UPROPERTY()
	TObjectPtr<class UStaticMeshComponent> Beam;

	/** The render origin's revision this was placed for, so a rebase moves it. */
	int32 PlacedAtRevision = -1;

	bool bPlaced = false;

	/** Whether where it landed has been said: once, not on every move a look sample makes. */
	bool bSaidPlaced = false;

	float ShownBeamStrength = -1.0f;

#if !UE_BUILD_SHIPPING
	TWeakObjectPtr<const class ASpaceMMOCharacterPawn> LookBeside;
	double LookAheadMetres = 0.0;
	double LookSideMetres = 0.0;
	double LookLasting = 0.0;
#endif
};
