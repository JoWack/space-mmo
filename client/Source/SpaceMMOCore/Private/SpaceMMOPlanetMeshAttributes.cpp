#include "SpaceMMOPlanetMeshAttributes.h"

#include "DynamicMesh/DynamicMeshAttributeSet.h"

using namespace UE::Geometry;

void FPlanetMeshAttributes::Write(
	FDynamicMesh3& Mesh,
	const TArray<FVector2D>& SurfaceUVs,
	const TArray<FVector2D>& GroundKinds)
{
	FDynamicMeshAttributeSet* const Attributes = Mesh.Attributes();

	if (Attributes == nullptr || SurfaceUVs.Num() == 0)
	{
		return;
	}

	// UV0 carries the surface parameterisation, for tiling a texture.
	if (FDynamicMeshUVOverlay* const UVs = Attributes->PrimaryUV();
		UVs != nullptr && SurfaceUVs.Num() >= Mesh.MaxVertexID())
	{
		UVs->ClearElements();

		TArray<int32> Elements;
		Elements.Reserve(SurfaceUVs.Num());

		for (const FVector2D& Value : SurfaceUVs)
		{
			Elements.Add(UVs->AppendElement(FVector2f(Value)));
		}

		for (const int32 TriangleId : Mesh.TriangleIndicesItr())
		{
			const FIndex3i Triangle = Mesh.GetTriangle(TriangleId);

			UVs->SetTriangle(
				TriangleId,
				FIndex3i(Elements[Triangle.A], Elements[Triangle.B], Elements[Triangle.C]));
		}
	}

	// Height and steepness go in the vertex colour, not a second UV channel.
	//
	// They were in UV1 first, and the mesh carried them correctly -- 32768 triangles across two
	// layers, and the scene proxy forwards every layer it finds. A material reading TexCoord[1]
	// still got a constant, and rather than keep chasing where an index stops matching, this is
	// the channel the engine and every terrain material already agree on: VertexColor has no
	// index to get wrong.
	if (GroundKinds.Num() < Mesh.MaxVertexID())
	{
		return;
	}

	Attributes->EnablePrimaryColors();

	if (FDynamicMeshColorOverlay* const Colors = Attributes->PrimaryColors())
	{
		Colors->ClearElements();

		TArray<int32> Elements;
		Elements.Reserve(GroundKinds.Num());

		for (const FVector2D& Kind : GroundKinds)
		{
			// Red is height, green is steepness, and blue is left free for whatever the third
			// thing turns out to be -- moisture, or a biome mask, when a planet needs one.
			Elements.Add(Colors->AppendElement(
				FVector4f(
					static_cast<float>(Kind.X),
					static_cast<float>(Kind.Y),
					0.0f,
					1.0f)));
		}

		for (const int32 TriangleId : Mesh.TriangleIndicesItr())
		{
			const FIndex3i Triangle = Mesh.GetTriangle(TriangleId);

			Colors->SetTriangle(
				TriangleId,
				FIndex3i(Elements[Triangle.A], Elements[Triangle.B], Elements[Triangle.C]));
		}
	}
}
