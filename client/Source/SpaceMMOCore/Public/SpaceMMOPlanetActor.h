#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SpaceMMOPlanet.h"
#include "SpaceMMOPlanetActor.generated.h"

class UStaticMeshComponent;

/**
 * A planet, drawn from its system-space position.
 *
 * Like everything else with a system coordinate, its Unreal transform is recomputed against the
 * render origin rather than stored — so it holds still while the ship flies past and the origin
 * jumps beneath it.
 *
 * <strong>This is a sphere, not terrain.</strong> It is enough to fly toward, orbit, and measure
 * altitude against, which is what proves the approach transition. An actual landable surface needs
 * runtime cube-sphere LOD terrain, and that is a substantially larger problem deliberately left
 * until the transition around it is known to work.
 */
UCLASS()
class SPACEMMOCORE_API ASpaceMMOPlanetActor : public AActor
{
	GENERATED_BODY()

public:
	ASpaceMMOPlanetActor();

	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Planet")
	FPlanetConfig GetPlanetConfig() const { return Planet; }

	UFUNCTION(BlueprintCallable, Category = "SpaceMMO|Planet")
	void SetPlanetConfig(const FPlanetConfig& NewConfig);

protected:
	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Planet")
	FPlanetConfig Planet;

private:
	void ApplyRenderTransform();

	UPROPERTY(VisibleAnywhere, Category = "SpaceMMO|Planet")
	TObjectPtr<UStaticMeshComponent> Surface;

	/** Render-origin revision the transform was last built against. */
	int32 BuiltAtRevision = -1;
};
