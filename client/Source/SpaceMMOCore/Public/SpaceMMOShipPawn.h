#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "SpaceMMOCoordinates.h"
#include "SpaceMMOFlightModel.h"
#include "SpaceMMOPlanet.h"
#include "SpaceMMOShipPawn.generated.h"

class UCameraComponent;
class USpringArmComponent;
class UStaticMeshComponent;

/**
 * What the server publishes about a ship.
 *
 * <strong>Position travels as a system coordinate, not as an Unreal transform.</strong> Every
 * client rebases its own render origin independently, so a world location means something
 * different on each of them — replicating one would put every remote ship in the wrong place for
 * everybody except whoever happened to share the sender's origin.
 *
 * Doubles cross the wire uncompressed for now. That is honest but not cheap, and quantising
 * position and rotation is the obvious first saving once there are enough ships to care.
 */
USTRUCT()
struct FShipNetState
{
	GENERATED_BODY()

	UPROPERTY()
	FSystemCoordinate SystemPosition;

	UPROPERTY()
	FVector Velocity = FVector::ZeroVector;

	UPROPERTY()
	FQuat Rotation = FQuat::Identity;

	UPROPERTY()
	FVector AngularVelocity = FVector::ZeroVector;

	/** Server time the state was captured, so a receiver knows how stale it is. */
	UPROPERTY()
	double ServerTimeSeconds = 0.0;
};

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
UCLASS(Config = Game)
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

	/**
	 * Where the ship will be when it begins play.
	 *
	 * Must be called on a deferred spawn, before FinishSpawning — the same contract as
	 * <c>ASpaceMMOCharacterPawn::SetStartingSystemPosition</c>, and named the same way because the
	 * two pawns had grown separate spellings of one idea and a caller had to know which was which.
	 */
	UFUNCTION(BlueprintCallable, Category = "SpaceMMO|Ship")
	void SetStartingSystemPosition(const FVector& Kilometres)
	{
		StartingSystemPositionKilometres = Kilometres;
	}

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

	/**
	 * Asks to step out onto the surface.
	 *
	 * A request, not an instruction. The server checks the ship is actually landed and does the
	 * possession itself — a client that decides it has disembarked has decided nothing.
	 */
	UFUNCTION(BlueprintCallable, Category = "SpaceMMO|Ship")
	void RequestDisembark();

	/** Altitude above the nearest planet's surface, in kilometres. Zero if there is none. */
	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Ship")
	double GetAltitudeKilometres() const;

	/**
	 * Height above the terrain rather than above the sphere it sits on.
	 *
	 * The two disagree by however tall the ground is, and this is the one a pilot cares about:
	 * it is what reaches zero on landing. Kept alongside the sphere figure rather than replacing
	 * it, because the proximity band is derived from this one and showing only the other made
	 * "Altitude 0.34 km | ATMOSPHERE" look like a contradiction while both halves were true.
	 */
	double GetGroundAltitudeKilometres() const { return GroundAltitudeKilometres; }

	/**
	 * Speed of a circular orbit where the ship currently is, in centimetres per second.
	 *
	 * A fact about the ship's situation rather than a display concern: on a 20 km world it is only
	 * about 443 m/s, which is why atmospheric drag caps a ship well below it (task 98) and why a
	 * faster ship cannot be flown along the ground at all, only skipped across it.
	 */
	double GetOrbitalSpeedHere() const;

	/**
	 * Whether flight debugging is on, which the HUD's debug line follows.
	 *
	 * One switch rather than two, so the readout and whatever else is being watched cannot disagree
	 * about whether this is a debugging session.
	 */
	bool ShowsFlightDebug() const { return bShowFlightDebug; }

	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Ship")
	EPlanetProximity GetProximity() const { return Proximity; }

	/** True while the ship is resting on terrain. */
	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Ship")
	bool IsOnGround() const { return bOnGround; }

	virtual void GetLifetimeReplicatedProps(
		TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** How far the last server correction moved this client, in kilometres. Diagnostic. */
	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Ship")
	double GetLastCorrectionKilometres() const { return LastCorrectionKilometres; }

	/**
	 * Which hull this ship is configured to draw, for anything that needs to check the asset.
	 *
	 * Public because a test measures the mesh: whether the hull is drawn where the ship is cannot
	 * be reasoned about from the configuration, only read off the imported artefact.
	 */
	const FSoftObjectPath& GetHullMesh() const { return HullMesh; }

protected:
	virtual void BeginPlay() override;

	/**
	 * Pilot intent, sent to the server every frame the ship is locally controlled.
	 *
	 * Unreliable on purpose. Input is a continuous stream, and a dropped frame of it is corrected
	 * by the next one — retransmitting stale intent would arrive late and be wrong twice over.
	 *
	 * The server sanitises what arrives, so a client sending an out-of-range axis flies exactly as
	 * fast as one sending a legal value.
	 */
	UFUNCTION(Server, Unreliable, WithValidation)
	void ServerSendInput(FShipFlightInput Input);

	/** Reliable, unlike input: a dropped boarding request is not corrected by the next frame. */
	UFUNCTION(Server, Reliable)
	void ServerDisembark();

	/** Class spawned when stepping out. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Ship")
	TSubclassOf<class ASpaceMMOCharacterPawn> CharacterClass;

	/** How this client resolves disagreement with the server. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Ship")
	FShipReconciliation Reconciliation;

	/** Handling characteristics. Ultimately comes from the hull's definition. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Ship")
	FShipFlightConfig FlightConfig;

	/**
	 * Distance from the ship's centre to its hull, in kilometres.
	 *
	 * Two metres, matching the placeholder cone. It was 0.02 — twenty metres — which parked the
	 * ship hovering twenty metres above the ground and dropped anyone stepping out off the
	 * equivalent of a six-storey building. Kilometres are an unforgiving unit for something this
	 * small, and that is exactly how the mistake happened.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Ship")
	double HullRadiusKilometres = 0.002;

	/**
	 * The hull to draw, named in DefaultGame.ini rather than compiled in.
	 *
	 * <strong>Content, for the same reason the character model and the station buildings are.</strong>
	 * Swapping the ship somebody flies should be a line in a file, not a rebuild, and the cone stays
	 * behind it so an unset or mistyped path leaves something visible rather than nothing.
	 *
	 * Note what this does <em>not</em> change: HullRadiusKilometres above. That two-metre sphere is
	 * what ground contact and the blocking sweep both use, and it does not follow the mesh -- a hull
	 * that looked twenty metres long and collided like a two-metre ball would be a disagreement
	 * nobody finds until they are wedged in a doorway. Set both, deliberately.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "SpaceMMO|Ship")
	FSoftObjectPath HullMesh;

	/**
	 * How long the ship should be, nose to tail, in metres.
	 *
	 * The mesh is scaled uniformly to this off its own bounds, so a model exported in the wrong
	 * units is corrected without anybody editing the asset -- and the authored size is logged, so a
	 * model needing a large multiplier is something you get told about rather than silently live
	 * with.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "SpaceMMO|Ship")
	double HullLengthMetres = 12.0;

	/** Turns an authored hull to face +X, which is forward and the direction thrust is applied in. */
	UPROPERTY(EditAnywhere, Config, Category = "SpaceMMO|Ship")
	FRotator HullMeshRotation = FRotator::ZeroRotator;

	/**
	 * Nudges the hull relative to the ship's origin, which is what the flight model moves.
	 *
	 * Zero is correct for almost everything. The hull is already centred on its own geometry before
	 * this is applied, so this is for deliberately sitting a model off its pivot -- not for
	 * correcting an asset whose pivot is wrong, which the centring handles without being asked.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "SpaceMMO|Ship")
	FVector HullMeshOffset = FVector::ZeroVector;

	/** Puts the configured hull on, or says why it did not. */
	void ApplyHullMesh();

	/** Centres the stick when the pilot leaves, so an empty ship does not fly itself. */
	virtual void UnPossessed() override;

	/**
	 * Stops the ship at anything solid it has flown into.
	 *
	 * <strong>The hull was solid to everything except the ship wearing it (ADR-0013).</strong> It
	 * is QueryOnly and blocks the pawn channel, so a walking character is stopped by a parked ship
	 * -- but flight sets position from FShipFlightModel and nothing swept, so nothing could stop the
	 * ship. It flew through the capital hub.
	 *
	 * The same shape as the character's, and for the same reasons: a swept query rather than a
	 * physics body, so there is no solver state for a render origin rebase to disturb (ADR-0001),
	 * and the arithmetic of the hit lives in the flight model where it is tested with no world.
	 *
	 * The sphere is the one ground contact already uses. A ship has been a two-metre sphere to the
	 * terrain since it could land; being a different size to a wall than to a hillside would be the
	 * kind of disagreement nobody finds until they are wedged in a doorway.
	 */
	void ResolveBlocking(const FSystemCoordinate& From);

	/** Says what the ship is against, and how far it let the ship get. */
	void ReportBlocking(const AActor* Touched, const FVector& Normal, double WantedCentimetres);

	/** What the ship is currently pressed against, if anything. Diagnostic only. */
	TWeakObjectPtr<const AActor> BlockedBy;

	/** When the current contact began, in world seconds. */
	double BlockedSinceSeconds = 0.0;

	/** Where the ship was when the current contact began. */
	FSystemCoordinate BlockedFrom;

	/** How far the ship has asked to move since the current contact began, in centimetres. */
	double BlockedWantedCentimetres = 0.0;

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
	/** Integrates one step from PendingInput. Runs on the server and on the predicting client. */
	void SimulateStep(double DeltaSeconds);

	/** Server only: captures the authoritative state for replication. */
	void PublishNetState();

	/** Owning client: pulls the prediction back toward the server's answer. */
	void ReconcileWithServer(double DeltaSeconds);

	/** Other players' ships: carries the last known state forward and eases toward new ones. */
	void FollowServerState(double DeltaSeconds);

	void PublishRenderOrigin();

	/** Gravity from every planet in the world, summed. */
	FVector ComputeGravity() const;

	/**
	 * Stops the ship falling through any planet it is touching.
	 *
	 * Resolved against the terrain height function rather than against collision geometry, so the
	 * server reaches the same answer without a renderer or a mesh.
	 */
	void ResolveGroundContact();

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

	/**
	 * The server's last word on this ship.
	 *
	 * Replicated to everyone rather than only to the owner, because remote ships are drawn from it
	 * too — this is the single channel through which one player learns another exists.
	 */
	UPROPERTY(Replicated)
	FShipNetState NetState;

	/** Client-side clock reading when NetState last changed, for extrapolating between updates. */
	double LastNetStateReceivedAt = 0.0;

	/** Server time carried by the last state actually applied, so repeats are ignored. */
	double LastAppliedServerTime = -1.0;

	double LastCorrectionKilometres = 0.0;

	bool bFirstPerson = false;

	EPlanetProximity Proximity = EPlanetProximity::Orbital;

	/** Height above the ground beneath, which is what the proximity above is classified from. */
	double GroundAltitudeKilometres = 0.0;

	bool bOnGround = false;

	/** Guards the -AutoDisembark affordance, so it fires once rather than on every landing. */
	bool bAutoDisembarked = false;

	/** Diagnostic only: the rebase count the last log line was written for. */
	int32 LastLoggedRebaseCount = 0;

	/** Diagnostic only: seconds since the last approach line. */
	double DiagnosticSeconds = 0.0;

	/** Accumulates for the -LogTickHealth heartbeat. */
	double HeartbeatSeconds = 0.0;
};
