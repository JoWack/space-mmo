#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "SpaceMMOCoordinates.h"
#include "SpaceMMOFlightModel.h"
#include "SpaceMMOWalkModel.h"
#include "SpaceMMOCharacterPawn.generated.h"

class UCameraComponent;
class USpringArmComponent;
class UStaticMeshComponent;

/**
 * What the server publishes about a character on foot.
 *
 * The same shape and the same reasoning as a ship's: position travels as a system coordinate
 * rather than an Unreal transform, because every client rebases its render origin independently
 * and a world location therefore means something different on each of them.
 */
USTRUCT()
struct FCharacterNetState
{
	GENERATED_BODY()

	UPROPERTY()
	FSystemCoordinate SystemPosition;

	UPROPERTY()
	FVector Velocity = FVector::ZeroVector;

	UPROPERTY()
	FQuat Rotation = FQuat::Identity;

	UPROPERTY()
	bool bOnGround = false;

	UPROPERTY()
	double ServerTimeSeconds = 0.0;
};

/**
 * A person on foot, standing on a planet.
 *
 * Holds its position as an {@link FSystemCoordinate} exactly as a ship does, and derives its
 * Unreal transform from it against the shared render origin — so a character can walk around a
 * body a hundred million kilometres from the system origin without single-precision rendering
 * falling apart (ADR-0001).
 *
 * <strong>Its up axis is the ground's normal, not the world's.</strong> Everything that follows
 * from that — turning, movement, the camera — comes out of {@link FCharacterWalkModel}, which
 * knows nothing about actors and is tested by walking a full arc around a planet with no world
 * loaded at all.
 */
UCLASS()
class SPACEMMOCORE_API ASpaceMMOCharacterPawn : public APawn
{
	GENERATED_BODY()

public:
	ASpaceMMOCharacterPawn();

	virtual void Tick(float DeltaSeconds) override;

	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	virtual void GetLifetimeReplicatedProps(
		TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Character")
	FSystemCoordinate GetSystemPosition() const { return Navigation.SystemPosition; }

	UFUNCTION(BlueprintCallable, Category = "SpaceMMO|Character")
	void SetSystemPosition(const FSystemCoordinate& NewPosition);

	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Character")
	bool IsOnGround() const { return bOnGround; }

	/** Which way is up where the character is standing. */
	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Character")
	FVector GetSurfaceNormal() const { return SurfaceNormal; }

	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Character")
	double GetSpeedMetresPerSecond() const { return WalkState.Velocity.Size() / 100.0; }

	UFUNCTION(BlueprintCallable, Category = "SpaceMMO|Character")
	void ToggleCameraView();

	/**
	 * Where the character will be when it begins play.
	 *
	 * Must be called on a deferred spawn, before FinishSpawning. Setting the position afterwards
	 * is too late: BeginPlay has already resolved the ground and aligned the character to it, so
	 * the log reports the default position and the first frame is spent somewhere else entirely.
	 */
	UFUNCTION(BlueprintCallable, Category = "SpaceMMO|Character")
	void SetStartingSystemPosition(const FVector& Kilometres)
	{
		StartingSystemPositionKilometres = Kilometres;
	}

protected:
	virtual void BeginPlay() override;

	/** Pilot intent, sent to the server every frame the character is locally controlled. */
	UFUNCTION(Server, Unreliable, WithValidation)
	void ServerSendWalkInput(FWalkInput Input);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Character")
	FWalkConfig WalkConfig;

	/** How this client resolves disagreement with the server. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Character")
	FShipReconciliation Reconciliation;

	/**
	 * Distance from the character's centre to its feet, in kilometres.
	 *
	 * Ninety centimetres, so the placeholder capsule stands on the ground rather than half in it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Character")
	double StandingHeightKilometres = 0.0009;

	UPROPERTY(EditAnywhere, Category = "SpaceMMO|Character")
	FVector StartingSystemPositionKilometres = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = "SpaceMMO|Character")
	bool bShowWalkDebug = true;

private:
	void SimulateStep(double DeltaSeconds);
	void PublishNetState();
	void ReconcileWithServer(double DeltaSeconds);
	void FollowServerState(double DeltaSeconds);

	void PublishRenderOrigin();
	void ApplyWorldTransform();

	/** Gravity from every planet, and the ground beneath, resolved together. */
	void ResolveSurface();

	void MoveForward(float Value);
	void MoveRight(float Value);
	void TurnRight(float Value);
	void StartJump();
	void StopJump();

	UPROPERTY(VisibleAnywhere, Category = "SpaceMMO|Character")
	TObjectPtr<USceneComponent> CharacterRoot;

	UPROPERTY(VisibleAnywhere, Category = "SpaceMMO|Character")
	TObjectPtr<UStaticMeshComponent> Body;

	UPROPERTY(VisibleAnywhere, Category = "SpaceMMO|Character")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, Category = "SpaceMMO|Character")
	TObjectPtr<UCameraComponent> ThirdPersonCamera;

	UPROPERTY(VisibleAnywhere, Category = "SpaceMMO|Character")
	TObjectPtr<UCameraComponent> FirstPersonCamera;

	UPROPERTY(Replicated)
	FCharacterNetState NetState;

	/** Reused from flight: system position, render origin and rebase count are not ship-specific. */
	FShipNavigation Navigation;

	FWalkState WalkState;

	FWalkInput PendingInput;

	FVector SurfaceNormal = FVector::UpVector;

	FVector Gravity = FVector::ZeroVector;

	bool bOnGround = false;

	bool bFirstPerson = false;

	double LastAppliedServerTime = -1.0;

	double LastNetStateReceivedAt = 0.0;
};
