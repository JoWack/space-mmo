#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SpaceMMOBackendTypes.h"
#include "SpaceMMOPlanet.h"
#include "SpaceMMOPlanetTerrain.h"
#include "SpaceMMODepositActor.generated.h"

/**
 * A deposit standing on a planet's surface.
 *
 * <strong>Not replicated, and not spawned by the game mode.</strong> Where a deposit is, is a pure
 * function of content the server already served and terrain both machines already compute, so every
 * machine can place it identically for free. Replicating it would be paying to transmit a position
 * that both ends can derive — the same reasoning as ADR-0002 and as the scenery in
 * USpaceMMOWorldSubsystem, which was moved off the game mode for exactly this reason after a
 * connected client saw an empty black level.
 *
 * What is <em>in</em> the deposit is a different question entirely, and that one is replicated:
 * quantity remaining changes when somebody mines it, and only the server may say so.
 */
UCLASS()
class SPACEMMOBACKEND_API ASpaceMMODepositActor : public AActor
{
	GENERATED_BODY()

public:
	ASpaceMMODepositActor();

	virtual void BeginPlay() override;

	virtual void Tick(float DeltaSeconds) override;

	/**
	 * Sets everything the actor needs to place itself.
	 *
	 * Call before FinishSpawning. A plain SpawnActor runs BeginPlay immediately, so configuration
	 * applied afterwards arrives too late and the deposit reports — and briefly occupies — the
	 * system origin. That mistake has been made three times in this project already.
	 */
	UFUNCTION(BlueprintCallable, Category = "SpaceMMO|Deposit")
	void Configure(
		const FBackendResourceNode& InNode,
		const FPlanetConfig& InPlanet,
		const FPlanetTerrainConfig& InTerrain);

	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Deposit")
	const FBackendResourceNode& GetNode() const { return Node; }

	/** Where the deposit stands, in system space. Derived, never stored by the server. */
	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Deposit")
	FSystemCoordinate GetSurfacePosition() const { return SurfacePosition; }

private:
	/** Recomputes the Unreal transform from the system position and the current render origin. */
	void ApplyRenderTransform();

	/**
	 * Swaps in the mesh configured for this deposit's material, if there is one.
	 *
	 * Called from Configure, because the item key arrives with the node and the constructor has no
	 * way to know what this deposit will turn out to be.
	 */
	void ApplyConfiguredMesh();

	UPROPERTY()
	TObjectPtr<class UStaticMeshComponent> Marker;

	UPROPERTY()
	FBackendResourceNode Node;

	UPROPERTY()
	FPlanetConfig Planet;

	UPROPERTY()
	FPlanetTerrainConfig Terrain;

	/** Computed once in Configure. The terrain function is deterministic, so it cannot change. */
	FSystemCoordinate SurfacePosition;

	/** Render-origin revision this transform was built for, so rebases are detected. */
	int32 BuiltAtRevision = -1;

	/** Where the height function put the ground, in world space. For -ShowDepositAnchors. */
	FVector AnchorWorldLocation = FVector::ZeroVector;

	/** The surface normal there, so the marker stands the same way the deposit does. */
	FVector AnchorUp = FVector::UpVector;
};
