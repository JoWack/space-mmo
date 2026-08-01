#include "Misc/AutomationTest.h"
#include "SpaceMMOPlanetPatch.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FPlanetConfig PatchTestPlanet()
	{
		FPlanetConfig Planet;
		Planet.Centre = FSystemCoordinate(FVector(200.0, 0.0, 0.0));
		Planet.RadiusKilometres = 20.0;

		return Planet;
	}

	FPlanetTerrainConfig PatchTestTerrain()
	{
		FPlanetTerrainConfig Terrain;
		Terrain.Seed = 20260801;
		Terrain.MaxElevationKilometres = 0.5;

		return Terrain;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOPatchTopologyTest,
	"SpaceMMO.Patch.Topology",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOPatchTopologyTest::RunTest(const FString& Parameters)
{
	FPlanetPatchConfig Config;
	Config.Resolution = 9;

	const FPlanetPatchMesh Mesh =
		FPlanetPatch::Build(PatchTestPlanet(), PatchTestTerrain(), Config);

	TestEqual(TEXT("Vertex count is resolution squared"), Mesh.Positions.Num(), 81);
	TestEqual(TEXT("One normal per vertex"), Mesh.Normals.Num(), 81);
	TestEqual(TEXT("Two triangles per cell"), Mesh.Triangles.Num(), 8 * 8 * 6);

	// Every index must address a real vertex. An off-by-one here is a crash inside the renderer,
	// which is a far worse place to find it than a test.
	int32 OutOfRange = 0;

	for (const int32 Index : Mesh.Triangles)
	{
		if (Index < 0 || Index >= Mesh.Positions.Num())
		{
			++OutOfRange;
		}
	}

	TestEqual(TEXT("No index escapes the vertex array"), OutOfRange, 0);
	TestTrue(TEXT("Mesh reports itself valid"), Mesh.IsValid());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOPatchSitsOnTheTerrainTest,
	"SpaceMMO.Patch.SitsOnTheTerrain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOPatchSitsOnTheTerrainTest::RunTest(const FString& Parameters)
{
	const FPlanetConfig Planet = PatchTestPlanet();
	const FPlanetTerrainConfig Terrain = PatchTestTerrain();

	FPlanetPatchConfig Config;
	Config.Resolution = 17;

	const FPlanetPatchMesh Mesh = FPlanetPatch::Build(Planet, Terrain, Config);

	// The mesh and the server's height function must be the same surface. If they drift, players
	// stand on ground the server thinks is empty air — the exact bug that makes a rendered world
	// and an authoritative one disagree.
	double WorstErrorMetres = 0.0;

	for (const FVector& LocalPosition : Mesh.Positions)
	{
		const FSystemCoordinate World(
			Mesh.Origin.Kilometres
			+ (LocalPosition / SpaceMMO::Coordinates::CentimetresPerKilometre));

		const double Altitude =
			FPlanetTerrain::AltitudeAboveGroundKilometres(Planet, Terrain, World);

		WorstErrorMetres = FMath::Max(WorstErrorMetres, FMath::Abs(Altitude) * 1000.0);
	}

	TestTrue(
		TEXT("Every vertex is on the ground the server reports"),
		WorstErrorMetres < 0.01);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOPatchWorksAtThePolesTest,
	"SpaceMMO.Patch.WorksAtThePoles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOPatchWorksAtThePolesTest::RunTest(const FString& Parameters)
{
	// A latitude-longitude parameterisation dies here, and a tangent frame built by crossing with
	// a fixed axis collapses to zero at exactly these two directions. A player can land anywhere,
	// so anywhere has to work.
	const TArray<FVector> Awkward = {
		FVector(0, 0, 1), FVector(0, 0, -1),
		FVector(1, 0, 0), FVector(-1, 0, 0),
		FVector(0, 1, 0), FVector(0, -1, 0),
	};

	for (const FVector& Direction : Awkward)
	{
		FPlanetPatchConfig Config;
		Config.CentreDirection = Direction;
		Config.Resolution = 5;

		const FPlanetPatchMesh Mesh =
			FPlanetPatch::Build(PatchTestPlanet(), PatchTestTerrain(), Config);

		TestEqual(TEXT("Patch is complete"), Mesh.Positions.Num(), 25);

		int32 Degenerate = 0;

		for (int32 Index = 0; Index < Mesh.Positions.Num(); ++Index)
		{
			if (Mesh.Positions[Index].ContainsNaN() || Mesh.Normals[Index].IsNearlyZero())
			{
				++Degenerate;
			}
		}

		TestEqual(
			*FString::Printf(TEXT("No degenerate vertices at %s"), *Direction.ToCompactString()),
			Degenerate, 0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOPatchNormalsPointOutwardTest,
	"SpaceMMO.Patch.NormalsPointOutward",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOPatchNormalsPointOutwardTest::RunTest(const FString& Parameters)
{
	FPlanetPatchConfig Config;
	Config.CentreDirection = FVector(0.4, 0.6, 0.7);
	Config.Resolution = 13;

	const FPlanetPatchMesh Mesh =
		FPlanetPatch::Build(PatchTestPlanet(), PatchTestTerrain(), Config);

	const FVector Up = Config.CentreDirection.GetSafeNormal();

	// Inward normals mean the ground is lit from underneath and the patch is invisible from above
	// — a wound-backwards mesh looks like a hole in the planet.
	double WorstDot = 1.0;
	double WorstLengthError = 0.0;

	for (const FVector& Normal : Mesh.Normals)
	{
		WorstDot = FMath::Min(WorstDot, FVector::DotProduct(Normal, Up));
		WorstLengthError = FMath::Max(WorstLengthError, FMath::Abs(Normal.Size() - 1.0));
	}

	TestTrue(TEXT("Every normal faces away from the planet"), WorstDot > 0.5);
	TestTrue(TEXT("Every normal is unit length"), WorstLengthError < 1e-6);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOPatchStaysNearItsOriginTest,
	"SpaceMMO.Patch.StaysNearItsOrigin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOPatchStaysNearItsOriginTest::RunTest(const FString& Parameters)
{
	FPlanetPatchConfig Config;
	Config.AngularRadiusDegrees = 10.0;
	Config.Resolution = 33;

	const FPlanetPatchMesh Mesh =
		FPlanetPatch::Build(PatchTestPlanet(), PatchTestTerrain(), Config);

	// The whole reason positions are relative to a patch origin. The planet is 200 km from the
	// system origin, so absolute vertices would be tens of millions of centimetres out — straight
	// into the precision loss ADR-0001 exists to prevent.
	double FurthestCentimetres = 0.0;

	for (const FVector& Position : Mesh.Positions)
	{
		FurthestCentimetres = FMath::Max(FurthestCentimetres, Position.Size());
	}

	// Ten degrees of a 20 km planet is under 4 km across, so nothing should be far from the middle.
	TestTrue(
		TEXT("Vertices stay within a few km of the patch origin"),
		FurthestCentimetres < 500000.0);

	// And the patch really is anchored on the planet, not at the system origin.
	const double OriginToPlanet =
		(Mesh.Origin.Kilometres - PatchTestPlanet().Centre.Kilometres).Size();

	TestTrue(
		TEXT("Origin sits on the planet's surface"),
		FMath::Abs(OriginToPlanet - 20.0) < 1.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOPatchIsDeterministicTest,
	"SpaceMMO.Patch.IsDeterministic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOPatchIsDeterministicTest::RunTest(const FString& Parameters)
{
	FPlanetPatchConfig Config;
	Config.CentreDirection = FVector(0.1, -0.9, 0.4);
	Config.Resolution = 11;

	const FPlanetPatchMesh First =
		FPlanetPatch::Build(PatchTestPlanet(), PatchTestTerrain(), Config);

	const FPlanetPatchMesh Second =
		FPlanetPatch::Build(PatchTestPlanet(), PatchTestTerrain(), Config);

	// Two clients landing at the same place must get the same ground. Nothing here may depend on
	// call order, time, or anything but the arguments.
	bool bIdentical = First.Positions.Num() == Second.Positions.Num();

	for (int32 Index = 0; bIdentical && Index < First.Positions.Num(); ++Index)
	{
		bIdentical = First.Positions[Index].Equals(Second.Positions[Index], 0.0);
	}

	TestTrue(TEXT("Two builds are byte-identical"), bIdentical);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOPatchCentreIsTheRequestedPlaceTest,
	"SpaceMMO.Patch.CentreIsTheRequestedPlace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOPatchCentreIsTheRequestedPlaceTest::RunTest(const FString& Parameters)
{
	const FVector Requested = FVector(0.3, 0.2, -0.9).GetSafeNormal();

	// (0,0) must be the direction asked for, or a landing zone appears somewhere other than where
	// the ship is coming down.
	const FVector Centre = FPlanetPatch::DirectionAt(Requested, 10.0, 0.0, 0.0);

	TestTrue(TEXT("Patch centre is the requested direction"), Centre.Equals(Requested, 1e-9));

	// Corners must be inside the requested angular radius, allowing for the gnomonic stretch that
	// pushes a diagonal further than an edge.
	const double CornerAngle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
		FVector::DotProduct(FPlanetPatch::DirectionAt(Requested, 10.0, 1.0, 1.0), Requested),
		-1.0, 1.0)));

	TestTrue(TEXT("Corner is beyond the edge but bounded"), CornerAngle > 10.0 && CornerAngle < 20.0);

	// A tangent frame is only a frame if it is orthonormal.
	FVector Tangent;
	FVector Bitangent;
	FPlanetPatch::BuildTangentFrame(Requested, Tangent, Bitangent);

	TestTrue(TEXT("Tangent is unit"), FMath::IsNearlyEqual(Tangent.Size(), 1.0, 1e-9));
	TestTrue(TEXT("Bitangent is unit"), FMath::IsNearlyEqual(Bitangent.Size(), 1.0, 1e-9));
	TestTrue(
		TEXT("Frame is orthogonal"),
		FMath::Abs(FVector::DotProduct(Tangent, Bitangent)) < 1e-9
		&& FMath::Abs(FVector::DotProduct(Tangent, Requested)) < 1e-9);

	return true;
}

#endif
