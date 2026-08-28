#include "SpaceMMOSolidity.h"

#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "SpaceMMOBackendLog.h"

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

	if (Primitives > 0)
	{
		return;
	}

	UE_LOG(LogSpaceMMOBackend, Warning,
		TEXT("%s '%s' draws '%s', which has no simple collision: a character will walk straight ")
		TEXT("through it. Give it collision in the Static Mesh editor (Collision > Add Simplified ")
		TEXT("Collision, or a UCX_ mesh alongside it in the FBX), or re-import with Generate ")
		TEXT("Missing Collision on."),
		What, *Which, *Mesh->GetName());
}
