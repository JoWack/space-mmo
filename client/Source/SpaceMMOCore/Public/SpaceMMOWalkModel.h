#pragma once

#include "CoreMinimal.h"
#include "SpaceMMOCoordinates.h"
#include "SpaceMMOWalkModel.generated.h"

/** Which dodge a character is in (task 195): a roll the way they were moving, or a hop back standing still. */
UENUM()
enum class EWalkDodge : uint8
{
	None = 0,
	Roll = 1,
	Hop = 2,
};

/**
 * What a person on foot is trying to do this frame.
 */
USTRUCT(BlueprintType)
struct SPACEMMOCORE_API FWalkInput
{
	GENERATED_BODY()

	/** Movement intent in the character's own tangent plane: X forward, Y right, each -1..1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Walk")
	FVector2D Move = FVector2D::ZeroVector;

	/** Turn rate about the surface normal, in -1..1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Walk")
	double Turn = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Walk")
	bool bJump = false;

	/**
	 * Whether the character is running rather than walking.
	 *
	 * <strong>Simulated, not drawn.</strong> It travels with the rest of the input because the
	 * server integrates this model and the client predicts with it; a client that simply moved
	 * faster would be a client disagreeing with the server about where it is.
	 *
	 * `design-bible.md` §2 gives this to the `stamina` skill -- "sprint, jump, exertion pool" -- and
	 * defers the skill to the combat milestone, because a pool needs an XP source before it means
	 * anything. Jump is the precedent: same skill, works today, gains a cost later without the
	 * movement code changing shape.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Walk")
	bool bSprint = false;

	/**
	 * How many times dodge has been pressed (task 195). A count rather than a held flag: a tap that comes and goes
	 * between two of the server's ticks would never be seen as a flag, and a dodge is one press, not a hold.
	 */
	UPROPERTY()
	uint8 DodgePresses = 0;

	/**
	 * Which way the client faces after its own step, for the server to adopt (task 165).
	 *
	 * <strong>Turning is the client's, and only the result crosses the wire.</strong> Turn is the
	 * mouse's movement in one frame. The client applied it two hundred times a second; the server
	 * applied whichever frame's value had arrived last, thirty times a second -- so the two faced
	 * different ways within seconds, the server walked the character along its own facing, and
	 * reconciliation dragged the client after it. On a dedicated server that was stutter and a
	 * character going somewhere other than where it was pointed. Sending the facing removes the
	 * re-simulation entirely, which is what Unreal's own character movement does with the control
	 * rotation. Position stays the server's.
	 */
	UPROPERTY()
	FQuat Facing = FQuat::Identity;

	/** False until the client has a facing worth adopting: it takes the server's first. */
	UPROPERTY()
	bool bHasFacing = false;

	/**
	 * Which of the server's deliberate facings this client's facing was turned from (task 145).
	 *
	 * The client takes the server's facing once, when a pawn begins; after that every facing the
	 * server holds came from this client a moment ago. A server that turns a character later -- a new
	 * one stood at their outpost, facing it, seconds after spawning -- bumps an epoch instead, the
	 * client takes the facing again when it sees the new one, and the server ignores facings sent from
	 * an older one. Without it the client's next input put the character back the way it was, and the
	 * client never saw the turn at all.
	 */
	UPROPERTY()
	uint8 FacingEpoch = 0;

	/** Clamps every axis into range, for the same reason ship input is clamped: clients lie. */
	FWalkInput Sanitised() const
	{
		FWalkInput Result;
		Result.Move = FVector2D(
			FMath::Clamp(Move.X, -1.0, 1.0), FMath::Clamp(Move.Y, -1.0, 1.0));
		Result.Turn = FMath::Clamp(Turn, -1.0, 1.0);
		Result.bJump = bJump;
		Result.bSprint = bSprint;
		Result.DodgePresses = DodgePresses;

		// A facing that is not a rotation is no facing: the server keeps its own.
		Result.bHasFacing =
			bHasFacing && !Facing.ContainsNaN() && Facing.SizeSquared() > UE_KINDA_SMALL_NUMBER;
		Result.Facing = Result.bHasFacing ? Facing.GetNormalized() : FQuat::Identity;
		Result.FacingEpoch = FacingEpoch;

		return Result;
	}
};

/**
 * How a character moves on foot. Content, not code.
 */
