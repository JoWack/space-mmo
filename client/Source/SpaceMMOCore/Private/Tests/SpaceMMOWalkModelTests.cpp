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


/**
 * The direction a blend space is told about matches the key that was pressed.
 *
 * <strong>Every other test of this drives the maths with a velocity built by hand, and that is
 * exactly the gap this project has been bitten through before</strong> — five green tests over a
 * market panel all constructed their own inputs, and the one value the server actually sent was
 * the one nobody passed in. So this one presses the key: it runs the real Step with a real input
 * and asks what the animation graph would be handed.
 *
 * Written after a playtest reported strafing right playing the left animation, to settle whether
 * the sign was wrong in the code or the samples were placed mirrored in the blend space. It was
 * not the code, and this is what says so next time.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOWalkStrafeReadsAsTheKeyPressedTest,
	"SpaceMMO.Walk.StrafeReadsAsTheKeyPressed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOWalkStrafeReadsAsTheKeyPressedTest::RunTest(const FString& Parameters)
{
	const FVector Up = FVector(0.0, 0.0, 1.0);

	struct FPress
	{
		const TCHAR* What;
		FVector2D Move;
		double Expected;
	};

	// Move.X is forward and Move.Y is right, as the pawn's MoveForward and MoveRight set them.
	const TArray<FPress> Presses =
	{
		{ TEXT("W"), FVector2D(1.0, 0.0), 0.0 },
		{ TEXT("D"), FVector2D(0.0, 1.0), 90.0 },
		{ TEXT("A"), FVector2D(0.0, -1.0), -90.0 },
		{ TEXT("S"), FVector2D(-1.0, 0.0), 180.0 },
	};

	for (const FPress& Press : Presses)
	{
		FWalkState State;

		State.Rotation = FCharacterWalkModel::AlignToSurface(FQuat::Identity, Up);

		FWalkInput Input;

		Input.Move = Press.Move;

		// Long enough to be moving properly rather than reading the first frame of acceleration.
		for (int32 Step = 0; Step < 60; ++Step)
		{
			State = FCharacterWalkModel::Step(
				State, Input, FWalkConfig(), Up, FVector::ZeroVector, true, 1.0 / 60.0);
		}

		TestTrue(
			FString::Printf(TEXT("Holding %s actually moves"), Press.What),
			FCharacterWalkModel::GroundSpeed(State, Up) > 100.0);

		const double Direction = FCharacterWalkModel::MoveDirectionDegrees(State, Up);

		// Backward sits on the wrap, where either sign is the same heading.
		const double Measured =
			FMath::IsNearlyEqual(FMath::Abs(Press.Expected), 180.0)
				? FMath::Abs(Direction)
				: Direction;

		TestEqual(
			FString::Printf(
				TEXT("Holding %s reads as %.0f degrees"), Press.What, Press.Expected),
			Measured,
			FMath::Abs(Press.Expected) == 180.0 ? 180.0 : Press.Expected,
			0.5);
	}

	return true;
}


/**
 * A body turning to face travel takes the short way round.
 *
 * <strong>The wrap is the whole test.</strong> Running backwards puts the direction at plus or
 * minus 180, and the sign flips on numerical noise — so a body that turned by the raw difference
 * would spin a full circle every time the last digit changed. The same discontinuity froze a blend
 * space earlier the same day, which is how it earned a test of its own here.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOWalkBodyTurnsTheShortWayTest,
	"SpaceMMO.Walk.BodyTurnsTheShortWay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOWalkBodyTurnsTheShortWayTest::RunTest(const FString& Parameters)
{
	// Straight across the wrap: 170 to -170 is twenty degrees, not three hundred and forty.
	TestEqual(
		TEXT("Ten degrees from 170 toward -170 lands on 180"),
		FMath::Abs(ASpaceMMOCharacterPawn::TurnTowards(170.0, -170.0, 10.0)),
		180.0,
		0.001);

	TestEqual(
		TEXT("And the other way round"),
		FMath::Abs(ASpaceMMOCharacterPawn::TurnTowards(-170.0, 170.0, 10.0)),
		180.0,
		0.001);

	// A step it can complete lands exactly, rather than stepping past and oscillating -- which
	// reads as a body that jitters while running.
	TestEqual(
		TEXT("A reachable target is landed on exactly"),
		ASpaceMMOCharacterPawn::TurnTowards(0.0, 45.0, 90.0),
		45.0,
		0.001);

	TestEqual(
		TEXT("Already facing the right way does not move"),
		ASpaceMMOCharacterPawn::TurnTowards(90.0, 90.0, 30.0),
		90.0,
		0.001);

	// Direction of travel, not just magnitude: turning toward +90 must not go to -90.
	TestEqual(
		TEXT("Turning right goes right"),
		ASpaceMMOCharacterPawn::TurnTowards(0.0, 90.0, 30.0),
		30.0,
		0.001);

	TestEqual(
		TEXT("Turning left goes left"),
		ASpaceMMOCharacterPawn::TurnTowards(0.0, -90.0, 30.0),
		-30.0,
		0.001);

	// Repeated steps converge rather than orbiting the target forever.
	double Facing = 0.0;

	for (int32 Step = 0; Step < 60; ++Step)
	{
		Facing = ASpaceMMOCharacterPawn::TurnTowards(Facing, -135.0, 12.0);
	}

	TestEqual(TEXT("A body turning every frame settles on its heading"), Facing, -135.0, 0.001);

	return true;
}


/**
 * Walking into a wall stops you against it, and does not stop you sliding along it.
 *
 * <strong>Both halves are the test.</strong> Removing all velocity on contact freezes a character
 * the moment they brush anything, which reads as the controls dying; reflecting it makes a wall a
 * trampoline. The ground already resolves this way in FPlanetTerrain::ResolveContact, and a wall
 * that behaved differently from the floor would be two rules for one idea.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOWalkStopsAtAWallAndSlidesAlongItTest,
	"SpaceMMO.Walk.StopsAtAWallAndSlidesAlongIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOWalkStopsAtAWallAndSlidesAlongItTest::RunTest(const FString& Parameters)
{
	// A wall facing back along -X: its outward normal points at a character walking in +X.
	const FVector Wall(-1.0, 0.0, 0.0);

	FWalkState Straight;

	Straight.Velocity = FVector(600.0, 0.0, 0.0);

	const FWalkState Stopped = FCharacterWalkModel::ResolveBlockingHit(Straight, Wall, 0.0);

	TestEqual(
		TEXT("Walking straight into a wall takes all of the motion into it"),
		Stopped.Velocity.X,
		0.0,
		0.001);

	// Straight on, so there is nothing along the wall to keep -- but the character must be stopped
	// rather than thrown back.
	TestTrue(
		TEXT("And does not bounce off it"),
		Stopped.Velocity.Size() < 0.001);

	// The case that matters for feel: approaching at an angle should shed only the part heading
	// into the wall and keep the part running along it.
	FWalkState Glancing;

	Glancing.Velocity = FVector(600.0, 400.0, 0.0);

	const FWalkState Sliding = FCharacterWalkModel::ResolveBlockingHit(Glancing, Wall, 0.0);

	TestEqual(TEXT("The part into the wall is gone"), Sliding.Velocity.X, 0.0, 0.001);

	TestEqual(
		TEXT("The part along the wall is untouched, so a character slides rather than sticking"),
		Sliding.Velocity.Y,
		400.0,
		0.001);

	// Walking away from a surface you are touching must not be interfered with, or a character
	// standing against a wall could never leave it.
	FWalkState Leaving;

	Leaving.Velocity = FVector(-600.0, 0.0, 0.0);

	const FWalkState Left = FCharacterWalkModel::ResolveBlockingHit(Leaving, Wall, 0.0);

	TestEqual(
		TEXT("Moving away from the surface is left alone"), Left.Velocity.X, -600.0, 0.001);

	// A hit with no usable normal names no direction to push along. Leaving the state untouched
	// lets the next frame try again; inventing a direction would move the character arbitrarily.
	const FWalkState Degenerate =
		FCharacterWalkModel::ResolveBlockingHit(Straight, FVector::ZeroVector, 5.0);

	TestEqual(
		TEXT("A hit with no normal changes nothing"),
		Degenerate.Velocity.X,
		Straight.Velocity.X,
		0.001);

	return true;
}

/**
 * Being pushed out of a surface goes a hair further than exactly clear.
 *
 * Resolving to exactly touching leaves the next frame's sweep starting inside the surface by
 * whatever floating point does, so the character alternates between clear and penetrating and
 * jitters against every wall. The ground avoids the same failure with its contact tolerance.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOWalkSeparationClearsTheSurfaceTest,
	"SpaceMMO.Walk.SeparationClearsTheSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOWalkSeparationClearsTheSurfaceTest::RunTest(const FString& Parameters)
{
	TestTrue(
		TEXT("A character exactly touching is still pushed clear"),
		FCharacterWalkModel::SeparationCentimetres(0.0) > 0.0);

	TestTrue(
		TEXT("A character ten centimetres inside is pushed further than ten"),
		FCharacterWalkModel::SeparationCentimetres(10.0) > 10.0);

	// Imperceptible. A push large enough to see would read as being shoved by the scenery.
	TestTrue(
		TEXT("But not far enough for anybody to notice"),
		FCharacterWalkModel::SeparationCentimetres(10.0) < 11.0);

	// A negative depth is a caller saying "not actually penetrating"; it must not pull the
	// character into the surface.
	TestTrue(
		TEXT("A negative depth never pulls the character inward"),
		FCharacterWalkModel::SeparationCentimetres(-5.0) > 0.0);

	return true;
}


/**
 * A step that runs into a wall spends the rest of itself along the wall.
 *
 * <strong>Written against a measured bug, not a hypothetical one.</strong> Resolving a blocking hit
 * by clamping to the contact point and stopping looks correct and reads as sliding in a diagram. In
 * the game it pinned a character under a ship hull: the log showed 638 consecutive blocked frames
 * covering 34 cm, which is six centimetres a second against a walk speed of six hundred. Every
 * frame of the step past first contact was being thrown away, and the only motion left was the
 * separation push. This is the arithmetic that gives that motion somewhere to go.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOWalkSpendsTheRestOfTheStepAlongTheWallTest,
	"SpaceMMO.Walk.SpendsTheRestOfTheStepAlongTheWall",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOWalkSpendsTheRestOfTheStepAlongTheWallTest::RunTest(const FString& Parameters)
{
	// A wall facing back along -X, met by a step heading diagonally into it.
	const FVector Wall(-1.0, 0.0, 0.0);

	const FVector Slid =
		FCharacterWalkModel::SlideDeltaCentimetres(FVector(6.0, 4.0, 0.0), Wall);

	TestEqual(TEXT("Nothing is left heading into the wall"), Slid.X, 0.0, 0.001);

	TestEqual(
		TEXT("And everything running along it survives, so the step is not thrown away"),
		Slid.Y,
		4.0,
		0.001);

	// The case the bug was: a step almost parallel to the surface must keep almost all of itself.
	// Losing it is what turned walking into a six-centimetres-a-second crawl.
	const FVector Grazing =
		FCharacterWalkModel::SlideDeltaCentimetres(FVector(0.2, 5.0, 0.0), Wall);

	TestTrue(
		TEXT("A glancing step keeps nearly all of its length"),
		Grazing.Size() > 4.9);

	// Leaving a surface you are touching must not be interfered with, or a character resolved into
	// contact could never be resolved back out of it.
	const FVector Leaving =
		FCharacterWalkModel::SlideDeltaCentimetres(FVector(-6.0, 0.0, 0.0), Wall);

	TestEqual(TEXT("A step away from the surface is untouched"), Leaving.X, -6.0, 0.001);

	// Straight on there is genuinely nothing along the wall to spend, and the character should
	// stop rather than be deflected somewhere it did not ask to go.
	const FVector HeadOn =
		FCharacterWalkModel::SlideDeltaCentimetres(FVector(6.0, 0.0, 0.0), Wall);

	TestTrue(TEXT("A head-on step keeps nothing"), HeadOn.Size() < 0.001);

	// A hit with no usable normal names no direction, and a position is spent the moment it is
	// applied. Spending nothing costs a frame; guessing costs a character inside the scenery.
	const FVector Unknown =
		FCharacterWalkModel::SlideDeltaCentimetres(FVector(6.0, 4.0, 0.0), FVector::ZeroVector);

	TestTrue(TEXT("A hit with no normal moves the character nowhere"), Unknown.IsNearlyZero());

	return true;
}


/**
 * What counts as a floor when the floor is geometry rather than the height field.
 *
 * <strong>Written from the cases that break a building, not from the rule.</strong> A station's
 * slab, its stair ramp and its outside wall are all surfaces a downward probe finds; only two of
 * them are floors, and which two is the whole of this function. The A-02 stairs are a 26.6 degree
 * ramp on purpose, so that angle is a case here rather than a number in a comment.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOWalkStandsOnAFloorAndNotOnAWallTest,
	"SpaceMMO.Walk.StandsOnAFloorAndNotOnAWall",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOWalkStandsOnAFloorAndNotOnAWallTest::RunTest(const FString& Parameters)
{
	const FVector Up(0.0, 0.0, 1.0);
	const FVector Flat(0.0, 0.0, 1.0);

	TestTrue(
		TEXT("A slab underfoot is stood on"),
		FCharacterWalkModel::StandsOn(Flat, Up, 0.0, 0.0, false));

	// Resolved into the floor by a step or by gravity. It must still count, or the character is
	// airborne while inside the thing holding it up and falls through.
	TestTrue(
		TEXT("A floor the feet are slightly inside is stood on"),
		FCharacterWalkModel::StandsOn(Flat, Up, -5.0, 0.0, false));

	// The A-02 stair ramp. If this ever stops passing, the stairs stop being climbable.
	const double RampDegrees = 26.6;

	const FVector Ramp(
		FMath::Sin(FMath::DegreesToRadians(RampDegrees)),
		0.0,
		FMath::Cos(FMath::DegreesToRadians(RampDegrees)));

	TestTrue(
		TEXT("The stair ramp is a floor"),
		FCharacterWalkModel::StandsOn(Ramp, Up, 0.0, 0.0, true));

	// A downward probe run alongside a wall finds the wall. Standing on one would let a character
	// walk up the outside of the building.
	const FVector Wall(1.0, 0.0, 0.0);

	TestFalse(
		TEXT("A wall is not a floor, however close the feet are to it"),
		FCharacterWalkModel::StandsOn(Wall, Up, 0.0, 0.0, true));

	// Just past the limit, to pin the boundary rather than only the obvious cases.
	const double TooSteep = FCharacterWalkModel::SteepestWalkableSlopeDegrees + 2.0;

	const FVector Cliff(
		FMath::Sin(FMath::DegreesToRadians(TooSteep)),
		0.0,
		FMath::Cos(FMath::DegreesToRadians(TooSteep)));

	TestFalse(
		TEXT("Nor is anything steeper than the walkable limit"),
		FCharacterWalkModel::StandsOn(Cliff, Up, 0.0, 0.0, true));

	return true;
}

/**
 * Standing has to be harder to lose than to gain, and a jump has to win outright.
 *
 * The same two rules FPlanetTerrain::ResolveContact reaches for, and they are here for the same
 * reasons: one threshold cannot both keep a character attached over a step and let one leave the
 * ground on purpose.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOWalkKeepsItsFooterOverAStepTest,
	"SpaceMMO.Walk.KeepsItsFooterOverAStep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOWalkKeepsItsFooterOverAStepTest::RunTest(const FString& Parameters)
{
	const FVector Up(0.0, 0.0, 1.0);
	const FVector Flat(0.0, 0.0, 1.0);

	// A gap between the two bands: reached only by someone already walking.
	const double Step =
		(FCharacterWalkModel::FloorCaptureGapCentimetres
			+ FCharacterWalkModel::FloorReleaseGapCentimetres)
		* 0.5;

	TestTrue(
		TEXT("A walker stays attached stepping down onto a floor below the capture band"),
		FCharacterWalkModel::StandsOn(Flat, Up, Step, 0.0, true));

	TestFalse(
		TEXT("But falling onto the same floor from that height does not begin standing early"),
		FCharacterWalkModel::StandsOn(Flat, Up, Step, 0.0, false));

	// A jump. Without this the band drags a rising character straight back onto the floor and the
	// jump key does nothing at all -- which is the failure ground contact already had once.
	TestFalse(
		TEXT("Climbing away from a floor leaves it"),
		FCharacterWalkModel::StandsOn(Flat, Up, 3.0, 400.0, true));

	// Rising while resolved into the floor is not a jump, it is the frame a lift happens on. The
	// character has to keep its footing or the very act of being stood up ends the standing.
	TestTrue(
		TEXT("Rising while still inside the floor keeps its footing"),
		FCharacterWalkModel::StandsOn(Flat, Up, -2.0, 400.0, true));

	// Far below anything: the probe reaches further than the band, so this is the case where
	// something was found and correctly ignored.
	TestFalse(
		TEXT("A floor beyond the release band is not stood on"),
		FCharacterWalkModel::StandsOn(
			Flat, Up, FCharacterWalkModel::FloorReleaseGapCentimetres + 1.0, 0.0, true));

	return true;
}


/**
 * Sprint raises the speed a character ends up at, not the rate they get there.
 *
 * <strong>A ceiling, not a shove.</strong> Adding a push would make tapping the key a lunge;
 * multiplying the acceleration would make a sprinting character turn sharper than a walking one,
 * which reads as the controls changing under you. The model accelerates toward a higher number and
 * is otherwise untouched, which is also what lets the animation blend space keep working on ground
 * speed alone.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOWalkSprintRaisesTheCeilingTest,
	"SpaceMMO.Walk.SprintRaisesTheCeiling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOWalkSprintRaisesTheCeilingTest::RunTest(const FString& Parameters)
{
	const FVector Up(0.0, 0.0, 1.0);
	const FVector NoGravity = FVector::ZeroVector;

	FWalkConfig Config;

	FWalkInput Walking;
	Walking.Move = FVector2D(1.0, 0.0);

	FWalkInput Running = Walking;
	Running.bSprint = true;

	// Long enough for both to have reached whatever they are heading for.
	FWalkState Walked;
	FWalkState Ran;

	for (int32 Frames = 0; Frames < 240; ++Frames)
	{
		Walked = FCharacterWalkModel::Step(
			Walked, Walking, Config, Up, NoGravity, true, 1.0 / 60.0);

		Ran = FCharacterWalkModel::Step(Ran, Running, Config, Up, NoGravity, true, 1.0 / 60.0);
	}

	TestEqual(
		TEXT("Walking settles at the walk speed"),
		FCharacterWalkModel::GroundSpeed(Walked, Up), Config.WalkSpeed, 1.0);

	TestEqual(
		TEXT("Sprinting settles at the walk speed times the multiplier"),
		FCharacterWalkModel::GroundSpeed(Ran, Up),
		Config.WalkSpeed * Config.SprintMultiplier,
		1.0);

	// One step from a standstill, where the difference between raising the ceiling and adding a
	// push would show: both are still accelerating at the same rate, so both are in the same place.
	const FWalkState FirstWalk =
		FCharacterWalkModel::Step(FWalkState(), Walking, Config, Up, NoGravity, true, 1.0 / 60.0);

	const FWalkState FirstRun =
		FCharacterWalkModel::Step(FWalkState(), Running, Config, Up, NoGravity, true, 1.0 / 60.0);

	TestEqual(
		TEXT("And the first step off the mark is the same either way"),
		FCharacterWalkModel::GroundSpeed(FirstRun, Up),
		FCharacterWalkModel::GroundSpeed(FirstWalk, Up),
		0.001);

	// It has to survive the trip to the server, which is the half a client-side speed change would
	// get wrong: the server integrates this model, so a sprint it never hears about is a client
	// disagreeing with it about where the character is.
	FWalkInput Hostile;
	Hostile.Move = FVector2D(5.0, 5.0);
	Hostile.bSprint = true;

	TestTrue(TEXT("Sanitising input keeps the sprint"), Hostile.Sanitised().bSprint);

	return true;
}

#endif
