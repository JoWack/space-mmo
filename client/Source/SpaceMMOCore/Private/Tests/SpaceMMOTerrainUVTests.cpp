#include "Misc/AutomationTest.h"
#include "SpaceMMOPlanetGlobe.h"
#include "SpaceMMOPlanetPatch.h"
#include "SpaceMMOPlanetTerrain.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FPlanetConfig TestPlanet()
	{
		FPlanetConfig Planet;
		Planet.Centre = FSystemCoordinate(60.0, 0.0, 0.0);
		Planet.RadiusKilometres = 20.0;

		return Planet;
	}

	FPlanetTerrainConfig TestTerrain()
	{
		FPlanetTerrainConfig Terrain;
		Terrain.Seed = 20260801;
		Terrain.MaxElevationKilometres = 0.5;

		return Terrain;
	}
}

/**
 * Task 121. The patch's rim is where one mesh hands over to the other, and a texture that jumped
 * there would draw a line around every patch.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOTerrainUVsAgreeAcrossMeshesTest,
	"SpaceMMO.Terrain.UVsAgreeAcrossMeshes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOTerrainUVsAgreeAcrossMeshesTest::RunTest(const FString& Parameters)
{
	// One direction, both meshes. They sample the surface at different resolutions and must still
	// name the same point on the texture, which is why the parameterisation lives on the height
	// function rather than in either builder.
	const FVector Direction = FVector(0.3, -0.7, 0.5).GetSafeNormal();

	const FVector2D FromTerrain = FPlanetTerrain::SurfaceUV(Direction);

	TestTrue(TEXT("Inside the unit square"),
		FromTerrain.X >= 0.0 && FromTerrain.X <= 1.0
		&& FromTerrain.Y >= 0.0 && FromTerrain.Y <= 1.0);

	// The same direction scaled is the same direction, so length must not change the answer -- the
	// patch passes a unit vector and the globe passes one straight out of CubeToSphere.
	//
	// To a tolerance, because normalising a scaled vector differs from normalising the original in
	// the last bits, and an exact comparison here tests floating-point normalisation rather than
	// this function. A texture coordinate that agrees to a millionth agrees.
	const FVector2D Scaled = FPlanetTerrain::SurfaceUV(Direction * 7.0);

	TestTrue(
		TEXT("Scale free"),
		FMath::IsNearlyEqual(Scaled.X, FromTerrain.X, 1.0e-6)
			&& FMath::IsNearlyEqual(Scaled.Y, FromTerrain.Y, 1.0e-6));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOTerrainMeshesCarryTheirUVsTest,
	"SpaceMMO.Terrain.MeshesCarryTheirUVs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOTerrainMeshesCarryTheirUVsTest::RunTest(const FString& Parameters)
{
	// Neither builder wrote UVs at all until this task, and the patch report has been printing
	// "0 UV layers" since it was written. A material cannot be wrong about a coordinate it never
	// receives; it simply draws nothing recognisable.
	FPlanetGlobeConfig GlobeConfig;
	GlobeConfig.Resolution = 16;

	const FPlanetGlobeMesh Globe = FPlanetGlobe::Build(TestPlanet(), TestTerrain(), GlobeConfig);

	TestEqual(TEXT("Globe UV per vertex"), Globe.SurfaceUVs.Num(), Globe.Positions.Num());
	TestEqual(TEXT("Globe ground kind per vertex"), Globe.GroundKinds.Num(), Globe.Positions.Num());

	FPlanetPatchConfig PatchConfig;
	PatchConfig.CentreDirection = FVector(0.0, 0.0, 1.0);
	PatchConfig.AngularRadiusDegrees = 2.0;
	// Odd, so that a vertex lands exactly on the patch's centre direction: U runs -1..1 across
	// Resolution - 1 steps, and an even resolution puts the middle between two vertices, which the
	// comparison below would then read as a disagreement that is really an off-by-half-a-cell.
	PatchConfig.Resolution = 15;

	const FPlanetPatchMesh Patch = FPlanetPatch::Build(TestPlanet(), TestTerrain(), PatchConfig);

	TestEqual(TEXT("Patch UV per vertex"), Patch.SurfaceUVs.Num(), Patch.Positions.Num());

	// The patch's centre must name the same texture point the globe would name there. This is the
	// assertion that catches a builder inventing its own parameterisation, which draws a line of
	// jumped texture right around the patch rim where one mesh hands over to the other.
	const FVector2D Expected = FPlanetTerrain::SurfaceUV(PatchConfig.CentreDirection);

	const int32 Middle = (PatchConfig.Resolution - 1) / 2;

	const int32 CentreVertex = (Middle * PatchConfig.Resolution) + Middle;

	TestTrue(
		TEXT("Patch agrees with the shared parameterisation"),
		FMath::IsNearlyEqual(Patch.SurfaceUVs[CentreVertex].X, Expected.X, 1.0e-3)
			&& FMath::IsNearlyEqual(Patch.SurfaceUVs[CentreVertex].Y, Expected.Y, 1.0e-3));
	TestEqual(TEXT("Patch ground kind per vertex"), Patch.GroundKinds.Num(), Patch.Positions.Num());

	// Height and steepness are fractions. A material bands on them directly, so a value outside the
	// range does not clip -- it silently reads as the top or bottom band everywhere it occurs.
	for (const FVector2D& Kind : Patch.GroundKinds)
	{
		TestTrue(TEXT("Height in range"), Kind.X >= 0.0 && Kind.X <= 1.0);
		TestTrue(TEXT("Steepness in range"), Kind.Y >= 0.0 && Kind.Y <= 1.0);
	}

	// Level ground reads as level. Steepness is measured against the direction from the planet's
	// centre, so getting the sign or the operand wrong makes every flat plain a cliff -- which
	// would band the whole planet as rock and look deliberate.
	double Flattest = 1.0;

	for (const FVector2D& Kind : Patch.GroundKinds)
	{
		Flattest = FMath::Min(Flattest, Kind.Y);
	}

	TestTrue(TEXT("Somewhere is nearly level"), Flattest < 0.1);

	return true;
}

#endif