USTRUCT(BlueprintType)
struct SPACEMMOCORE_API FWalkConfig
{
	GENERATED_BODY()

	/** Top speed on level ground, centimetres per second. 600 is a brisk walk. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Walk")
	double WalkSpeed = 600.0;

	/** How quickly the character reaches that speed, centimetres per second squared. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Walk")
	double GroundAcceleration = 3000.0;

	/** Fraction of horizontal speed shed per second when not being driven. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Walk")
	double GroundDamping = 8.0;

	/**
	 * How much of ground acceleration is available in mid-air, 0..1.
	 *
	 * Not zero. Realistically a jumping person cannot change direction at all, but a character who
	 * cannot be steered mid-jump feels broken to play rather than realistic.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Walk")
	double AirControl = 0.25;

	/** Speed imparted straight up on a jump, centimetres per second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Walk")
	double JumpSpeed = 420.0;

	/** Degrees per second the character turns at full input. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Walk")
	double TurnRate = 180.0;

	/**
	 * What holding sprint multiplies the top speed by.
	 *
	 * A ceiling, not a shove: the character still accelerates toward it at the same rate, so sprint
	 * changes how fast somebody ends up going and not how abruptly they get there.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Walk")
	double SprintMultiplier = 1.8;

	/**
	 * The speed the model accelerates toward, centimetres per second: WalkSpeed, or WalkSpeed times
	 * SprintMultiplier while sprinting, never less than WalkSpeed (136). Each race's sprint clip sits in
	 * its speed blend space at this value (191), read from here by the importer, so the clip's feet and
	 * the character's ground speed are one number rather than two that can drift.
	 */
	double TopSpeed(const bool bSprint) const
	{
		return bSprint ? WalkSpeed * FMath::Max(1.0, SprintMultiplier) : WalkSpeed;
	}

	/**
	 * The stamina pool (ADR-0017): 100 at stamina 1, 150 at 99. The server sets it from the character's level; 100
	 * until the skill is seeded (task 101).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Walk")
	double StaminaMax = 100.0;

	/** Refilled a second, once StaminaRegenDelaySeconds have passed without spending any. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Walk")
	double StaminaRegenPerSecond = 20.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Walk")
	double StaminaRegenDelaySeconds = 1.0;

	/** What sprinting spends a second: 10 s of sprint from a full pool at level 1 (Claude, 19 October; task 195). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Walk")
	double SprintStaminaPerSecond = 10.0;

	/** What a dodge costs: four from a full pool at level 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Walk")
	double DodgeStaminaCost = 25.0;

	/** How long into a dodge nothing can hit the dodger. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Walk")
	double DodgeUntouchableSeconds = 0.25;

	/**
	 * How much faster than captured the roll plays. The roll is the library's motion capture (ADR-0017, amended 19
	 * October): 2.07 s as captured, played from its crouch at this rate. The character travels by RollProfile at
	 * the same rate, so the clip and the ground agree; the importer logs the profile from the clip.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Walk")
	double RollPlayRate = 1.8;

	/** The hop back when standing still: 0.5 s and about 1.5 m, its own clip's (task 195). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Walk")
	double HopSeconds = 0.5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Walk")
	double HopDistanceCentimetres = 150.0;
};

/**
 * A character's motion, in the same system frame everything else uses.
 */
USTRUCT(BlueprintType)
struct SPACEMMOCORE_API FWalkState
{
	GENERATED_BODY()

	/** Velocity in system-frame axes, centimetres per second. */
	UPROPERTY(BlueprintReadWrite, Category = "SpaceMMO|Walk")
	FVector Velocity = FVector::ZeroVector;

	/** What is left in the stamina pool (task 195). Starts full: Step clamps it to the config's maximum. */
	double Stamina = TNumericLimits<double>::Max();

	/** Seconds since stamina was last spent; it refills once StaminaRegenDelaySeconds have passed. */
	double SinceSpentSeconds = 0.0;

	/** Sprint ran the pool dry, and stays a run until the key is let go. */
	bool bSprintSpent = false;

	/** The dodge under way, and how long since it began. */
	EWalkDodge Dodge = EWalkDodge::None;
	double DodgeSeconds = 0.0;

	/** Which way along the ground the dodge goes, in system-frame axes. */
	FVector DodgeDirection = FVector::ZeroVector;

