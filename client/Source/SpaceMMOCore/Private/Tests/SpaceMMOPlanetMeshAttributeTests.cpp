#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Misc/AutomationTest.h"
#include "SpaceMMOPlanetMeshAttributes.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace UE::Geometry;

namespace
{
	/** A single triangle, which is all this needs: the question is what lands on its attributes. */
	void BuildTriangle(FDynamicMesh3& OutMesh)
	{
		OutMesh.EnableAttributes();

		OutMesh.AppendVertex(FVector3d(0.0, 0.0, 0.0));
		OutMesh.AppendVertex(FVector3d(100.0, 0.0, 0.0));
		OutMesh.AppendVertex(FVector3d(0.0, 100.0, 0.0));

		OutMesh.AppendTriangle(0, 1, 2);
	}
}

/**
 * What the terrain knows about itself has to reach the mesh a material reads.
 *
 * <strong>This is the test that was missing.</strong> The builders were covered and correct
 * throughout; the step carrying their output onto the mesh was not, and it wrote height and
 * steepness into a UV channel that materials read as a constant. Every measurement passed at every
 * stage and the ground was one flat colour — a failure invisible to everything except looking at it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOGroundKindsReachTheMeshTest,
	"SpaceMMO.Terrain.GroundKindsReachTheMesh",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOGroundKindsReachTheMeshTest::RunTest(const FString& Parameters)
{
	FDynamicMesh3 Mesh;
	BuildTriangle(Mesh);

	const TArray<FVector2D> SurfaceUVs{
		FVector2D(0.10, 0.20), FVector2D(0.30, 0.40), FVector2D(0.50, 0.60) };

	// Height and steepness, deliberately different per vertex and different from each other, so a
	// channel swap or a vertex mix-up cannot pass by coincidence.
	const TArray<FVector2D> GroundKinds{
		FVector2D(0.11, 0.77), FVector2D(0.22, 0.66), FVector2D(0.33, 0.55) };

	FPlanetMeshAttributes::Write(Mesh, SurfaceUVs, GroundKinds);

	const FDynamicMeshAttributeSet* const Attributes = Mesh.Attributes();

	if (Attributes == nullptr)
	{
		AddError(TEXT("The mesh has no attribute set at all."));

		return false;
	}

	// UV0: where a point is, for tiling a texture.
	const FDynamicMeshUVOverlay* const UVs = Attributes->PrimaryUV();

	if (UVs == nullptr || UVs->ElementCount() != 3)
	{
		AddError(FString::Printf(
			TEXT("Expected three UV elements, got %d."), UVs != nullptr ? UVs->ElementCount() : -1));

		return false;
	}

	// The vertex colour is the channel that actually failed, and the one nothing asserted.
	const FDynamicMeshColorOverlay* const Colors = Attributes->PrimaryColors();

	if (Colors == nullptr || Colors->ElementCount() != 3)
	{
		AddError(FString::Printf(
			TEXT("Expected three colour elements, got %d."),
			Colors != nullptr ? Colors->ElementCount() : -1));

		return false;
	}

	const FIndex3i ColourTriangle = Colors->GetTriangle(0);
	const FIndex3i UVTriangle = UVs->GetTriangle(0);

	TestTrue(
		TEXT("Every corner is assigned a colour element"),
		ColourTriangle.A != FDynamicMesh3::InvalidID
			&& ColourTriangle.B != FDynamicMesh3::InvalidID
			&& ColourTriangle.C != FDynamicMesh3::InvalidID);

	TestTrue(
		TEXT("Every corner is assigned a UV element"),
		UVTriangle.A != FDynamicMesh3::InvalidID
			&& UVTriangle.B != FDynamicMesh3::InvalidID
			&& UVTriangle.C != FDynamicMesh3::InvalidID);

	for (int32 Corner = 0; Corner < 3; ++Corner)
	{
		const int32 VertexId = Mesh.GetTriangle(0)[Corner];
		const int32 ColourElement = ColourTriangle[Corner];
		const int32 UVElement = UVTriangle[Corner];

		const FVector4f Colour = Colors->GetElement(ColourElement);
		const FVector2f UV = UVs->GetElement(UVElement);

		// Red is height and green is steepness. Swapping them is the mistake that looks like a
		// palette problem: rock would band by altitude and colour by slope, both plausibly.
		TestEqual(
			FString::Printf(TEXT("Corner %d carries its height in red"), Corner),
			static_cast<double>(Colour.X),
			GroundKinds[VertexId].X,
			1.0e-5);

		TestEqual(
			FString::Printf(TEXT("Corner %d carries its steepness in green"), Corner),
			static_cast<double>(Colour.Y),
			GroundKinds[VertexId].Y,
			1.0e-5);

		TestEqual(
			FString::Printf(TEXT("Corner %d carries its surface coordinate"), Corner),
			static_cast<double>(UV.X),
			SurfaceUVs[VertexId].X,
			1.0e-5);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOPartialGroundKindsAreRefusedTest,
	"SpaceMMO.Terrain.PartialGroundKindsAreRefused",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOPartialGroundKindsAreRefusedTest::RunTest(const FString& Parameters)
{
	// Fewer values than the mesh has vertices. Writing what arrived would leave the rest at
	// whatever an unwritten element defaults to, and a material blending toward that draws
	// something that looks chosen -- so nothing is written at all.
	FDynamicMesh3 Mesh;
	BuildTriangle(Mesh);

	const TArray<FVector2D> SurfaceUVs{ FVector2D(0.10, 0.20), FVector2D(0.30, 0.40) };
	const TArray<FVector2D> GroundKinds{ FVector2D(0.11, 0.77) };

	FPlanetMeshAttributes::Write(Mesh, SurfaceUVs, GroundKinds);

	const FDynamicMeshAttributeSet* const Attributes = Mesh.Attributes();

	const FDynamicMeshColorOverlay* const Colors =
		Attributes != nullptr ? Attributes->PrimaryColors() : nullptr;

	TestTrue(
		TEXT("A short ground-kind array writes no colours"),
		Colors == nullptr || Colors->ElementCount() == 0);

	return true;
}

#endif
