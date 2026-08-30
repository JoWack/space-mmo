#include "SpaceMMOSolidity.h"

#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "SpaceMMOLog.h"

void SpaceMMOSolidity::ReportIfIntangible(
	const UStaticMesh* const Mesh, const TCHAR* const What, const FString& Which)
{
	if (Mesh == nullptr)
	{
		return;
	}

	const UBodySetup* const Body = Mesh->GetBodySetup();

	// The count of simple primitives -- boxes, spheres, capsules, convex hulls -- and not whether a
	// body setup exists. Every static mesh has one; an empty one is exactly the failure being
	// looked for, and asserting the presence rather than the value is how a check ends up passing
	// on the broken case.
	const int32 Primitives = Body != nullptr ? Body->AggGeom.GetElementCount() : 0;

	if (Primitives <= 0)
	{
		UE_LOG(LogSpaceMMO, Warning,
			TEXT("%s '%s' draws '%s', which has no simple collision: a character will walk ")
			TEXT("straight through it. Give it collision in the Static Mesh editor (Collision > ")
			TEXT("Add Simplified Collision, or a UCX_ mesh alongside it in the FBX), or re-import ")
			TEXT("with Generate Missing Collision on."),
			What, *Which, *Mesh->GetName());

		return;
	}

	// Having primitives is not the same as anybody being able to hit them.
	//
	// <strong>This flag makes every one of them unreachable to the queries this project runs.</strong>
	// A sweep leaves bTraceComplex false and the engine reads that as a choice of simple geometry,
	// so a mesh told to use its triangles as its simple collision answers nothing at all -- while
	// carrying hulls, drawing them in the editor, and passing every count anybody takes. Counting
	// elements and stopping there was this check's own version of asserting a value exists rather
	// than asserting the value.
	if (Body->CollisionTraceFlag == ECollisionTraceFlag::CTF_UseComplexAsSimple)
	{
		UE_LOG(LogSpaceMMO, Warning,
			TEXT("%s '%s' draws '%s', which has %d collision primitive(s) that nothing can hit: ")
			TEXT("its collision complexity is 'Use Complex Collision As Simple', and a character ")
			TEXT("sweeps against simple collision only. Set it to Default in the Static Mesh ")
			TEXT("editor."),
			What, *Which, *Mesh->GetName(), Primitives);

		return;
	}

	// And collision the wrong size for the mesh it belongs to is collision in the wrong place. A
	// re-import that changes a mesh's scale without rebuilding its hulls leaves exactly this, and it
	// reads in the game as collision being broken rather than as a number being stale.
	const double LargestCollision =
		Body->AggGeom.CalcAABB(FTransform::Identity).GetExtent().GetMax();

	const double LargestRender = Mesh->GetBounds().BoxExtent.GetMax();

	if (LargestRender > UE_DOUBLE_SMALL_NUMBER
		&& (LargestCollision > LargestRender * 2.0 || LargestCollision < LargestRender * 0.5))
	{
		UE_LOG(LogSpaceMMO, Warning,
			TEXT("%s '%s' draws '%s', whose collision is %.1f cm across against a mesh %.1f cm ")
			TEXT("across -- they have drifted apart, most likely a re-import that changed the ")
			TEXT("mesh's scale and not its hulls. Set Build Scale so the two agree."),
			What, *Which, *Mesh->GetName(), LargestCollision, LargestRender);
	}
}