	/** The last count of dodge presses taken from input, so each press is one dodge. */
	uint8 DodgePressesSeen = 0;

	/** Dodges begun, and refused for want of stamina: counts, so every machine can tell one from the next. */
	uint8 DodgesStarted = 0;
	uint8 DodgesRefused = 0;

	/** Orientation, with Z aligned to whatever counts as up where the character is standing. */
	UPROPERTY(BlueprintReadWrite, Category = "SpaceMMO|Walk")
	FQuat Rotation = FQuat::Identity;
};

/**
 * Walking on a sphere.
 *
 * <strong>Up is the surface normal, not the world Z axis.</strong> That single substitution is
 * what makes a planet walkable: keep going and the ground tilts under you until you are standing
 * on what used to be the underside of the world, and nothing in here ever notices, because
 * nothing in here has an opinion about which way is up beyond what it is handed.
 *
 * Pure, like the flight model and for the same reasons: the server integrates it, the client
 * predicts with it, and a test drives it a full lap around a planet without a world existing.
 */
class SPACEMMOCORE_API FCharacterWalkModel
{
public:
	/**
	 * Advances one step.
	 *
	 * @param SurfaceNormal Outward normal of the ground beneath. Defines up for this frame.
	 * @param Gravity       Acceleration from every nearby body, centimetres per second squared.
	 * @param bOnGround     Whether the character was resting on the surface at the start of this
	 *                      step. Grounded and airborne movement are genuinely different.
	 */
	/** How long a dodge lasts, in seconds. */
	static double DodgeDuration(EWalkDodge Dodge, const FWalkConfig& Config);

	/**
	 * How far along the ground a dodge has carried the character after Seconds, in centimetres: the roll by its
	 * clip's own root (RollProfile), the hop eased in and out.
	 */
	static double DodgeDistance(EWalkDodge Dodge, double Seconds, const FWalkConfig& Config);

	/** Whether nothing can hit the character: the first DodgeUntouchableSeconds of a dodge (ADR-0017). */
	static bool IsUntouchable(const FWalkState& State, const FWalkConfig& Config);

	/** Whether sprint is actually running this step: held, moving, and stamina left (task 195). */
	static bool IsSprinting(const FWalkState& State, const FWalkInput& Input);

	/**
	 * The roll's root, centimetres forward of where it began, at each of the clip's frames from its crouch (frame 3,
	 * where it is played from) to standing: read off anim_Dodge_Roll by SpaceMMOImportClips, 19 October. A test checks
	 * it against the clip.
	 */
	static const TArray<double>& RollProfile();

	/** The frame of the captured roll its playing starts from, and the rate it was captured at. */
	static constexpr int32 RollStartFrame = 3;
	static constexpr double RollCaptureRate = 30.0;

	static FWalkState Step(
		const FWalkState& State,
		const FWalkInput& Input,
		const FWalkConfig& Config,
		const FVector& SurfaceNormal,
		const FVector& Gravity,
		bool bOnGround,
		double DeltaSeconds);

	/**
	 * Rebuilds an orientation so its up axis matches the surface, keeping the heading.
	 *
	 * The awkward case is a heading that has drifted parallel to the new normal — walking over a
	 * sharp ridge, or being handed a normal from a cliff face. Projecting the old forward onto the
	 * new tangent plane then leaves nothing to normalise, and a naive implementation produces a
	 * NaN rotation that propagates into the transform and makes the character vanish.
	 */
	static FQuat AlignToSurface(const FQuat& Current, const FVector& SurfaceNormal);

	/**
	 * The input a client sends: its own facing after the step it just took, and no turn, because
	 * the turn is already in the facing and applying it again on the server is the fault task 165
	 * removed.
	 *
	 * @param bFacingIsOurs Whether this client has taken the server's facing yet. Until it has,
	 *                      sending its own would overwrite one the server chose deliberately --
	 *                      stepping out of a ship facing its nose, say.
	 */
	static FWalkInput ForServer(
		const FWalkInput& Input, const FWalkState& State, bool bFacingIsOurs, uint8 FacingEpoch = 0);

