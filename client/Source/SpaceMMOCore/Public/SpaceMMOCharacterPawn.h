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

	/**
	 * Uniform scale that stands a model of a given height at a target height.
	 *
	 * Pure and static so the arithmetic can be checked without a mesh, a world or an editor — the
	 * same treatment, and the same reasoning, as FDepositPlacement::UniformScale.
	 *
	 * Returns 1 when either height is unusable, which leaves the model exactly as authored rather
	 * than collapsing it to a point.
	 */
	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Character")
	static double UniformScaleForHeight(double AuthoredHeightCentimetres, double TargetCentimetres);

	/**
	 * Turns one angle toward another by at most a given step, the short way round.
	 *
	 * <strong>The short way is the whole point.</strong> Turning from 170 degrees to -170 is a
	 * twenty degree step, not a three hundred and forty degree spin, and the naive version of this
	 * makes a character pirouette every time they run backwards past the wrap. This project has
	 * already lost an evening to the same discontinuity in a blend space.
	 *
	 * Pure and static so the wrap can be tested without a mesh, a world or an editor.
	 */
	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Character")
	static double TurnTowards(double CurrentDegrees, double DesiredDegrees, double MaxStepDegrees);

	/**
	 * Which way the character is travelling relative to the way its <em>body</em> is drawn facing.
	 *
	 * Zero once the body has finished turning to face travel, which is what makes a forward run the
	 * only clip a facing-travel character needs. While the turn is still catching up it is the
	 * residual, so a blend space with real strafe animations in it would fill the gap correctly.
	 */
	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Character")
	double GetAnimationDirectionDegrees() const
	{
		return FRotator::NormalizeAxis(GetMoveDirectionDegrees() - MeshFacingDegrees);
	}

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

	/** Swings the drawn body round toward the direction of travel. Presentation only. */
	void UpdateMeshFacing(double DeltaSeconds);

	/** Prints actor, mesh, pose and camera positions. Behind SpaceMMO.LogCharacterDraw. */
	void ReportHowItIsDrawn() const;

	/** Samples how far off centre the character is drawn, every frame, keeping the worst. */
	void TrackHowFarOffCentre();

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

	/**
	 * Pushes the character out of anything solid it has walked into.
	 *
	 * <strong>A query, never a simulation (ADR-0013).</strong> The pawn owns no physics body: it
	 * sweeps a capsule from where it was to where the walk model wants it, and resolves any hit
	 * itself. Chaos is used to answer "is something in the way" and for nothing else, so there is
	 * no accumulated physics state for the render origin to disturb when the world rebases.
	 *
	 * The ground is not among the things it can hit. Terrain has no collision geometry at all and
	 * is resolved by FPlanetTerrain as a function of position, which is the half of ADR-0013 that
	 * did not change.
	 */
	void ResolveBlocking(const FSystemCoordinate& From);

	/**
	 * Says what the character is pressed against, and how far that surface let it travel.
	 *
	 * <strong>What is measured is the harm, not the contact.</strong> A character leaning on a hull
	 * and a character who cannot walk produce exactly the same per-frame line -- an actor name, a
	 * normal, a depth of zero -- and the first is correct behaviour while the second is a bug.
	 * Finding out which one was happening took piping 1794 of those lines into a script.
	 *
	 * What separates them is how far the contact let the character get against how far it was trying
	 * to go, so both are accumulated across the contact and reported at its edges: one line when it
	 * begins, one when it ends. Asking against getting is what makes standing still beside a ship
	 * read differently from pressing into one, which a distance on its own cannot do.
	 *
	 * @param WantedCentimetres How far this step tried to move, before anything was in the way.
	 */
	void ReportBlocking(const AActor* Touched, const FVector& Normal, double WantedCentimetres);

	/** What the character is currently pressed against, if anything. Diagnostic only. */
	TWeakObjectPtr<const AActor> BlockedBy;

	/** When the current contact began, in world seconds. */
	double BlockedSinceSeconds = 0.0;

	/** Where the character was when the current contact began. */
	FSystemCoordinate BlockedFrom;

	/** How far the character has asked to move since the current contact began, in centimetres. */
	double BlockedWantedCentimetres = 0.0;

	/** How wide and tall the character is to a sweep, in centimetres. */
	UPROPERTY(EditAnywhere, Config, Category = "SpaceMMO|Character")
	double CollisionRadiusCentimetres = 34.0;

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

	/**
	 * How tall the character stands, in centimetres. Zero leaves the model at its authored size.
	 *
	 * <strong>Fitted rather than trusted, for the same reason deposits are.</strong>
	 * FDepositPlacement already argues this at length: exporters disagree about scale, and a model
	 * of any dimensions should arrive at a sensible size with its shape intact. The first character
	 * model imported at 98 cm — a person normalised to roughly one unit, and one unit arriving as a
	 * metre — which read as a child standing next to a boulder and looked like the boulder was
	 * wrong.
	 *
	 * 180 cm because that is what everything else on this pawn was built around: the placeholder
	 * tube stood 180, the third-person boom sits at 160 and the first-person camera at 165, both
	 * eye height on a person that tall.
	 *
	 * Scaling here rather than in the asset is the cheap fix, not the correct one. Anything later
	 * attached to a socket — a mining laser in a hand — inherits this multiplier and has to
	 * remember it. Re-exporting the model at human scale and setting this to zero is the version
	 * with one authority instead of two.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "SpaceMMO|Character")
	double CharacterHeightCentimetres = 180.0;

	/**
	 * Whether the drawn body turns to face the way the character is travelling.
	 *
	 * <strong>The mesh only. Nothing the server simulates moves.</strong> The pawn still faces
	 * wherever the mouse points and still strafes; this changes what that looks like, from
	 * side-stepping to running. Chosen because the animation library's lateral clips are angled
	 * runs rather than strafes, and a character who faces where they are going needs none of them —
	 * a forward run covers every direction.
	 *
	 * Keeping it out of the walk model is deliberate: that model is pure, tested, and evaluated by
	 * the dedicated server as well as here, and how a body is drawn is not something the server
	 * should ever have an opinion about.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "SpaceMMO|Character")
	bool bCharacterFacesTravel = true;

	/** How fast the body swings round to face travel, in degrees per second. */
	UPROPERTY(EditAnywhere, Config, Category = "SpaceMMO|Character")
	double CharacterTurnRateDegreesPerSecond = 720.0;

	/** Below this, there is no travel to face and the body holds the way it was last going. */
	UPROPERTY(EditAnywhere, Config, Category = "SpaceMMO|Character")
	double CharacterFacingSpeedThresholdMetresPerSecond = 0.2;


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

	/** The same, for the draw report, so the two rate limits do not steal each other's ticks. */
	double DrawDiagnosticSeconds = 0.0;

	/** Yaw the body is drawn at, relative to the pawn's own facing. Presentation only. */
	double MeshFacingDegrees = 0.0;

	/** Worst offsets seen since the last draw report, in the camera's own axes. */
	double WorstHorizontalDegrees = 0.0;

	double WorstVerticalDegrees = 0.0;

	/** How far the drawn body is from the actor it hangs on, worst and latest, in centimetres. */
	double WorstDrawnFromActorCentimetres = 0.0;

	double LastDrawnFromActorCentimetres = 0.0;
};
