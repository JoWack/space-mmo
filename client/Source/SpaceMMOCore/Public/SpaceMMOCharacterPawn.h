#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "SpaceMMOCoordinates.h"
#include "SpaceMMOFlightModel.h"
#include "SpaceMMOWalkModel.h"
#include "SpaceMMOCharacterPawn.generated.h"

class UCameraComponent;
class USkeletalMeshComponent;
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
UCLASS(Config = Game)
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

	/**
	 * Speed across the ground, in metres per second, ignoring any rise or fall.
	 *
	 * <strong>This is what an animation blend space should be driven by</strong>, not the total
	 * speed above: a character falling off a cliff is moving quickly and walking nowhere, and
	 * blending a run on total speed would have them sprinting in mid-air.
	 */
	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Character")
	double GetGroundSpeedMetresPerSecond() const
	{
		return FCharacterWalkModel::GroundSpeed(WalkState, SurfaceNormal) / 100.0;
	}

	/** Metres per second along the surface normal: positive rising, negative falling. */
	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Character")
	double GetVerticalSpeedMetresPerSecond() const
	{
		return FCharacterWalkModel::VerticalSpeed(WalkState, SurfaceNormal) / 100.0;
	}

	/**
	 * Which way the character is travelling relative to the way it faces, in degrees.
	 *
	 * Zero ahead, +90 right, -90 left, ±180 back — the convention a directional blend space wants,
	 * so strafing plays a sidestep rather than a forward run performed sideways.
	 */
	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Character")
	double GetMoveDirectionDegrees() const
	{
		return FCharacterWalkModel::MoveDirectionDegrees(WalkState, SurfaceNormal);
	}

	/**
	 * The character's body, for an animation blueprint to be set on.
	 *
	 * <strong>Everything animation needs is already published above</strong> — ground speed,
	 * direction, vertical speed, whether the feet are down — and all four are readable on a remote
	 * player's pawn as well, because FollowServerState fills the same walk state from what the
	 * server replicated. So an animation blueprint drives everybody's character from one set of
	 * values, and none of it feeds back: animation is drawn, never simulated. The server owns where
	 * a person is, and a pose must never be able to argue with it.
	 */
	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Character")
	USkeletalMeshComponent* GetBodyMesh() const { return BodyMesh; }

	UFUNCTION(BlueprintCallable, Category = "SpaceMMO|Character")
	void ToggleCameraView();

	/**
	 * Asks to climb into the nearest ship.
	 *
	 * A request. The server picks the ship and checks the range, because a client that nominates
	 * a ship on the other side of the planet is nominating, not deciding.
	 */
	UFUNCTION(BlueprintCallable, Category = "SpaceMMO|Character")
	void RequestEmbark();

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

	/** Reliable, unlike input: a dropped boarding request is not fixed by the next frame. */
	UFUNCTION(Server, Reliable)
	void ServerEmbark();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Character")
	FWalkConfig WalkConfig;

	/** How this client resolves disagreement with the server. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Character")
	FShipReconciliation Reconciliation;

	/**
	 * How far the character's lowest point is from its origin, in kilometres.
	 *
	 * <strong>Zero, because this pawn's origin is its feet.</strong> The body mesh is offset upward
	 * from the root and both cameras sit at eye height above it -- 160 and 165 cm -- so everything
	 * here is built around an origin on the ground.
	 *
	 * It was ninety centimetres, documented as the distance from the character's centre to its feet,
	 * which is the other convention entirely. Ground contact duly held the origin ninety centimetres
	 * up and the whole character floated by exactly that: measured at 90.0 cm above the ground with
	 * a standing height of 90.0 cm, which is the pair of numbers that named it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Character")
	double StandingHeightKilometres = 0.0;

	/** So the standing-gap measurement is reported once rather than sixty times a second. */
	bool bReportedStandingGap = false;

	UPROPERTY(EditAnywhere, Category = "SpaceMMO|Character")
	FVector StartingSystemPositionKilometres = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = "SpaceMMO|Character")
	bool bShowWalkDebug = true;

