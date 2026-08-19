#include "Misc/AutomationTest.h"
#include "SpaceMMOPlanetGlobe.h"
#include "SpaceMMOPlanetPatch.h"
#include "SpaceMMOPlanetTerrain.h"
#include "SpaceMMOWorldSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

// Named apart from the other terrain tests' helpers on purpose. A unity build can put two of these
// files in one translation unit, where two anonymous namespaces are the same namespace and a second
// copy of a helper is a redefinition rather than a shadow.
namespace
{
	FPlanetConfig UVTestPlanet()
	{
		// The real planet, not a stand-in. These tests measure what a material will actually be
		// handed, and a hand-built config would keep passing after somebody changed the world.
		return USpaceMMOWorldSubsystem::StartingPlanet();
	}

	FPlanetTerrainConfig UVTestTerrain()
	{
		return USpaceMMOWorldSubsystem::StartingPlanetTerrain();
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

	// The production globe, for the report below. 96 per face is what the planet actually builds.
	FPlanetGlobeConfig RealGlobeConfig;

	const FPlanetGlobeMesh Globe = FPlanetGlobe::Build(UVTestPlanet(), UVTestTerrain(), GlobeConfig);

	TestEqual(TEXT("Globe UV per vertex"), Globe.SurfaceUVs.Num(), Globe.Positions.Num());
	TestEqual(TEXT("Globe ground kind per vertex"), Globe.GroundKinds.Num(), Globe.Positions.Num());

	FPlanetPatchConfig PatchConfig;
	PatchConfig.CentreDirection = FVector(0.0, 0.0, 1.0);
	PatchConfig.AngularRadiusDegrees = 2.0;
	// Odd, so that a vertex lands exactly on the patch's centre direction: U runs -1..1 across
	// Resolution - 1 steps, and an even resolution puts the middle between two vertices, which the
	// comparison below would then read as a disagreement that is really an off-by-half-a-cell.
	PatchConfig.Resolution = 15;

	const FPlanetPatchMesh Patch = FPlanetPatch::Build(UVTestPlanet(), UVTestTerrain(), PatchConfig);

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

	// Reported, not asserted. Both channels are 0..1 by construction, which says nothing about the
	// range they actually occupy on this planet -- and a material blending on a channel that only
	// ever spans a hundredth draws a flat colour however it is authored. Printing it means the next
	// person reads the range instead of inferring it from a screenshot.
	double LowHeight = 1.0;
	double HighHeight = 0.0;
	double Gentlest = 1.0;
	double Steepest = 0.0;

	for (const FVector2D& Kind : Patch.GroundKinds)
	{
		LowHeight = FMath::Min(LowHeight, Kind.X);
		HighHeight = FMath::Max(HighHeight, Kind.X);
		Gentlest = FMath::Min(Gentlest, Kind.Y);
		Steepest = FMath::Max(Steepest, Kind.Y);
	}

	AddInfo(FString::Printf(
		TEXT("Coarse patch (res %d): height %.4f..%.4f, steepness %.4f..%.4f"),
		PatchConfig.Resolution, LowHeight, HighHeight, Gentlest, Steepest));

	// And again at what the game actually builds. Sampling finer catches slopes the coarse mesh
	// steps straight over, so a range measured at test resolution says nothing about what a player
	// is standing on -- which is the whole question here.
	FPlanetPatchConfig Real;
	Real.CentreDirection = PatchConfig.CentreDirection;
	Real.AngularRadiusDegrees = 2.0;
	Real.Resolution = 129;

	const FPlanetPatchMesh RealPatch = FPlanetPatch::Build(UVTestPlanet(), UVTestTerrain(), Real);

	LowHeight = 1.0;
	HighHeight = 0.0;
	Gentlest = 1.0;
	Steepest = 0.0;

	for (const FVector2D& Kind : RealPatch.GroundKinds)
	{
		LowHeight = FMath::Min(LowHeight, Kind.X);
		HighHeight = FMath::Max(HighHeight, Kind.X);
		Gentlest = FMath::Min(Gentlest, Kind.Y);
		Steepest = FMath::Max(Steepest, Kind.Y);
	}

	AddInfo(FString::Printf(
		TEXT("Real patch (res 129): height %.4f..%.4f, steepness %.4f..%.4f, steepest angle %.1f deg"),
		LowHeight, HighHeight, Gentlest, Steepest,
		FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Steepest, 0.0, 1.0)))));

	const FPlanetGlobeMesh WholePlanet =
		FPlanetGlobe::Build(UVTestPlanet(), UVTestTerrain(), RealGlobeConfig);

	LowHeight = 1.0;
	HighHeight = 0.0;
	Gentlest = 1.0;
	Steepest = 0.0;

	for (const FVector2D& Kind : WholePlanet.GroundKinds)
	{
		LowHeight = FMath::Min(LowHeight, Kind.X);
		HighHeight = FMath::Max(HighHeight, Kind.X);
		Gentlest = FMath::Min(Gentlest, Kind.Y);
		Steepest = FMath::Max(Steepest, Kind.Y);
	}

	AddInfo(FString::Printf(
		TEXT("Real globe (res %d): height %.4f..%.4f, steepness %.4f..%.4f, steepest angle %.1f deg"),
		RealGlobeConfig.Resolution, LowHeight, HighHeight, Gentlest, Steepest,
		FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Steepest, 0.0, 1.0)))));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOTerrainHasSlopesToShadeTest,
	"SpaceMMO.Terrain.HasSlopesToShade",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOTerrainHasSlopesToShadeTest::RunTest(const FString& Parameters)
{
	// The planet had 0.5 km of relief spread over two features per radius, which made broad swells
	// and a steepest slope of 5.9 degrees anywhere on it. A material blending rock onto cliffs had
	// no cliffs, and one banding on height saw 0.31..0.37 across everything visible from the
	// ground -- both drew a flat colour, and neither was wrong.
	//
	// Nothing about that was visible from the terrain configuration, and it took measuring the
	// built mesh to find. This is that measurement, kept.
	FPlanetPatchConfig Patch;
	Patch.CentreDirection = FVector(0.0, 0.0, 1.0);
	Patch.AngularRadiusDegrees = 2.0;
	Patch.Resolution = 129;

	const FPlanetPatchMesh Mesh = FPlanetPatch::Build(
		USpaceMMOWorldSubsystem::StartingPlanet(),
		USpaceMMOWorldSubsystem::StartingPlanetTerrain(),
		Patch);

	double Steepest = 0.0;
	double LowHeight = 1.0;
	double HighHeight = 0.0;

	for (const FVector2D& Kind : Mesh.GroundKinds)
	{
		Steepest = FMath::Max(Steepest, Kind.Y);
		LowHeight = FMath::Min(LowHeight, Kind.X);
		HighHeight = FMath::Max(HighHeight, Kind.X);
	}

	const double SteepestDegrees =
		FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Steepest, 0.0, 1.0)));

	// The channel value as well as the angle it means. A 32 degree slope was arriving as 0.15 under
	// the old encoding, which is a correct number nobody can see -- and the reason this assertion
	// checks both is that the angle alone would have passed throughout.
	AddInfo(FString::Printf(TEXT("Steepest channel value %.3f"), Steepest));

	AddInfo(FString::Printf(
		TEXT("Steepest %.1f deg, height %.3f..%.3f across one patch"),
		SteepestDegrees, LowHeight, HighHeight));

	// Twenty degrees is a hillside. Below that a slope blend has nothing to say, and the planet is
	// back to swells -- which is a look somebody may choose, but not one to arrive at by accident.
	TestTrue(
		FString::Printf(TEXT("Steep enough to shade (%.1f deg)"), SteepestDegrees),
		SteepestDegrees > 20.0);

	// And enough height variation inside a single patch to band on. Planet-wide range is not the
	// question: a player on foot sees 283 m of horizon and never leaves one patch.
	TestTrue(
		FString::Printf(TEXT("Height varies locally (%.3f)"), HighHeight - LowHeight),
		(HighHeight - LowHeight) > 0.2);

	return true;
}

#endif
