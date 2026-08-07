#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "SpaceMMOCoordinates.h"
#include "SpaceMMODockingComponent.generated.h"

/**
 * Docks and undocks the pawn it is attached to.
 *
 * <strong>The decision is the server's, always.</strong> Pressing the key sends an intent and
 * nothing else — no station, no position, no claim about being in range. The server knows where
 * every ship is because it moved them, and it is the only party that can answer "is this player
 * actually alongside that station" (ADR-0003). The same shape as gathering, for the same reason.
 *
 * It also undocks on its own when a ship leaves. Without that, docked is a state you enter and
 * never exit, and the market gate it feeds would mean nothing after the first visit.
 */
UCLASS(ClassGroup = (SpaceMMO), meta = (BlueprintSpawnableComponent))
class SPACEMMOBACKEND_API USpaceMMODockingComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USpaceMMODockingComponent();

	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	/** Binds the dock key to whatever input component the pawn ends up with. */
	void BindInput(UInputComponent* InputComponent);

	/** Which character this pawn acts for. Zero until the player is identified. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Docking")
	int32 CharacterId = 0;

	/** The station this component last asked the backend to dock at, or zero. */
	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Docking")
	int32 GetDockedStationId() const { return DockedStationId; }

protected:
	virtual void BeginPlay() override;

private:
	/** One key, toggling. Docking when docked and undocking when not are both no-ops worth avoiding. */
	void RequestToggleDock();

	UFUNCTION(Server, Reliable)
	void ServerToggleDock();

	/** Tells the player what happened, since the backend's answer arrives out of band. */
	UFUNCTION(Client, Reliable)
	void ClientDockResult(const FString& Message, bool bSucceeded);

	/** The nearest station this pawn is within docking range of, or null. */
	class ASpaceMMOStationActor* FindStationInRange() const;

	/** Where this pawn is, in system space, or false if it cannot be worked out. */
	bool TryGetSystemPosition(FSystemCoordinate& OutPosition) const;

	/** Server-side only. Which station we believe this character is at. */
	int32 DockedStationId = 0;

	/**
	 * How often the server re-checks that a docked ship is still alongside.
	 *
	 * Not every frame: this walks the station actors, and the answer only changes at the speed a
	 * ship flies. A second is far finer than anybody can exploit and far coarser than a tick.
	 */
	static constexpr double RangeCheckSeconds = 1.0;

	double SecondsSinceRangeCheck = 0.0;

	bool bInputBound = false;
};