	/**
	 * The server's half: the client's facing becomes the character's, and no turn is re-applied.
	 *
	 * Only a facing turned from the server's latest deliberate one: see FWalkInput::FacingEpoch.
	 */
	static void AdoptClientFacing(FWalkState& State, FWalkInput& Input, uint8 ServerFacingEpoch = 0);

	/**
	 * A facing that stands on Up and looks along the ground toward Toward (task 145).
	 *
	 * Toward is a direction, not a point: whatever of it lies along Up is dropped, because a character
	 * turns about the ground, not toward the sky. Returns Fallback when nothing is left of it -- a
	 * target straight overhead or underfoot names no way to face.
	 */
	static FQuat FacingToward(const FVector& Up, const FVector& Toward, const FQuat& Fallback);

	/** How far the character moves this step, in kilometres, ready to add to a system position. */
	static FVector PositionDeltaKilometres(const FWalkState& State, double DeltaSeconds);

	/**
	 * Slides a character out of something it walked into, and takes the motion into it away.
	 *
	 * <strong>The arithmetic of a blocking hit, with none of the finding of one.</strong> Whether
	 * anything is in the way is a question about the world, and the world is the pawn's business;
	 * what happens once the answer is yes is movement, and movement lives here where it can be
	 * tested without a world at all. ADR-0013 puts the seam exactly here.
	 *
	 * Fully inelastic and tangential-preserving, which is the same treatment
	 * <see cref="FPlanetTerrain::ResolveContact"/> gives the ground: the component of velocity
	 * heading into the surface is removed rather than reflected, so a character stops against a
	 * wall instead of bouncing off it, and anything along the wall survives so they can slide.
	 *
	 * @param Normal      Outward normal of what was hit, pointing back at the character.
	 * @param DepthCentimetres How far past the surface the character ended up. Zero for a touch.
	 */
	static FWalkState ResolveBlockingHit(
		const FWalkState& State, const FVector& Normal, double DepthCentimetres);

	/**
	 * How far along the normal a character has to be pushed to be clear, in centimetres.
	 *
	 * Separate from the state change because a caller has to move a system coordinate rather than
	 * a velocity, and it should not have to know the skin width to do it.
	 *
	 * <strong>Pushed a hair further than exactly clear.</strong> Landing precisely on a surface
	 * leaves the next frame's sweep starting inside it by whatever floating point does, and a
	 * character that alternates between clear and penetrating jitters against every wall it
	 * touches -- the same reason ground contact carries a tolerance rather than resolving exactly.
	 */
	static double SeparationCentimetres(double DepthCentimetres);

	/**
	 * What is left of a step once the surface it ran into has taken its share.
	 *
	 * <strong>The half of a blocking hit that was missing, and the reason a character could be
	 * pinned against a hull while pressing forward.</strong> Stopping at the contact point and
	 * spending the rest of the step nowhere leaves a character in continuous contact moving only by
	 * the separation push -- measured at six centimetres a second against a walk of six hundred,
	 * which is indistinguishable from the controls having died. Cancelling the velocity into the
	 * surface fixes the next frame; this fixes the frame you are in.
	 *
	 * Projection onto the contact plane, which is what every character controller does and what
	 * makes a wall something you slide along rather than something you stick to.
	 *
	 * @param RemainingDelta How much of the step is unspent, in centimetres.
	 * @param Normal         Outward normal of what was hit, pointing back at the character.
	 */
	static FVector SlideDeltaCentimetres(const FVector& RemainingDelta, const FVector& Normal);

	/** The steepest ground a character can stand on rather than slide off, in degrees. */
	static constexpr double SteepestWalkableSlopeDegrees = 50.0;

	/** How far above a surface still counts as standing on it, in centimetres. */
	static constexpr double FloorCaptureGapCentimetres = 20.0;

	/** And how far, once already standing, before the character is let go of it. */
	static constexpr double FloorReleaseGapCentimetres = 45.0;

