#include "SpaceMMOPhysicsGrid.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Tests for nested physics grids (ADR-0001).
 *
 * The properties here are what let a player walk around inside a moving ship. In a single world
 * space that is genuinely hard — the floor is travelling at a fraction of light speed while the
 * character steps at walking pace. In the ship's own frame the floor is not moving at all, and
 * these tests guard the frame maths that makes that true.
 *
 * See SpaceMMOCoordinatesTests.cpp for how to run them.
 */

namespace
{
	constexpr double GridTolerance = 1e-4;

	/** A ship far out in the system, with a room inside it. */
	struct FShipFixture
	{
		FPhysicsGridRegistry Registry;
		FPhysicsGridRegistry::FGridId Planet = FPhysicsGridRegistry::InvalidGrid;
		FPhysicsGridRegistry::FGridId Ship = FPhysicsGridRegistry::InvalidGrid;
		FPhysicsGridRegistry::FGridId Cabin = FPhysicsGridRegistry::InvalidGrid;

		FShipFixture()
		{
			Planet = Registry.AddRoot(TEXT("Terra"), FSystemCoordinate(1000.0, 2000.0, 0.0));

			// 50 km "above" the planet, in centimetres.
			Ship = Registry.AddChild(TEXT("Shuttle"), Planet, FVector(5000000.0, 0.0, 0.0));

			// A cabin 10 m forward inside the ship.
			Cabin = Registry.AddChild(TEXT("Cabin"), Ship, FVector(1000.0, 0.0, 0.0));
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOGridRootPoseTest,
	"SpaceMMO.Grid.RootPose",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOGridRootPoseTest::RunTest(const FString& Parameters)
{
	FPhysicsGridRegistry Registry;

	const FSystemCoordinate Position(1234.5, -6789.0, 42.0);
	const FPhysicsGridRegistry::FGridId Root = Registry.AddRoot(TEXT("Station"), Position);

	const FPhysicsGridPose Pose = Registry.ResolveWorldPose(Root);

	TestEqual(TEXT("X"), Pose.Origin.Kilometres.X, Position.Kilometres.X, GridTolerance);
	TestEqual(TEXT("Y"), Pose.Origin.Kilometres.Y, Position.Kilometres.Y, GridTolerance);
	TestEqual(TEXT("Z"), Pose.Origin.Kilometres.Z, Position.Kilometres.Z, GridTolerance);
	TestEqual(TEXT("Depth"), Registry.GetDepth(Root), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOGridChainResolvesTest,
	"SpaceMMO.Grid.ChainResolvesToSystemSpace",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOGridChainResolvesTest::RunTest(const FString& Parameters)
{
	const FShipFixture Fixture;

	// Planet at x=1000 km, ship 50 km further out, cabin 10 m beyond that.
	const FPhysicsGridPose CabinPose = Fixture.Registry.ResolveWorldPose(Fixture.Cabin);

	TestEqual(TEXT("X"), CabinPose.Origin.Kilometres.X, 1050.01, GridTolerance);
	TestEqual(TEXT("Y"), CabinPose.Origin.Kilometres.Y, 2000.0, GridTolerance);
	TestEqual(TEXT("Depth"), Fixture.Registry.GetDepth(Fixture.Cabin), 2);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOGridActiveIsIdentityTest,
	"SpaceMMO.Grid.ActiveGridIsIdentity",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOGridActiveIsIdentityTest::RunTest(const FString& Parameters)
{
	const FShipFixture Fixture;

	// The property that keeps Chaos near the origin: whatever the active frame is, it renders at
	// exactly zero rather than at its system-space position.
	for (const FPhysicsGridRegistry::FGridId Grid : { Fixture.Planet, Fixture.Ship, Fixture.Cabin })
	{
		const FTransform Transform = Fixture.Registry.GetRenderTransform(Grid, Grid);

		TestTrue(
			FString::Printf(TEXT("%s renders at origin"), *Fixture.Registry.GetDebugName(Grid).ToString()),
			Transform.GetTranslation().IsNearlyZero(GridTolerance));

		TestTrue(TEXT("No rotation"), Transform.GetRotation().IsIdentity(GridTolerance));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOGridRenderTransformTest,
	"SpaceMMO.Grid.RenderTransformFromActive",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOGridRenderTransformTest::RunTest(const FString& Parameters)
{
	const FShipFixture Fixture;

	// Standing in the cabin, the planet's centre is 50.01 km "behind" — 5,001,000 cm.
	const FTransform PlanetFromCabin =
		Fixture.Registry.GetRenderTransform(Fixture.Planet, Fixture.Cabin);

	TestEqual(TEXT("X"), PlanetFromCabin.GetTranslation().X, -5001000.0, 1.0);

	// And the ship is 10 m behind, which is the scale physics actually has to work at.
	const FTransform ShipFromCabin =
		Fixture.Registry.GetRenderTransform(Fixture.Ship, Fixture.Cabin);

	TestEqual(TEXT("Ship X"), ShipFromCabin.GetTranslation().X, -1000.0, GridTolerance);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOGridMovingParentCarriesChildrenTest,
	"SpaceMMO.Grid.MovingParentCarriesChildren",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOGridMovingParentCarriesChildrenTest::RunTest(const FString& Parameters)
{
	FShipFixture Fixture;

	// The whole reason for nesting: fly the ship and the cabin goes with it, without touching the
	// cabin or anything standing in it.
	const FTransform Before =
		Fixture.Registry.GetRenderTransform(Fixture.Ship, Fixture.Cabin);

	Fixture.Registry.SetLocalOffset(Fixture.Ship, FVector(9000000.0, 0.0, 0.0));

	const FPhysicsGridPose CabinPose = Fixture.Registry.ResolveWorldPose(Fixture.Cabin);
	const FTransform After = Fixture.Registry.GetRenderTransform(Fixture.Ship, Fixture.Cabin);

	// The cabin moved with the ship in system space...
	TestEqual(TEXT("Cabin followed"), CabinPose.Origin.Kilometres.X, 1090.01, GridTolerance);

	// ...while inside the ship nothing changed at all. A character standing here felt nothing.
	TestEqual(
		TEXT("Relative position unchanged"),
		After.GetTranslation().X,
		Before.GetTranslation().X,
		GridTolerance);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOGridRotationCarriesTest,
	"SpaceMMO.Grid.ParentRotationRotatesChildOffset",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOGridRotationCarriesTest::RunTest(const FString& Parameters)
{
	FPhysicsGridRegistry Registry;

	const FPhysicsGridRegistry::FGridId Root =
		Registry.AddRoot(TEXT("Planet"), FSystemCoordinate(0.0, 0.0, 0.0));

	// A ship one kilometre along X, yawed 90 degrees.
	const FQuat Yaw90(FVector::UpVector, FMath::DegreesToRadians(90.0));

	const FPhysicsGridRegistry::FGridId Ship =
		Registry.AddChild(TEXT("Ship"), Root, FVector(100000.0, 0.0, 0.0), Yaw90);

	// A cabin one kilometre "forward" inside the ship. Because the ship is yawed, forward in the
	// ship's frame is +Y in the planet's — if this came out along X, the offset was never rotated
	// and a turning ship would shear its own interior sideways.
	const FPhysicsGridRegistry::FGridId Cabin =
		Registry.AddChild(TEXT("Cabin"), Ship, FVector(100000.0, 0.0, 0.0));

	const FPhysicsGridPose Pose = Registry.ResolveWorldPose(Cabin);

	TestEqual(TEXT("X stays at the ship"), Pose.Origin.Kilometres.X, 1.0, GridTolerance);
	TestEqual(TEXT("Y took the offset"), Pose.Origin.Kilometres.Y, 1.0, GridTolerance);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOGridPrecisionTest,
	"SpaceMMO.Grid.PrecisionFarFromOrigin",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOGridPrecisionTest::RunTest(const FString& Parameters)
{
	FPhysicsGridRegistry Registry;

	// A hundred million kilometres out, where single precision has no resolution whatsoever.
	const FPhysicsGridRegistry::FGridId Root = Registry.AddRoot(
		TEXT("FarStation"), FSystemCoordinate(100000000.0, 100000000.0, 100000000.0));

	const FPhysicsGridRegistry::FGridId Room =
		Registry.AddChild(TEXT("Room"), Root, FVector(250.0, 0.0, 0.0));

	// Standing in the room, the station is 2.5 m away and must resolve to the centimetre.
	const FTransform StationFromRoom = Registry.GetRenderTransform(Root, Room);

	TestEqual(TEXT("Separation survives"), StationFromRoom.GetTranslation().X, -250.0, 0.01);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOGridSimulationRangeTest,
	"SpaceMMO.Grid.SimulationRange",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOGridSimulationRangeTest::RunTest(const FString& Parameters)
{
	FPhysicsGridRegistry Registry;

	const FPhysicsGridRegistry::FGridId Here =
		Registry.AddRoot(TEXT("Here"), FSystemCoordinate(0.0, 0.0, 0.0));

	// The budget is 20 km, so these bracket it.
	const FPhysicsGridRegistry::FGridId Near =
		Registry.AddRoot(TEXT("Near"), FSystemCoordinate(15.0, 0.0, 0.0));

	const FPhysicsGridRegistry::FGridId Far =
		Registry.AddRoot(TEXT("Far"), FSystemCoordinate(500.0, 0.0, 0.0));

	TestTrue(TEXT("Near is simulated"), Registry.IsWithinSimulationRange(Near, Here));
	TestFalse(TEXT("Far is only drawn"), Registry.IsWithinSimulationRange(Far, Here));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOGridAncestryTest,
	"SpaceMMO.Grid.Ancestry",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOGridAncestryTest::RunTest(const FString& Parameters)
{
	const FShipFixture Fixture;

	TestTrue(TEXT("Cabin under ship"), Fixture.Registry.IsDescendantOf(Fixture.Cabin, Fixture.Ship));
	TestTrue(TEXT("Cabin under planet"), Fixture.Registry.IsDescendantOf(Fixture.Cabin, Fixture.Planet));
	TestFalse(TEXT("Ship not under cabin"), Fixture.Registry.IsDescendantOf(Fixture.Ship, Fixture.Cabin));
	TestFalse(TEXT("Nothing is its own ancestor"),
		Fixture.Registry.IsDescendantOf(Fixture.Ship, Fixture.Ship));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOGridInvalidHandlingTest,
	"SpaceMMO.Grid.InvalidGridsAreHarmless",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOGridInvalidHandlingTest::RunTest(const FString& Parameters)
{
	FPhysicsGridRegistry Registry;

	const FPhysicsGridRegistry::FGridId Real =
		Registry.AddRoot(TEXT("Real"), FSystemCoordinate(1.0, 2.0, 3.0));

	constexpr FPhysicsGridRegistry::FGridId Bogus = 999;

	// Grids are looked up every frame from data that may be stale by one tick, so an unknown id
	// has to be inert rather than a crash.
	TestFalse(TEXT("Not valid"), Registry.IsValidGrid(Bogus));
	TestTrue(TEXT("Render transform is identity"),
		Registry.GetRenderTransform(Bogus, Real).Equals(FTransform::Identity));
	TestFalse(TEXT("Not in range"), Registry.IsWithinSimulationRange(Bogus, Real));
	TestEqual(TEXT("No parent"), Registry.GetParent(Bogus), FPhysicsGridRegistry::InvalidGrid);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