private:
	/** Puts the configured model and animation blueprint on the pawn, or says why it did not. */
	void ApplyCharacterMesh();

	/**
	 * Applies which camera is live and what the body does about it.
	 *
	 * One function rather than two, because the two decisions are the same decision: in first
	 * person the character is inside their own head, and a model drawn there fills the screen with
	 * the underside of a jaw.
	 */
	void ApplyCameraView();

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

	/**
	 * Tilts the view up and down.
	 *
	 * <strong>The camera, not the character.</strong> Turning is part of the walk model because it
	 * changes which way the body faces and the server simulates that; pitch changes nothing about
	 * where somebody stands or walks, so putting it in FWalkInput would replicate a number the
	 * simulation has no use for and add a field to a struct with headless tests over it.
	 *
	 * Clamped short of vertical, because a view that passes straight up flips the horizon over and
	 * there is no way back from it that is not a second bug.
	 */
	void LookUp(float Value);

	/** Degrees per unit of mouse movement. */
	UPROPERTY(EditAnywhere, Config, Category = "SpaceMMO|Character")
	double LookSensitivityDegrees = 1.5;

	/** How far the view may tilt from level, short of straight up or straight down. */
	UPROPERTY(EditAnywhere, Config, Category = "SpaceMMO|Character")
	double MaxLookPitchDegrees = 85.0;

	/** Where the view is tilted to. Local to whoever is looking; nothing else depends on it. */
	double ViewPitchDegrees = 0.0;
	void StartJump();
	void StopJump();

	UPROPERTY(VisibleAnywhere, Category = "SpaceMMO|Character")
	TObjectPtr<USceneComponent> CharacterRoot;

	/**
	 * The placeholder tube, kept and shown only when no character mesh is configured or it fails
	 * to load.
	 *
	 * The same reasoning the deposit settings give for falling back to an engine cylinder: a
	 * character that failed to render would still walk, still gather and still be invisible, which
	 * reads as the player not existing and is far worse than an ugly stand-in.
	 */
	UPROPERTY(VisibleAnywhere, Category = "SpaceMMO|Character")
	TObjectPtr<UStaticMeshComponent> Body;

	UPROPERTY(VisibleAnywhere, Category = "SpaceMMO|Character")
	TObjectPtr<USkeletalMeshComponent> BodyMesh;

	/**
	 * The character model, e.g. <c>/Game/Characters/Human/HumanCharacterRigged</c>.
	 *
	 * <strong>Config, so a model can be swapped without a rebuild.</strong> The same reason the
	 * terrain material is config: deciding how something looks means trying a value, looking at it,
	 * and trying another, and a compile in the middle of that loop is how people stop iterating.
	 *
	 * Set it in DefaultGame.ini under [/Script/SpaceMMOCore.SpaceMMOCharacterPawn]. Unset leaves
	 * the placeholder tube, which is a working state and says so in the log.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "SpaceMMO|Character")
	FSoftObjectPath CharacterMesh;

	/** The animation blueprint to run on it. Unset leaves the model in its bind pose. */
	UPROPERTY(EditAnywhere, Config, Category = "SpaceMMO|Character")
	FSoftClassPath CharacterAnimClass;

	/**
	 * How the model sits on the pawn, which no two exporters agree about.
	 *
	 * Config rather than compiled for exactly the reason above: whether a mesh faces +X or +Y, and
	 * whether its origin is at its feet or its hips, is discovered by looking at it. Unreal's own
	 * mannequin wants -90 degrees of yaw; a mesh exported facing forward wants none.
	 *
	 * The offset is measured from the pawn's origin, which <em>is</em> the character's feet — see
	 * StandingHeightKilometres, which is zero for that reason.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "SpaceMMO|Character")
	FRotator CharacterMeshRotation = FRotator(0.0, -90.0, 0.0);

	UPROPERTY(EditAnywhere, Config, Category = "SpaceMMO|Character")
	FVector CharacterMeshOffset = FVector::ZeroVector;

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

	/** Diagnostic only: seconds since the last on-foot line. */
	double DiagnosticSeconds = 0.0;
};
