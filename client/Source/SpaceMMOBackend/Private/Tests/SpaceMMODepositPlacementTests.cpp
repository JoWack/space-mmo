#include "Misc/AutomationTest.h"
#include "SpaceMMODepositSettings.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMODepositFitsATallModelTest,
	"SpaceMMO.Deposit.FitsATallModel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMODepositFitsATallModelTest::RunTest(const FString& Parameters)
{
	// A metre wide and ten metres tall. Height is the binding constraint, so the fit is driven by
	// it and the result comes out narrower than the width budget rather than taller than the
	// height one.
	const FVector Extent(50.0, 50.0, 500.0);

	const double Scale = FDepositPlacement::UniformScale(Extent);

	TestEqual(
		TEXT("Scaled to the target height"),
		Extent.Z * 2.0 * Scale,
		FDepositPlacement::TargetHeightCentimetres,
		0.001);

	TestTrue(
		TEXT("Within the width budget"),
		Extent.X * 2.0 * Scale <= FDepositPlacement::TargetWidthCentimetres + 0.001);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMODepositFitsAWideModelTest,
	"SpaceMMO.Deposit.FitsAWideModel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMODepositFitsAWideModelTest::RunTest(const FString& Parameters)
{
	// Ten metres across and half a metre tall: a slab. Width binds this time, and the model must
	// end up short rather than being stretched to fill the height.
	const FVector Extent(500.0, 500.0, 25.0);

	const double Scale = FDepositPlacement::UniformScale(Extent);

	TestEqual(
		TEXT("Scaled to the target width"),
		Extent.X * 2.0 * Scale,
		FDepositPlacement::TargetWidthCentimetres,
		0.001);

	TestTrue(
		TEXT("Within the height budget"),
		Extent.Z * 2.0 * Scale <= FDepositPlacement::TargetHeightCentimetres + 0.001);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMODepositKeepsProportionsTest,
	"SpaceMMO.Deposit.KeepsProportions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMODepositKeepsProportionsTest::RunTest(const FString& Parameters)
{
	// The reason the scale is uniform at all. Stretching each axis to its own target would squash
	// every model not authored at exactly the target ratio, and an artist would have no way to tell
	// whether their proportions were wrong or the game was misrepresenting them.
	const FVector Extent(30.0, 30.0, 90.0);

	const double Scale = FDepositPlacement::UniformScale(Extent);

	const double WidthAfter = Extent.X * 2.0 * Scale;
	const double HeightAfter = Extent.Z * 2.0 * Scale;

	TestEqual(TEXT("Ratio preserved"), HeightAfter / WidthAfter, 3.0, 0.001);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMODepositSeatsEitherPivotTest,
	"SpaceMMO.Deposit.SeatsEitherPivot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMODepositSeatsEitherPivotTest::RunTest(const FString& Parameters)
{
	const FVector Extent(50.0, 50.0, 150.0);
	const double Scale = FDepositPlacement::UniformScale(Extent);

	// Pivot on the ground: bounds centre sits one half-height above the origin, and nothing needs
	// lifting.
	const double BasePivot =
		FDepositPlacement::BaseLift(FVector(0.0, 0.0, Extent.Z), Extent, Scale);

	TestEqual(TEXT("A base pivot needs no lift"), BasePivot, 0.0, 0.001);

	// Pivot in the middle, which is how the engine primitives are built: half the model is below
	// the origin and would be underground.
	const double CentrePivot =
		FDepositPlacement::BaseLift(FVector::ZeroVector, Extent, Scale);

	TestEqual(
		TEXT("A centred pivot lifts by half its scaled height"),
		CentrePivot,
		Extent.Z * Scale,
		0.001);

	// Both conventions are common and neither is wrong. Assuming one would half-bury or float every
	// model authored the other way, and a rock sunk into a hillside reports no error anywhere.
	TestTrue(TEXT("The two differ"), !FMath::IsNearlyEqual(BasePivot, CentrePivot));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMODepositSurvivesEmptyBoundsTest,
	"SpaceMMO.Deposit.SurvivesEmptyBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMODepositSurvivesEmptyBoundsTest::RunTest(const FString& Parameters)
{
	// A mesh whose bounds were never built, or which is genuinely empty. Worth handling because the
	// alternative is a division by zero producing an infinite scale, which takes the frame with it
	// rather than drawing a wrong-looking rock.
	TestEqual(
		TEXT("Degenerate bounds scale by one"),
		FDepositPlacement::UniformScale(FVector::ZeroVector),
		1.0,
		0.001);

	return true;
}

#endif
