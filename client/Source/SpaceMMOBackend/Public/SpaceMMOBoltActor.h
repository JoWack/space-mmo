#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SpaceMMOCoordinates.h"

#include "SpaceMMOBoltActor.generated.h"

/**
 * A laser bolt in flight (task 194): a short, bright streak from the muzzle to where the shot stopped, and a small
 * flash there if it hit somebody. Joe chose it over an instant beam and over nothing, 16 October.
 *
 * Drawn on every client and never replicated: the server multicasts the two ends and each machine draws its own.
 * Held in system space and placed against the render origin every frame, so a rebase mid-flight cannot strand it.
 */
UCLASS()
class SPACEMMOBACKEND_API ASpaceMMOBoltActor : public AActor
{
	GENERATED_BODY()

public:
	ASpaceMMOBoltActor();

	virtual void Tick(float DeltaSeconds) override;

	/** Sends it on its way. */
	void Launch(const FSystemCoordinate& Start, const FSystemCoordinate& End, bool bHitSomebody);

	/** How fast the streak travels, in metres a second: about a fifth of a second across the rifle's 150 m. */
	static constexpr double SpeedMetresPerSecond = 750.0;

	/** How long the streak is. */
	static constexpr double StreakMetres = 3.0;

	/** How long the flash lasts where it lands. */
	static constexpr double FlashSeconds = 0.15;

protected:
	UPROPERTY(VisibleAnywhere, Category = "SpaceMMO|Combat")
	TObjectPtr<class UStaticMeshComponent> Streak;

	UPROPERTY(VisibleAnywhere, Category = "SpaceMMO|Combat")
	TObjectPtr<class UStaticMeshComponent> Flash;

private:
	void Place();

	FSystemCoordinate StartPosition;

	FSystemCoordinate EndPosition;

	double LengthMetres = 0.0;

	double Elapsed = 0.0;

	bool bHit = false;

	bool bLaunched = false;
};
