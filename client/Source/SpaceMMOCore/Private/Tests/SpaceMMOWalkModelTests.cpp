#include "Misc/AutomationTest.h"
#include "SpaceMMOPlanetTerrain.h"
#include "SpaceMMOCharacterPawn.h"
#include "SpaceMMOWalkModel.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FWalkConfig WalkTestConfig()
	{
		return FWalkConfig();
	}

	FPlanetConfig WalkTestPlanet()
	{
		FPlanetConfig Planet;
		Planet.Centre = FSystemCoordinate(FVector::ZeroVector);
		Planet.RadiusKilometres = 20.0;
		Planet.SurfaceGravity = 981.0;

		return Planet;
	}

	/** A perfectly smooth planet, so a walking test measures walking and not hills. */
	FPlanetTerrainConfig WalkTestSmoothTerrain()
	{
		FPlanetTerrainConfig Terrain;
		Terrain.MaxElevationKilometres = 0.0;

		return Terrain;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOWalkAlignsUpToSurfaceTest,
	"SpaceMMO.Walk.AlignsUpToSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOWalkAlignsUpToSurfaceTest::RunTest(const FString& Parameters)
{
	// Whatever the normal, the character's up must end up exactly along it. This is the entire
	// mechanism by which a sphere is walkable.
	const TArray<FVector> Normals = {
		FVector(0, 0, 1), FVector(0, 0, -1), FVector(1, 0, 0),
		FVector(-1, 0, 0), FVector(0.3, -0.5, 0.8).GetSafeNormal(),
	};

	for (const FVector& Normal : Normals)
	{
		const FQuat Aligned = FCharacterWalkModel::AlignToSurface(FQuat::Identity, Normal);

		TestTrue(
			*FString::Printf(TEXT("Up matches %s"), *Normal.ToCompactString()),
			Aligned.GetUpVector().Equals(Normal.GetSafeNormal(), 1e-6));

		// And it must still be a rotation, not a scale or a NaN.
		TestTrue(
			TEXT("Rotation stays normalised"),
			FMath::IsNearlyEqual(Aligned.Size(), 1.0, 1e-6));

		TestFalse(TEXT("Forward is not NaN"), Aligned.GetForwardVector().ContainsNaN());
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOWalkAlignmentKeepsHeadingTest,
	"SpaceMMO.Walk.AlignmentKeepsHeading",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOWalkAlignmentKeepsHeadingTest::RunTest(const FString& Parameters)
{
	// Standing still on unchanged ground must not spin the character round.
	const FQuat Facing = FRotationMatrix::MakeFromZX(
		FVector(0, 0, 1), FVector(1, 0, 0)).ToQuat();

	const FQuat Same = FCharacterWalkModel::AlignToSurface(Facing, FVector(0, 0, 1));

	TestTrue(TEXT("Heading survives a no-op alignment"),
		Same.GetForwardVector().Equals(Facing.GetForwardVector(), 1e-6));

	// The awkward case: a heading that has drifted parallel to the new normal leaves nothing to
	// project, and a naive implementation returns a NaN quaternion that makes the character vanish.
	const FQuat PointingUp = FRotationMatrix::MakeFromZX(
		FVector(1, 0, 0), FVector(0, 0, 1)).ToQuat();

	const FQuat Recovered = FCharacterWalkModel::AlignToSurface(PointingUp, FVector(0, 0, 1));

	TestFalse(TEXT("Degenerate heading does not produce NaN"),
		Recovered.GetForwardVector().ContainsNaN());

	TestTrue(TEXT("Up is still correct"),
		Recovered.GetUpVector().Equals(FVector(0, 0, 1), 1e-6));

	TestTrue(TEXT("Forward is still a real direction"),
		FMath::IsNearlyEqual(Recovered.GetForwardVector().Size(), 1.0, 1e-6));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOWalkReachesTopSpeedTest,
	"SpaceMMO.Walk.ReachesTopSpeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOWalkReachesTopSpeedTest::RunTest(const FString& Parameters)
{
	const FWalkConfig Config = WalkTestConfig();
	const FVector Up(0, 0, 1);

	FWalkState State;
	State.Rotation = FCharacterWalkModel::AlignToSurface(FQuat::Identity, Up);

	FWalkInput Input;
	Input.Move = FVector2D(1.0, 0.0);

	for (int32 Frame = 0; Frame < 240; ++Frame)
	{
		State = FCharacterWalkModel::Step(
			State, Input, Config, Up, FVector::ZeroVector, true, 1.0 / 60.0);
	}

	TestTrue(
		TEXT("Walks at the configured speed, not faster"),
		FMath::IsNearlyEqual(State.Velocity.Size(), Config.WalkSpeed, 1.0));

	// Diagonal input must not be faster than straight ahead — the classic bug where holding two
	// directions gives you 1.41x speed.
	FWalkState Diagonal;
	Diagonal.Rotation = State.Rotation;

	FWalkInput Both;
	Both.Move = FVector2D(1.0, 1.0);

	for (int32 Frame = 0; Frame < 240; ++Frame)
	{
		Diagonal = FCharacterWalkModel::Step(
			Diagonal, Both, Config, Up, FVector::ZeroVector, true, 1.0 / 60.0);
	}

	TestTrue(
		TEXT("Diagonal is not faster than forward"),
		Diagonal.Velocity.Size() <= Config.WalkSpeed + 1.0);

	// Releasing the stick brings it back to rest.
	FWalkInput None;

	for (int32 Frame = 0; Frame < 240; ++Frame)
	{
		State = FCharacterWalkModel::Step(
			State, None, Config, Up, FVector::ZeroVector, true, 1.0 / 60.0);
	}

	TestTrue(TEXT("Comes to a stop"), State.Velocity.Size() < 1.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOWalkJumpTest,
	"SpaceMMO.Walk.Jump",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOWalkJumpTest::RunTest(const FString& Parameters)
{
	const FWalkConfig Config = WalkTestConfig();
	const FVector Up(0, 0, 1);
	const FVector Gravity = Up * -981.0;

	FWalkState Grounded;
	Grounded.Rotation = FCharacterWalkModel::AlignToSurface(FQuat::Identity, Up);

	FWalkInput Jump;
	Jump.bJump = true;

	const FWalkState Jumped = FCharacterWalkModel::Step(
		Grounded, Jump, Config, Up, Gravity, true, 1.0 / 60.0);

	TestTrue(
		TEXT("Jumping launches along the surface normal"),
		FMath::IsNearlyEqual(FVector::DotProduct(Jumped.Velocity, Up), Config.JumpSpeed, 1e-6));

	// Not in mid-air, or a character can climb the sky one frame at a time.
	const FWalkState Airborne = FCharacterWalkModel::Step(
		Jumped, Jump, Config, Up, Gravity, false, 1.0 / 60.0);

	TestTrue(
		TEXT("A second jump in mid-air does nothing"),
		FVector::DotProduct(Airborne.Velocity, Up) < Config.JumpSpeed);

	// And gravity is pulling it back down while airborne.
	TestTrue(
		TEXT("Gravity applies in the air"),
		FVector::DotProduct(Airborne.Velocity, Up)
			< FVector::DotProduct(Jumped.Velocity, Up));

	// Standing on the ground, gravity must not accumulate into a downward speed that can never be
	// shed — the bug where a character sinks faster the longer it stands still.
	FWalkState Standing;
	Standing.Rotation = Grounded.Rotation;

	FWalkInput Still;

	for (int32 Frame = 0; Frame < 600; ++Frame)
	{
		Standing = FCharacterWalkModel::Step(
			Standing, Still, Config, Up, Gravity, true, 1.0 / 60.0);
	}

	TestTrue(
		TEXT("Standing still does not build downward speed"),
		FVector::DotProduct(Standing.Velocity, Up) >= -1e-6);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOWalkAroundThePlanetTest,
	"SpaceMMO.Walk.AroundThePlanet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOWalkAroundThePlanetTest::RunTest(const FString& Parameters)
{
	// The test the whole exercise exists for. Walk forward long enough and "down" has rotated
	// completely, without anything in the walk model knowing that happened.
	const FPlanetConfig Planet = WalkTestPlanet();
	const FPlanetTerrainConfig Terrain = WalkTestSmoothTerrain();

	FWalkConfig Config = WalkTestConfig();

	// Faster than a walk, or circumnavigating a 20 km planet takes three simulated hours — but
	// deliberately well below orbital velocity, which on this planet is only
	// sqrt(g * r) = sqrt(981 * 2e6) = about 0.44 km/s.
	//
	// This is a real constraint of the 1:10 universe scale, not a quirk of the test. Small planets
	// have low orbital velocities, and anything moving across the surface faster than that simply
	// leaves it — no amount of ground contact can hold something down when the ground is curving
	// away beneath it faster than gravity can pull it back. The first version of this test walked
	// at 2 km/s and flew off, which the code reported correctly.
	Config.WalkSpeed = 20000.0;

	const double GroundRadius = Planet.RadiusKilometres;
	const double CharacterRadius = 0.001;

	FSystemCoordinate Position(FVector(0.0, 0.0, GroundRadius + CharacterRadius));

	FWalkState State;
	State.Rotation = FCharacterWalkModel::AlignToSurface(FQuat::Identity, FVector(0, 0, 1));

	FWalkInput Forward;
	Forward.Move = FVector2D(1.0, 0.0);

	const FVector StartUp = Position.Kilometres.GetSafeNormal();

	double WorstAltitudeErrorMetres = 0.0;
	const double Delta = 1.0 / 60.0;

	for (int32 Frame = 0; Frame < 60 * 200; ++Frame)
	{
		const FVector Up = (Position.Kilometres - Planet.Centre.Kilometres).GetSafeNormal();

		const FGroundContact Contact = FPlanetTerrain::ResolveContact(
			Planet, Terrain, Position, State.Velocity, CharacterRadius);

		State.Velocity = Contact.Velocity;
		Position = Contact.Position;

		const FVector Gravity = FPlanetPhysics::GravityAcceleration(Planet, Position);

		State = FCharacterWalkModel::Step(
			State, Forward, Config, Contact.SurfaceNormal, Gravity, Contact.bOnGround, Delta);

		Position = FSystemCoordinate(
			Position.Kilometres + FCharacterWalkModel::PositionDeltaKilometres(State, Delta));

		const double Altitude =
			FPlanetTerrain::AltitudeAboveGroundKilometres(Planet, Terrain, Position);

		WorstAltitudeErrorMetres =
			FMath::Max(WorstAltitudeErrorMetres, FMath::Abs(Altitude - CharacterRadius) * 1000.0);
	}

	const FVector EndUp = (Position.Kilometres - Planet.Centre.Kilometres).GetSafeNormal();

	const double TravelledDegrees = FMath::RadiansToDegrees(FMath::Acos(
		FMath::Clamp(FVector::DotProduct(StartUp, EndUp), -1.0, 1.0)));

	// Genuinely somewhere else on the sphere, with up pointing a completely different way.
	TestTrue(
		*FString::Printf(TEXT("Travelled a long way around (%.1f degrees)"), TravelledDegrees),
		TravelledDegrees > 60.0);

	// And never left the ground on the way. A character that drifts off the surface, or sinks into
	// it, would show up here even though it walked the right distance.
	TestTrue(
		*FString::Printf(
			TEXT("Stayed on the surface (worst error %.2f m)"), WorstAltitudeErrorMetres),
		WorstAltitudeErrorMetres < 5.0);

	// Up really did rotate — the point of the whole exercise.
	TestFalse(TEXT("Up is no longer where it started"), EndUp.Equals(StartUp, 0.1));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOWalkTurnsAboutTheNormalTest,
	"SpaceMMO.Walk.TurnsAboutTheNormal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOWalkTurnsAboutTheNormalTest::RunTest(const FString& Parameters)
{
	const FWalkConfig Config = WalkTestConfig();

	// On the underside of a planet the surface normal points opposite world Z. Turning about world
	// Z instead of the normal would invert the controls exactly there, which is the sort of bug
	// that only shows up after someone walks a long way.
	const FVector Up(0, 0, -1);

	FWalkState State;
	State.Rotation = FCharacterWalkModel::AlignToSurface(FQuat::Identity, Up);

	const FVector StartForward = State.Rotation.GetForwardVector();

	FWalkInput Turning;
	Turning.Turn = 1.0;

	for (int32 Frame = 0; Frame < 30; ++Frame)
	{
		State = FCharacterWalkModel::Step(
			State, Turning, Config, Up, FVector::ZeroVector, true, 1.0 / 60.0);
	}

	const FVector EndForward = State.Rotation.GetForwardVector();

	// Half a second at 180 degrees per second is 90 degrees.
	const double TurnedDegrees = FMath::RadiansToDegrees(FMath::Acos(
		FMath::Clamp(FVector::DotProduct(StartForward, EndForward), -1.0, 1.0)));

	TestTrue(
		*FString::Printf(TEXT("Turned about 90 degrees (%.1f)"), TurnedDegrees),
		FMath::Abs(TurnedDegrees - 90.0) < 5.0);

	// Still standing on the surface, not tipped over by the turn.
	TestTrue(TEXT("Up is unchanged by turning"), State.Rotation.GetUpVector().Equals(Up, 1e-6));

	// The turn went the way the sign says, measured about the normal rather than about world Z.
	const double Signed = FVector::DotProduct(
		FVector::CrossProduct(StartForward, EndForward), Up);

	TestTrue(TEXT("Positive input turns one consistent way"), Signed > 0.0);

	return true;
}


/**
 * Ground speed is what the character crosses ground at, not how fast it is moving.
 *
 * <strong>The distinction is what stops a falling character sprinting in mid-air.</strong> An
 * animation blend space driven by total speed plays a faster run the further somebody falls, which
 * looks like a bug in the animation and is a bug in the number feeding it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOWalkGroundSpeedIgnoresFallingTest,
	"SpaceMMO.Walk.GroundSpeedIgnoresFalling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOWalkGroundSpeedIgnoresFallingTest::RunTest(const FString& Parameters)
{
	const FVector Up = FVector::UpVector;

	FWalkState State;

	// Three metres a second forward, five falling.
	State.Velocity = FVector(300.0, 0.0, -500.0);

	TestEqual(
		TEXT("Ground speed is the part across the ground"),
		FCharacterWalkModel::GroundSpeed(State, Up),
		300.0,
		0.001);

	TestEqual(
		TEXT("Vertical speed is negative while falling"),
		FCharacterWalkModel::VerticalSpeed(State, Up),
		-500.0,
		0.001);

	State.Velocity = FVector(0.0, 0.0, 420.0);

	TestEqual(
		TEXT("Standing still and rising crosses no ground"),
		FCharacterWalkModel::GroundSpeed(State, Up),
		0.0,
		0.001);

	// The sign is the whole point: it is what tells a jump from a fall, and they are different
	// animations. A magnitude would play the landing as somebody left the ground.
	TestTrue(
		TEXT("Vertical speed is positive while rising"),
		FCharacterWalkModel::VerticalSpeed(State, Up) > 0.0);

	return true;
}

/**
 * Movement direction is measured against the way the character faces, and against the ground it is
 * standing on.
 *
 * <strong>Both halves have cost this project a session in other forms.</strong> Measuring in world
 * axes gives an answer that is right at one point on a planet and wrong everywhere else, which is
 * the "up is the surface normal, never Z" lesson that the walk model, the camera and the terrain
 * material have each had to learn separately. So this checks the same four headings twice: once
 * standing at the north pole where up happens to be world Z, and once on the equator where it is
 * not.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOWalkMoveDirectionIsLocalTest,
	"SpaceMMO.Walk.MoveDirectionIsLocal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOWalkMoveDirectionIsLocalTest::RunTest(const FString& Parameters)
{
	// Two places on a planet with nothing in common but the maths: on top, where up is world Z, and
	// on the side, where it is world X and a world-axis implementation would be visibly wrong.
	struct FStance
	{
		const TCHAR* Where;
		FVector Up;
		FVector Forward;
	};

	const TArray<FStance> Stances =
	{
		{ TEXT("at the pole"), FVector::UpVector, FVector::ForwardVector },
		{ TEXT("on the equator"), FVector(1.0, 0.0, 0.0), FVector(0.0, 0.0, 1.0) },
	};

	for (const FStance& Stance : Stances)
	{
		const FVector Right = FVector::CrossProduct(Stance.Up, Stance.Forward);

		FWalkState State;

		State.Rotation = FRotationMatrix::MakeFromZX(Stance.Up, Stance.Forward).ToQuat();

		const double Speed = 400.0;

		State.Velocity = Stance.Forward * Speed;

		TestEqual(
			FString::Printf(TEXT("Walking forward reads as ahead %s"), Stance.Where),
			FCharacterWalkModel::MoveDirectionDegrees(State, Stance.Up),
			0.0,
			0.01);

		State.Velocity = Right * Speed;

		TestEqual(
			FString::Printf(TEXT("Strafing right reads as +90 %s"), Stance.Where),
			FCharacterWalkModel::MoveDirectionDegrees(State, Stance.Up),
			90.0,
			0.01);

		State.Velocity = -Right * Speed;

		TestEqual(
			FString::Printf(TEXT("Strafing left reads as -90 %s"), Stance.Where),
			FCharacterWalkModel::MoveDirectionDegrees(State, Stance.Up),
			-90.0,
			0.01);

		State.Velocity = -Stance.Forward * Speed;

		TestEqual(
			FString::Printf(TEXT("Walking backward reads as a half turn %s"), Stance.Where),
			FMath::Abs(FCharacterWalkModel::MoveDirectionDegrees(State, Stance.Up)),
			180.0,
			0.01);

		// The same world velocity, with the character turned to face it: the direction has to
		// follow the body, not the world. This is what fails if the heading is ignored and the
		// answer is computed from the velocity alone.
		State.Velocity = Right * Speed;
		State.Rotation = FRotationMatrix::MakeFromZX(Stance.Up, Right).ToQuat();

		TestEqual(
			FString::Printf(
				TEXT("Turning to face the way you are moving reads as ahead %s"), Stance.Where),
			FCharacterWalkModel::MoveDirectionDegrees(State, Stance.Up),
			0.0,
			0.01);
	}

	return true;
}

/**
 * Falling straight down has no direction to report, and must not invent one.
 *
 * Zero rather than a stale or arbitrary angle, because a blend space reads it every frame including
 * the frames where the speed weight is zero, and NaN in an animation graph is a character that
 * disappears rather than an error anybody sees.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOWalkMoveDirectionIsSafeWhenStillTest,
	"SpaceMMO.Walk.MoveDirectionIsSafeWhenStill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOWalkMoveDirectionIsSafeWhenStillTest::RunTest(const FString& Parameters)
{
	FWalkState State;

	State.Rotation = FQuat::Identity;
	State.Velocity = FVector::ZeroVector;

	TestEqual(
		TEXT("Standing still reports no direction"),
		FCharacterWalkModel::MoveDirectionDegrees(State, FVector::UpVector),
		0.0,
		0.001);

	State.Velocity = FVector(0.0, 0.0, -900.0);

	const double Falling =
		FCharacterWalkModel::MoveDirectionDegrees(State, FVector::UpVector);

	TestEqual(TEXT("Falling straight down reports no direction"), Falling, 0.0, 0.001);
	TestFalse(TEXT("And it is a number"), FMath::IsNaN(Falling));

	// A degenerate normal is what a cliff face or an unresolved frame can hand this, and it must
	// not produce a NaN that propagates into the pose.
	State.Velocity = FVector(300.0, 0.0, 0.0);

	const double Degenerate =
		FCharacterWalkModel::MoveDirectionDegrees(State, FVector::ZeroVector);

	TestFalse(TEXT("A zero normal still produces a number"), FMath::IsNaN(Degenerate));

	TestEqual(
		TEXT("A zero normal falls back to the plain speed"),
		FCharacterWalkModel::GroundSpeed(State, FVector::ZeroVector),
		300.0,
		0.001);

	return true;
}


/**
 * A model exported at any scale stands the height it is supposed to.
 *
 * The first character model imported at 98 cm — normalised to roughly one unit, and one unit
 * arriving as a metre — which on screen read as the ore deposit being enormous rather than the
 * person being half size. Deposits already solve this (FDepositPlacement::UniformScale) for the
 * same reason: exporters disagree about scale and the game should not.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOCharacterStandsAtItsHeightTest,
	"SpaceMMO.Walk.CharacterStandsAtItsHeight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOCharacterStandsAtItsHeightTest::RunTest(const FString& Parameters)
{
	// The real measurement off the first imported model, so this test has met the case it exists
	// for at least once.
	const double Authored = 98.0;
	const double Target = 180.0;

	const double Scale = ASpaceMMOCharacterPawn::UniformScaleForHeight(Authored, Target);

	TestEqual(TEXT("A 98 cm model scaled to stand 180"), Authored * Scale, Target, 0.001);

	// Uniform, so the shape survives. A per-axis fit would make every model that was not authored
	// at exactly the right proportions look squashed, and the artist would have no way to tell
	// whether their proportions were wrong or the game was lying about them.
	TestTrue(TEXT("A model already the right height is left alone"),
		FMath::IsNearlyEqual(
			ASpaceMMOCharacterPawn::UniformScaleForHeight(Target, Target), 1.0, 1e-9));

	TestEqual(
		TEXT("A model authored too large is scaled down"),
		ASpaceMMOCharacterPawn::UniformScaleForHeight(360.0, Target),
		0.5,
		1e-9);

	// Both refusals leave the model as authored rather than collapsing it to nothing, which would
	// read as a character that failed to spawn instead of a number nobody set.
	TestEqual(
		TEXT("A model with no height is left alone"),
		ASpaceMMOCharacterPawn::UniformScaleForHeight(0.0, Target),
		1.0,
		1e-9);

	TestEqual(
		TEXT("A zero target leaves the model at its authored size"),
		ASpaceMMOCharacterPawn::UniformScaleForHeight(Authored, 0.0),
		1.0,
		1e-9);

	return true;
}

#endif
