#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Misc/AutomationTest.h"
#include "SpaceMMOPlanetGlobe.h"
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

namespace
{
	/**
	 * Assembles a mesh exactly as the planet actor does, and reports what survived.
	 *
	 * The arrays are clean and the component says it holds the triangles, so if anything is lost
	 * it is lost here — between handing vertices to FDynamicMesh3 and asking it to draw them.
	 * AppendTriangle refuses non-manifold work and returns an error instead of adding, which is
	 * silent unless somebody counts.
	 */
	struct FAssembled
	{
		int32 Vertices = 0;
		int32 Triangles = 0;
		int32 Rejected = 0;
		int32 NormalElements = 0;
		bool bMeshValid = false;
		bool bAttributesValid = false;
	};

	FAssembled Assemble(
		const TArray<FVector>& Positions,
		const TArray<FVector>& Normals,
		const TArray<int32>& Triangles)
	{
		using namespace UE::Geometry;

		FDynamicMesh3 Mesh;
		Mesh.EnableAttributes();

		for (const FVector& Position : Positions)
		{
			Mesh.AppendVertex(FVector3d(Position));
		}

		FAssembled Result;

		for (int32 Index = 0; Index + 2 < Triangles.Num(); Index += 3)
		{
			const int32 Added = Mesh.AppendTriangle(
				Triangles[Index], Triangles[Index + 1], Triangles[Index + 2]);

			if (Added < 0)
			{
				++Result.Rejected;
			}
		}

		if (FDynamicMeshNormalOverlay* Overlay =
			Mesh.Attributes() != nullptr ? Mesh.Attributes()->PrimaryNormals() : nullptr)
		{
			Overlay->ClearElements();

			TArray<int32> Elements;
			Elements.Reserve(Normals.Num());

			for (const FVector& Normal : Normals)
			{
				Elements.Add(Overlay->AppendElement(FVector3f(Normal)));
			}

			for (const int32 TriangleId : Mesh.TriangleIndicesItr())
			{
				const FIndex3i Triangle = Mesh.GetTriangle(TriangleId);

				Overlay->SetTriangle(
					TriangleId,
					FIndex3i(Elements[Triangle.A], Elements[Triangle.B], Elements[Triangle.C]));
			}

			Result.NormalElements = Overlay->ElementCount();
		}

		Result.Vertices = Mesh.VertexCount();
		Result.Triangles = Mesh.TriangleCount();

		Result.bMeshValid = Mesh.CheckValidity(
			FDynamicMesh3::FValidityOptions(), EValidityCheckFailMode::ReturnOnly);

		// The attribute set's own CheckValidity is protected, so the overlay is checked instead:
		// every triangle must carry three real normal elements. A triangle whose normals were
		// never set holds -1s, which is exactly what an overlay looks like when SetTriangle was
		// skipped for it.
		Result.bAttributesValid = true;

		if (const FDynamicMeshNormalOverlay* Overlay =
			Mesh.Attributes() != nullptr ? Mesh.Attributes()->PrimaryNormals() : nullptr)
		{
			for (const int32 TriangleId : Mesh.TriangleIndicesItr())
			{
				const FIndex3i Elements = Overlay->GetTriangle(TriangleId);

				if (Elements.A < 0 || Elements.B < 0 || Elements.C < 0)
				{
					Result.bAttributesValid = false;

					break;
				}
			}
		}

		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOPatchAssemblesLikeTheGlobeTest,
	"SpaceMMO.Patch.AssemblesLikeTheGlobe",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOPatchAssemblesLikeTheGlobeTest::RunTest(const FString& Parameters)
{
	const FPlanetConfig Planet = PatchTestPlanet();
	const FPlanetTerrainConfig Terrain = PatchTestTerrain();

	FPlanetPatchConfig PatchConfig;
	PatchConfig.Resolution = 129;
	PatchConfig.AngularRadiusDegrees = 4.0;

	const FPlanetPatchMesh Patch = FPlanetPatch::Build(Planet, Terrain, PatchConfig);

	FPlanetGlobeConfig GlobeConfig;
	GlobeConfig.Resolution = 24;

	const FPlanetGlobeMesh Globe = FPlanetGlobe::Build(Planet, Terrain, GlobeConfig);

	const FAssembled PatchMesh =
		Assemble(Patch.Positions, Patch.Normals, Patch.Triangles);

	const FAssembled GlobeMesh =
		Assemble(Globe.Positions, Globe.Normals, Globe.Triangles);

	auto Report = [this](const TCHAR* Name, const FAssembled& A, const int32 Expected)
	{
		AddInfo(FString::Printf(
			TEXT("%s: %d verts, %d/%d tris (%d rejected), %d normal elements, "
				"mesh valid %d, attributes valid %d"),
			Name,
			A.Vertices,
			A.Triangles,
			Expected,
			A.Rejected,
			A.NormalElements,
			A.bMeshValid ? 1 : 0,
			A.bAttributesValid ? 1 : 0));
	};

	Report(TEXT("patch"), PatchMesh, Patch.Triangles.Num() / 3);
	Report(TEXT("globe"), GlobeMesh, Globe.Triangles.Num() / 3);

	TestEqual(TEXT("Patch loses no triangle to AppendTriangle"), PatchMesh.Rejected, 0);
	TestEqual(TEXT("Globe loses no triangle to AppendTriangle"), GlobeMesh.Rejected, 0);

	TestTrue(TEXT("Patch mesh is structurally valid"), PatchMesh.bMeshValid);
	TestTrue(TEXT("Globe mesh is structurally valid"), GlobeMesh.bMeshValid);

	TestTrue(TEXT("Patch attributes are valid"), PatchMesh.bAttributesValid);
	TestTrue(TEXT("Globe attributes are valid"), GlobeMesh.bAttributesValid);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOPatchAgainstTheGlobeTest,
	"SpaceMMO.Patch.AgainstTheGlobe",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOPatchAgainstTheGlobeTest::RunTest(const FString& Parameters)
{
	const FPlanetConfig Planet = PatchTestPlanet();
	const FPlanetTerrainConfig Terrain = PatchTestTerrain();

	// One of these draws and one does not, from the same height function through the same
	// component. Everything checked so far has come back identical, so this prints the two side by
	// side and lets the difference show itself rather than being guessed at.
	FPlanetPatchConfig PatchConfig;
	PatchConfig.Resolution = 129;
	PatchConfig.AngularRadiusDegrees = 4.0;

	const FPlanetPatchMesh Patch = FPlanetPatch::Build(Planet, Terrain, PatchConfig);

	FPlanetGlobeConfig GlobeConfig;
	GlobeConfig.Resolution = 24;

	const FPlanetGlobeMesh Globe = FPlanetGlobe::Build(Planet, Terrain, GlobeConfig);

	auto Describe = [this](
		const TCHAR* Name,
		const TArray<FVector>& Positions,
		const TArray<FVector>& Normals,
		const TArray<int32>& Triangles)
	{
		FBox Box(ForceInit);
		double Longest = 0.0;
		double Shortest = TNumericLimits<double>::Max();

		for (const FVector& Position : Positions)
		{
			Box += Position;
			Longest = FMath::Max(Longest, Position.Size());
			Shortest = FMath::Min(Shortest, Position.Size());
		}

		double ShortestEdge = TNumericLimits<double>::Max();

		for (int32 Index = 0; Index + 2 < Triangles.Num(); Index += 3)
		{
			ShortestEdge = FMath::Min(
				ShortestEdge,
				FVector::Dist(Positions[Triangles[Index]], Positions[Triangles[Index + 1]]));
		}

		AddInfo(FString::Printf(
			TEXT("%s: %d verts, %d normals, %d tris, extent %s, |v| %.1f..%.1f cm, "
				"shortest edge %.3f cm"),
			Name,
			Positions.Num(),
			Normals.Num(),
			Triangles.Num() / 3,
			*Box.GetExtent().ToCompactString(),
			Shortest,
			Longest,
			ShortestEdge));

		return ShortestEdge;
	};

	const double PatchEdge =
		Describe(TEXT("patch"), Patch.Positions, Patch.Normals, Patch.Triangles);

	const double GlobeEdge =
		Describe(TEXT("globe"), Globe.Positions, Globe.Normals, Globe.Triangles);

	TestTrue(TEXT("Patch has a normal per vertex"), Patch.Normals.Num() == Patch.Positions.Num());
	TestTrue(TEXT("Globe has a normal per vertex"), Globe.Normals.Num() == Globe.Positions.Num());

	// An edge measured in thousandths of a centimetre across a mesh kilometres wide is the kind of
	// ratio that collapses in single precision once the renderer converts, whatever it looked like
	// in double.
	TestTrue(
		FString::Printf(TEXT("Patch's shortest edge is %.4f cm"), PatchEdge),
		PatchEdge > 0.01);

	TestTrue(
		FString::Printf(TEXT("Globe's shortest edge is %.4f cm"), GlobeEdge),
		GlobeEdge > 0.01);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOPatchMeshIsFitToRenderTest,
	"SpaceMMO.Patch.MeshIsFitToRender",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOPatchMeshIsFitToRenderTest::RunTest(const FString& Parameters)
{
	const FPlanetConfig Planet = PatchTestPlanet();
	const FPlanetTerrainConfig Terrain = PatchTestTerrain();

	// The patch's vertices do not draw even inside the component that draws the globe, so the fault
	// is in these numbers. This looks for what stops a renderer dead rather than what looks wrong:
	// a non-finite coordinate poisons the bounds, and a zero-length normal is not a direction.
	for (const double Degrees : { 4.0, 60.0 })
	{
		FPlanetPatchConfig Config;
		Config.Resolution = 129;
		Config.AngularRadiusDegrees = Degrees;

		const FPlanetPatchMesh Mesh = FPlanetPatch::Build(Planet, Terrain, Config);

		int32 BadPositions = 0;
		int32 BadNormals = 0;
		int32 ShortNormals = 0;

		for (const FVector& Position : Mesh.Positions)
		{
			if (Position.ContainsNaN())
			{
				++BadPositions;
			}
		}

		for (const FVector& Normal : Mesh.Normals)
		{
			if (Normal.ContainsNaN())
			{
				++BadNormals;
			}
			else if (!FMath::IsNearlyEqual(Normal.Size(), 1.0, 0.01))
			{
				++ShortNormals;
			}
		}

		TestEqual(
			FString::Printf(TEXT("Non-finite positions at %.0f degrees"), Degrees),
			BadPositions, 0);

		TestEqual(
			FString::Printf(TEXT("Non-finite normals at %.0f degrees"), Degrees),
			BadNormals, 0);

		TestEqual(
			FString::Printf(TEXT("Normals that are not unit length at %.0f degrees"), Degrees),
			ShortNormals, 0);

		// A triangle with no area has no normal and no pixels, and enough of them can make a mesh
		// the renderer declines to build buffers for.
		int32 Degenerate = 0;

		for (int32 Index = 0; Index + 2 < Mesh.Triangles.Num(); Index += 3)
		{
			const FVector A = Mesh.Positions[Mesh.Triangles[Index]];
			const FVector B = Mesh.Positions[Mesh.Triangles[Index + 1]];
			const FVector C = Mesh.Positions[Mesh.Triangles[Index + 2]];

			if (FVector::CrossProduct(B - A, C - A).IsNearlyZero())
			{
				++Degenerate;
			}
		}

		TestEqual(
			FString::Printf(TEXT("Degenerate triangles at %.0f degrees"), Degrees),
			Degenerate, 0);

		// Duplicated vertices are what makes a grid non-manifold, and a non-manifold triangle is
		// one FDynamicMesh3 refuses to append — which would leave the component holding fewer
		// triangles than were handed to it.
		int32 Duplicates = 0;

		for (int32 Index = 1; Index < Mesh.Positions.Num(); ++Index)
		{
			if (Mesh.Positions[Index].Equals(Mesh.Positions[Index - 1], 0.0001))
			{
				++Duplicates;
			}
		}

		TestEqual(
			FString::Printf(TEXT("Coincident neighbouring vertices at %.0f degrees"), Degrees),
			Duplicates, 0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOPatchNormalsMatchTheGroundTest,
	"SpaceMMO.Patch.NormalsMatchTheGround",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOPatchNormalsMatchTheGroundTest::RunTest(const FString& Parameters)
{
	const FPlanetConfig Planet = PatchTestPlanet();
	const FPlanetTerrainConfig Terrain = PatchTestTerrain();

	constexpr int32 Resolution = 33;

	FPlanetPatchConfig Config;
	Config.Resolution = Resolution;
	Config.AngularRadiusDegrees = 4.0;

	const FPlanetPatchMesh Mesh = FPlanetPatch::Build(Planet, Terrain, Config);
	const FVector Centre = Config.CentreDirection.GetSafeNormal();

	// Outward-facing is a weak claim: a normal ninety degrees off still satisfies it, and a surface
	// lit by a normal ninety degrees off is black while everything standing on it is lit. So this
	// asks the stronger question — does the drawn normal agree with the one the height function
	// gives at the same place? Terrain here is gentle, a few degrees of slope, so any large
	// disagreement is the mesh being wrong rather than the ground being steep.
	double WorstDegrees = 0.0;
	int32 WorstIndex = INDEX_NONE;

	for (int32 Row = 1; Row < Resolution - 1; ++Row)
	{
		for (int32 Column = 1; Column < Resolution - 1; ++Column)
		{
			const int32 Index = (Row * Resolution) + Column;

			const double U = -1.0 + ((2.0 * Column) / (Resolution - 1));
			const double V = -1.0 + ((2.0 * Row) / (Resolution - 1));

			const FVector Direction =
				FPlanetPatch::DirectionAt(Centre, Config.AngularRadiusDegrees, U, V);

			const FVector Expected = FPlanetTerrain::SurfaceNormal(Planet, Terrain, Direction);

			const double Degrees = FMath::RadiansToDegrees(FMath::Acos(
				FMath::Clamp(FVector::DotProduct(Expected, Mesh.Normals[Index]), -1.0, 1.0)));

			if (Degrees > WorstDegrees)
			{
				WorstDegrees = Degrees;
				WorstIndex = Index;
			}
		}
	}

	TestTrue(
		FString::Printf(
			TEXT("Worst normal (vertex %d) is %.1f degrees from the ground it sits on"),
			WorstIndex,
			WorstDegrees),
		WorstDegrees < 20.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOPatchSurvivesBeingWideTest,
	"SpaceMMO.Patch.SurvivesBeingWide",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOPatchSurvivesBeingWideTest::RunTest(const FString& Parameters)
{
	const FPlanetConfig Planet = PatchTestPlanet();
	const FPlanetTerrainConfig Terrain = PatchTestTerrain();

	// Everything about this patch was written and checked at four degrees, and then a viewer high
	// in the atmosphere started asking for sixty. The gnomonic projection stretches hard out there
	// — the corners of a sixty-degree patch land at sixty-eight — so whether it still produces a
	// surface facing the right way is a question, not an assumption.
	for (const double Degrees : { 4.0, 20.0, 45.0, 60.0 })
	{
		FPlanetPatchConfig Config;
		Config.Resolution = 25;
		Config.AngularRadiusDegrees = Degrees;

		const FPlanetPatchMesh Mesh = FPlanetPatch::Build(Planet, Terrain, Config);

		if (!Mesh.IsValid())
		{
			AddError(FString::Printf(TEXT("A %.0f degree patch built nothing."), Degrees));

			return false;
		}

		// Outward is away from the planet's centre. The patch's vertices are relative to the ground
		// beneath its own middle, so the centre is that anchor's offset from the planet.
		const FVector CentreOffset =
			(Mesh.Origin.Kilometres - Planet.Centre.Kilometres)
			* SpaceMMO::Coordinates::CentimetresPerKilometre;

		int32 Inward = 0;

		for (int32 Index = 0; Index + 2 < Mesh.Triangles.Num(); Index += 3)
		{
			const FVector A = Mesh.Positions[Mesh.Triangles[Index]];
			const FVector B = Mesh.Positions[Mesh.Triangles[Index + 1]];
			const FVector C = Mesh.Positions[Mesh.Triangles[Index + 2]];

			const FVector Outward = CentreOffset + ((A + B + C) / 3.0);

			if (FVector::DotProduct(FVector::CrossProduct(B - A, C - A), Outward) <= 0.0)
			{
				++Inward;
			}
		}

		if (Inward > 0)
		{
			AddError(FString::Printf(
				TEXT("%d of %d triangles face inward at %.0f degrees."),
				Inward,
				Mesh.Triangles.Num() / 3,
				Degrees));

			return false;
		}

		// Vertex normals are what the lighting uses, and an inward one is a patch of ground lit
		// from underneath — which is how the patch's winding bug showed up the first time.
		for (int32 Index = 0; Index < Mesh.Normals.Num(); ++Index)
		{
			const FVector Outward = CentreOffset + Mesh.Positions[Index];

			if (FVector::DotProduct(Mesh.Normals[Index], Outward.GetSafeNormal()) <= 0.0)
			{
				AddError(FString::Printf(
					TEXT("Vertex %d has an inward normal at %.0f degrees."), Index, Degrees));

				return false;
			}
		}
	}

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOPatchRebuildThresholdTest,
	"SpaceMMO.Patch.RebuildThreshold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOPatchRebuildThresholdTest::RunTest(const FString& Parameters)
{
	const FVector Centre = FVector(1, 0, 0);
	const double Radius = 10.0;

	// No patch yet is always a reason to build one.
	TestTrue(
		TEXT("No patch means build"),
		FPlanetPatch::ShouldRebuild(FVector::ZeroVector, Centre, Radius));

	// Standing still must not rebuild, or the mesh is regenerated every single frame.
	TestFalse(
		TEXT("Standing still does not rebuild"),
		FPlanetPatch::ShouldRebuild(Centre, Centre, Radius));

	// A small drift is still comfortably inside the patch.
	const FVector Nearby = FVector(FMath::Cos(FMath::DegreesToRadians(1.0)),
		FMath::Sin(FMath::DegreesToRadians(1.0)), 0.0);

	TestFalse(TEXT("A degree of drift is fine"), FPlanetPatch::ShouldRebuild(Centre, Nearby, Radius));

	// Past the threshold — 40% of a 10 degree radius — the ground ahead is running out.
	const FVector Far = FVector(FMath::Cos(FMath::DegreesToRadians(6.0)),
		FMath::Sin(FMath::DegreesToRadians(6.0)), 0.0);

	TestTrue(TEXT("Six degrees of drift rebuilds"), FPlanetPatch::ShouldRebuild(Centre, Far, Radius));

	// The rebuild has to happen while there is still ground ahead, not once the player has walked
	// off the edge, so the threshold must be inside the patch rather than at its rim.
	const FVector AtRim = FVector(FMath::Cos(FMath::DegreesToRadians(Radius)),
		FMath::Sin(FMath::DegreesToRadians(Radius)), 0.0);

	TestTrue(TEXT("Reaching the rim certainly rebuilds"), FPlanetPatch::ShouldRebuild(Centre, AtRim, Radius));

	// Identical directions produce a dot product that floating point often nudges just past 1,
	// and acos of that is NaN — which compares false against any threshold and would silently
	// stop rebuilding forever.
	const FVector Same = Centre.GetSafeNormal();

	TestFalse(
		TEXT("Exactly parallel does not produce NaN"),
		FPlanetPatch::ShouldRebuild(Same, Same, Radius));

	return true;
}

#endif