	/**
	 * Whether a surface found under the feet is one to stand on.
	 *
	 * <strong>The height field is not the only floor any more.</strong> Terrain is a pure function
	 * of direction and is resolved by FPlanetTerrain::ResolveContact; a building's slab is geometry,
	 * and nothing in the height field knows it exists. Without this a character walks into a station
	 * and falls through its floor to the ground the planet says is there, four and a half metres
	 * below the gallery they were standing on.
	 *
	 * The rules are deliberately the same three FPlanetTerrain::ResolveContact uses, because a floor
	 * that behaved differently from the ground would be two rules for one idea:
	 *
	 * - a band that counts as touching, widened once the character is already standing, so walking
	 *   down a shallow step does not spend a frame airborne every step;
	 * - a separation speed that always wins, so a jump leaves the floor rather than being dragged
	 *   back onto it by the band;
	 * - and, new here because a sphere has no walls, a limit on how steep the surface may be. A
	 *   downward sweep beside a wall finds the wall, and standing on one would let a character walk
	 *   up the outside of the building.
	 *
	 * @param Normal                     Outward normal of what was found underfoot.
	 * @param Up                         Which way is up where the character is standing.
	 * @param GapCentimetres             How far the feet are above it. Negative if inside it.
	 * @param SeparationSpeedCentimetres Speed along Up. Positive is climbing away from the floor.
	 * @param bWasStanding               Whether the character was standing when the step began.
	 */
	static bool StandsOn(
		const FVector& Normal,
		const FVector& Up,
		double GapCentimetres,
		double SeparationSpeedCentimetres,
		bool bWasStanding);

	/**
	 * Whether a character found inside a floor climbs out on top of it (task 175).
	 *
	 * <strong>Nothing else lifts somebody standing inside a floor.</strong> Ayla was put back where
	 * she had logged out, which was before Borlash existed and so under its paving. The terrain caught
	 * her thirty centimetres down inside the city's floor, and the floor probe -- which starts ten
	 * centimetres above the feet -- began inside the slab, which says nothing about what is underfoot.
	 * Pushing out of geometry only happens while moving, and the terrain took her back down the next
	 * frame, so she stood half under the city until a jump let go of the ground long enough for the
	 * floor to be found (2 October).
	 *
	 * Up through a floor only, never over a wall. The way out of what the probe began inside must
	 * point up as steeply as a floor's normal can, which beside a railing it does not; the top found
	 * above must be a floor; and it must be within a step's reach rather than a storey's.
	 *
	 * @param WayOut                    The direction the probe reported as the shortest way out.
	 * @param FloorNormal               Normal of the top found by searching down from above.
	 * @param Up                        Which way is up where the character is standing.
	 * @param FloorAboveFeetCentimetres How far that top is above the feet.
	 */
	static bool ClimbsOutOnto(
		const FVector& WayOut,
		const FVector& FloorNormal,
		const FVector& Up,
		double FloorAboveFeetCentimetres);

	/** Whether the way out of something is up, as out through a floor's top rather than a wall's side. */
	static bool WayOutIsUp(const FVector& WayOut, const FVector& Up);

	/** How deep inside a floor a character may be and still climb out onto it: a step, not a roof. */
	static constexpr double DeepestClimbOutCentimetres = 100.0;

	/**
	 * Speed across the ground, ignoring any rise or fall. Centimetres per second.
	 *
	 * <strong>What a walk cycle should be played against, and not the same as the speed of the
	 * velocity vector.</strong> A character stepping off a ledge is travelling fast downward while
	 * moving nowhere across the ground; blending a run animation on total speed would have them
	 * sprinting in mid-air, faster the further they fall.
	 */
	static double GroundSpeed(const FWalkState& State, const FVector& SurfaceNormal);

	/**
	 * Speed along the surface normal: positive rising, negative falling. Centimetres per second.
	 *
	 * The sign is the whole point — it is what tells a jump from a fall, and they are different
	 * animations.
	 */
	static double VerticalSpeed(const FWalkState& State, const FVector& SurfaceNormal);

	/**
	 * Which way the character is travelling relative to the way it is facing, in degrees.
	 *
	 * Zero straight ahead, +90 to the right, -90 to the left, ±180 backwards — the convention a
	 * directional blend space expects, so strafing plays a sidestep rather than a forward run
	 * performed sideways.
	 *
	 * <strong>Measured in the character's own frame, against the surface normal.</strong> Doing it
	 * against world axes would be right at one point on a planet and wrong everywhere else, which
	 * is the mistake this codebase has made in one form or another several times: up is the ground's
	 * normal, never Z.
	 *
	 * Zero when standing still, which is what a blend space wants when the speed weight is zero
	 * anyway.
	 */
	static double MoveDirectionDegrees(const FWalkState& State, const FVector& SurfaceNormal);
};
