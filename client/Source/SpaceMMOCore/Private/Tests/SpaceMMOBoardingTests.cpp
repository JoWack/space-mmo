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

	// Just inside fifty metres.
	const FSystemCoordinate Near(Ship.Kilometres + FVector(0.04, 0.0, 0.0));

	TestTrue(TEXT("Forty metres away is in range"), FBoarding::CanEmbark(Near, Ship));

	// Just outside. A client asking to board from further away is asking, not telling.
	const FSystemCoordinate Far(Ship.Kilometres + FVector(0.06, 0.0, 0.0));

	TestFalse(TEXT("Sixty metres away is not"), FBoarding::CanEmbark(Far, Ship));

	// And nowhere near, which is what a hostile client would send.
	const FSystemCoordinate Absurd(Ship.Kilometres + FVector(500.0, 0.0, 0.0));

	TestFalse(TEXT("Half a light-second away is refused"), FBoarding::CanEmbark(Absurd, Ship));

	// Range is measured in every direction, not just along one axis.
	const FSystemCoordinate Diagonal(
		Ship.Kilometres + FVector(0.03, 0.03, 0.03));

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

#endif
