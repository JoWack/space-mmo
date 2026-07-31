#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "SpaceMMOCoordinates.h"
#include "SpaceMMOFlightModel.h"
#include "SpaceMMOShipPawn.generated.h"

class UCameraComponent;
class USpringArmComponent;
class UStaticMeshComponent;

/**
 * A flyable ship.
 *
 * Holds its authoritative position as an {@link FSystemCoordinate} in kilometres and derives its
 * Unreal world location from it, relative to a movable render origin. Unreal's own transform is
 * therefore a <em>view</em> of the ship's position rather than the truth of it — which is what
 * lets the ship fly a hundred million kilometres without single-precision rendering falling apart
 * (ADR-0001).
 *
 * When the ship drifts far enough from the render origin for physics to start degrading, the
 * origin moves to the ship and the world location resets to near zero. Nothing about the ship's
 * system position changes; only the window onto it does.
 *
 * <strong>Input uses the legacy axis and action mappings</strong> rather than Enhanced Input.
 * Enhanced Input needs InputAction and InputMappingContext assets, which are binary and cannot be
 * authored as text, and the legacy path is still present and unmarked in UE 5.8. Migrating is
 * worth doing once someone opens the editor to make the assets.
 */
UCLASS()
class SPACEMMOCORE_API ASpaceMMOShipPawn : public APawn
{
	GENERATED_BODY()

public:
	ASpaceMMOShipPawn();

	virtual void Tick(float DeltaSeconds) override;

	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	/** The ship's authoritative position, in kilometres of system space. */
	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Ship")
	FSystemCoordinate GetSystemPosition() const { return Navigation.SystemPosition; }

	UFUNCTION(BlueprintCallable, Category = "SpaceMMO|Ship")
	void SetSystemPosition(const FSystemCoordinate& NewPosition);

	/** The system position that currently maps to Unreal's world origin. */
	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Ship")
	FSystemCoordinate GetRenderOrigin() const { return Navigation.RenderOrigin; }

	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Ship")
	double GetSpeedKilometresPerSecond() const { return FlightState.SpeedKilometresPerSecond(); }

	/** How many times the render origin has moved. Useful for confirming rebasing happens at all. */
	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Ship")
	int32 GetRebaseCount() const { return Navigation.RebaseCount; }

	UFUNCTION(BlueprintCallable, Category = "SpaceMMO|Ship")
	void ToggleCameraView();

protected:
	virtual void BeginPlay() override;

	/** Handling characteristics. Ultimately comes from the hull's definition. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Ship")
	FShipFlightConfig FlightConfig;

	/** Where the ship starts, in kilometres. */
	UPROPERTY(EditAnywhere, Category = "SpaceMMO|Ship")
	FVector StartingSystemPositionKilometres = FVector::ZeroVector;

	/**
	 * Draws position, speed and rebase count on screen.
	 *
	 * On by default because rebasing is invisible when it works — the whole point is that nothing
	 * appears to happen — so without a readout there is no way to tell it ever ran.
	 */
	UPROPERTY(EditAnywhere, Category = "SpaceMMO|Ship")
	bool bShowFlightDebug = true;

private:
	void ApplyWorldTransform();

	void ThrustForward(float Value);
	void ThrustRight(float Value);
	void ThrustUp(float Value);
	void Pitch(float Value);
	void Yaw(float Value);
	void Roll(float Value);
	void StartBoost();
	void StopBoost();

	UPROPERTY(VisibleAnywhere, Category = "SpaceMMO|Ship")
	TObjectPtr<USceneComponent> ShipRoot;

	UPROPERTY(VisibleAnywhere, Category = "SpaceMMO|Ship")
	TObjectPtr<UStaticMeshComponent> Hull;

	UPROPERTY(VisibleAnywhere, Category = "SpaceMMO|Ship")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, Category = "SpaceMMO|Ship")
	TObjectPtr<UCameraComponent> ThirdPersonCamera;

	/** Design-bible §8: third person by default, first person on a toggle. */
	UPROPERTY(VisibleAnywhere, Category = "SpaceMMO|Ship")
	TObjectPtr<UCameraComponent> FirstPersonCamera;

	FShipNavigation Navigation;

	FShipFlightState FlightState;

	FShipFlightInput PendingInput;

	bool bFirstPerson = false;
};
