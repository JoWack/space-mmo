#include "Misc/AutomationTest.h"
#include "SpaceMMOBoarding.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOBoardingDisembarkRuleTest,
	"SpaceMMO.Boarding.DisembarkRule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOBoardingDisembarkRuleTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Landed ships can be left"), FBoarding::CanDisembark(true));

	// Not in flight. There is no EVA, so opening the door in space is a slow death nobody chose.
	TestFalse(TEXT("Flying ships cannot be left"), FBoarding::CanDisembark(false));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOBoardingRangeTest,
	"SpaceMMO.Boarding.Range",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOBoardingRangeTest::RunTest(const FString& Parameters)
{
	const FSystemCoordinate Ship(FVector(200.0, 0.0, 20.0));

	// Standing on it.
	TestTrue(TEXT("Touching the ship is in range"), FBoarding::CanEmbark(Ship, Ship));

	// Just inside and just outside, taken from the range rather than written out.
	//
	// This used to say 0.09 and 0.11 against a range of 0.1, and so it failed the day the range
	// changed -- not because anything was wrong, but because it had been asserting the number
	// instead of the rule. The rule is that inside is in and outside is not, at whatever distance
	// the range happens to be, and a test that has to be edited when a value is tuned trains whoever
	// is tuning it to edit tests without reading them.
	const double Range = FBoarding::DefaultBoardingRangeKilometres;

	const FSystemCoordinate Near(Ship.Kilometres + FVector(Range * 0.9, 0.0, 0.0));

	TestTrue(TEXT("Just inside the range is in range"), FBoarding::CanEmbark(Near, Ship));

	// A client asking to board from further away is asking, not telling.
	const FSystemCoordinate Far(Ship.Kilometres + FVector(Range * 1.1, 0.0, 0.0));

	TestFalse(TEXT("Just outside it is not"), FBoarding::CanEmbark(Far, Ship));

	// And the range is a walk rather than a flight. Boarding from further than a hull's length or
	// two away is what made stepping out thirty metres look normal for as long as it did.
	TestTrue(
		TEXT("A ship is boarded from beside it, not from across a field"),
		Range <= 0.05);

	// And nowhere near, which is what a hostile client would send.
	const FSystemCoordinate Absurd(Ship.Kilometres + FVector(500.0, 0.0, 0.0));

	TestFalse(TEXT("Half a light-second away is refused"), FBoarding::CanEmbark(Absurd, Ship));

	// Range is measured in every direction, not just along one axis.
	const FSystemCoordinate Diagonal(
		Ship.Kilometres + FVector(Range * 0.6, Range * 0.6, Range * 0.6));

	TestEqual(
		TEXT("Diagonal distance is measured properly"),
		FBoarding::CanEmbark(Diagonal, Ship),
		(Diagonal.Kilometres - Ship.Kilometres).Size() <= FBoarding::DefaultBoardingRangeKilometres);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOBoardingStepOutTest,
	"SpaceMMO.Boarding.StepOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOBoardingStepOutTest::RunTest(const FString& Parameters)
{
	const FSystemCoordinate Ship(FVector(200.0, 0.0, 20.0));
	const FVector Up(0.0, 0.0, 1.0);
	const FVector Right(0.0, 1.0, 0.0);

	const FSystemCoordinate Out = FBoarding::StepOutPosition(Ship, Up, Right);

	// Beside the ship, not inside it.
	const FVector Offset = Out.Kilometres - Ship.Kilometres;

	TestTrue(TEXT("Actually moved"), Offset.Size() > 0.02);

	// Sideways along the ground, with only the small deliberate lift along up.
	const double Along = FVector::DotProduct(Offset, Up);

	TestTrue(TEXT("Lifted slightly, not buried"), Along > 0.0 && Along < 0.01);

	const FVector Tangential = Offset - (Up * Along);

	TestTrue(
		TEXT("Stepped along the ship's right side"),
		FVector::DotProduct(Tangential.GetSafeNormal(), Right) > 0.99);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOBoardingStepOutOnASlopeTest,
	"SpaceMMO.Boarding.StepOutOnASlope",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOBoardingStepOutOnASlopeTest::RunTest(const FString& Parameters)
{
	const FSystemCoordinate Ship(FVector(200.0, 0.0, 20.0));

	// A ship parked nose-down on a slope: its right vector has a large component along the surface
	// normal. Using it unflattened would put the character underground.
	const FVector Up = FVector(0.2, 0.0, 1.0).GetSafeNormal();
	const FVector TiltedRight = FVector(0.0, 0.6, 0.8).GetSafeNormal();

	const FSystemCoordinate Out = FBoarding::StepOutPosition(Ship, Up, TiltedRight);
	const FVector Offset = Out.Kilometres - Ship.Kilometres;

	const double Along = FVector::DotProduct(Offset, Up);

	TestTrue(TEXT("Never below the ground plane"), Along >= 0.0);
	TestTrue(TEXT("Only the deliberate lift"), Along < 0.01);

	// The degenerate case: a ship rolled so its right vector points straight along the normal,
	// leaving nothing to flatten. It still has to produce a usable direction rather than a zero.
	const FSystemCoordinate Rolled = FBoarding::StepOutPosition(Ship, Up, Up);
	const FVector RolledOffset = Rolled.Kilometres - Ship.Kilometres;

	TestFalse(TEXT("Degenerate roll does not produce NaN"), RolledOffset.ContainsNaN());
	TestTrue(TEXT("Degenerate roll still steps aside"), RolledOffset.Size() > 0.02);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOBoardingWorksAnywhereOnASphereTest,
	"SpaceMMO.Boarding.WorksAnywhereOnASphere",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOBoardingWorksAnywhereOnASphereTest::RunTest(const FString& Parameters)
{
	// A player can land anywhere, including the poles, where a fixed reference axis degenerates.
	const TArray<FVector> Normals = {
		FVector(0, 0, 1), FVector(0, 0, -1), FVector(1, 0, 0),
		FVector(-1, 0, 0), FVector(0, 1, 0), FVector(0.4, -0.5, 0.7).GetSafeNormal(),
	};

	const FSystemCoordinate Ship(FVector(200.0, 0.0, 0.0));

	for (const FVector& Up : Normals)
	{
		// Right chosen adversarially: exactly along the normal, so there is nothing to flatten.
		const FSystemCoordinate Out = FBoarding::StepOutPosition(Ship, Up, Up);
		const FVector Offset = Out.Kilometres - Ship.Kilometres;

		TestFalse(
			*FString::Printf(TEXT("No NaN at %s"), *Up.ToCompactString()),
			Offset.ContainsNaN());

		TestTrue(
			*FString::Printf(TEXT("Steps aside at %s"), *Up.ToCompactString()),
			Offset.Size() > 0.02);

		// And never into the ground.
		TestTrue(
			*FString::Printf(TEXT("Not underground at %s"), *Up.ToCompactString()),
			FVector::DotProduct(Offset, Up) >= 0.0);
	}

	return true;
}


/**
 * Stepping out of a ship leaves you facing the way the ship is pointing.
 *
 * <strong>Not the way you stepped.</strong> A character steps out sideways, and facing along the
 * step-out direction leaves somebody staring at their own hull; the ship's nose is the direction
 * they flew in from and the direction everything they landed for is. Nothing set this at all before
 * -- a freshly spawned pawn faced wherever it happened to.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOBoardingStepsOutFacingTheShipsNoseTest,
	"SpaceMMO.Boarding.StepsOutFacingTheShipsNose",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOBoardingStepsOutFacingTheShipsNoseTest::RunTest(const FString& Parameters)
{
	const FVector Up(0.0, 0.0, 1.0);

	// A ship pointing along +Y on flat ground.
	const FQuat Facing = FBoarding::StepOutRotation(Up, FVector(0.0, 1.0, 0.0));

	TestTrue(
		TEXT("The character faces the ship's nose"),
		FVector::DotProduct(Facing.GetForwardVector(), FVector(0.0, 1.0, 0.0)) > 0.999);

	TestTrue(
		TEXT("And stands up out of the ground"),
		FVector::DotProduct(Facing.GetUpVector(), Up) > 0.999);

	// A ship parked nose-up on a slope has a forward with a component along the normal. Using it
	// unmodified leans the character; flattening keeps them upright and pointing the same way.
	const FVector Tilted = FVector(0.0, 1.0, 1.0).GetSafeNormal();

	const FQuat Flattened = FBoarding::StepOutRotation(Up, Tilted);

	TestTrue(
		TEXT("A ship pitched up still leaves the character upright"),
		FVector::DotProduct(Flattened.GetUpVector(), Up) > 0.999);

	TestTrue(
		TEXT("...and still facing the way its nose points across the ground"),
		FVector::DotProduct(Flattened.GetForwardVector(), FVector(0.0, 1.0, 0.0)) > 0.999);

	// The degenerate case, which is what makes this worth a function rather than two lines at the
	// call site: a ship nose-straight-up leaves nothing to flatten, and the naive version produces
	// a NaN rotation. A character with a NaN transform does not face the wrong way, it vanishes.
	const FQuat NoseUp = FBoarding::StepOutRotation(Up, Up);

	TestFalse(TEXT("A ship parked nose-up does not produce a NaN"), NoseUp.ContainsNaN());

	TestTrue(
		TEXT("...and still leaves the character standing on the ground"),
		FVector::DotProduct(NoseUp.GetUpVector(), Up) > 0.999);

	// Up itself being useless is the caller having no ground to stand on. Identity is a heading
	// somebody can walk out of; a NaN is not.
	const FQuat NoGround =
		FBoarding::StepOutRotation(FVector::ZeroVector, FVector(1.0, 0.0, 0.0));

	TestFalse(TEXT("No surface normal produces no NaN either"), NoGround.ContainsNaN());

	return true;
}

#endif
