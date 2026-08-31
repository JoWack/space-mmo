#include "Misc/AutomationTest.h"

#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "SpaceMMOShipPawn.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * A hull that is drawn twelve metres long is one you bump into twelve metres out.
 *
 * <strong>The one thing about a hull that code cannot correct for.</strong> Where a mesh sits
 * relative to its pivot is measurable, so ApplyHullMesh measures it and subtracts it -- this hull
 * arrived 77 m from its own origin and no import setting has to be remembered for that any more.
 * How big a mesh says it is does not matter either, because HullLengthMetres fits it.
 *
 * What neither can fix is the mesh and its collision disagreeing with each other. A re-import that
 * changes one and not the other leaves a ship you can see and cannot touch, or one that stops you
 * from across the road, and both look like collision being broken rather than like two numbers
 * having drifted apart. That happened here twice in one evening, at a hundred to one and then at ten
 * thousand to one, and neither the extent, the primitive count, the trace flag nor the render bounds
 * said anything was wrong on their own.
 *
 * <strong>The ratio is the measurement.</strong> Not the size, which is allowed to be anything.
 */
/*
 * Having collision primitives is not the same as being solid to a query, either. A character sweeps
 * with bTraceComplex false, and the engine reads that as a choice of simple or complex geometry
 * rather than a preference, so a mesh set to use complex collision as simple answers nothing at all
 * -- while carrying hulls, drawing them in the editor, and passing every count anybody takes.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOShipHullCanBeBumpedIntoTest,
	"SpaceMMO.Ship.HullCanBeBumpedInto",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOShipHullCanBeBumpedIntoTest::RunTest(const FString& Parameters)
{
	const ASpaceMMOShipPawn* const Defaults = GetDefault<ASpaceMMOShipPawn>();

	if (Defaults == nullptr || Defaults->GetHullMesh().IsNull())
	{
		AddInfo(TEXT("No hull is configured; nothing to measure."));

		return true;
	}

	UStaticMesh* const Mesh = Cast<UStaticMesh>(Defaults->GetHullMesh().TryLoad());

	if (Mesh == nullptr)
	{
		AddError(TEXT("The configured hull did not load."));

		return false;
	}

	const UBodySetup* const Body = Mesh->GetBodySetup();

	if (Body == nullptr)
	{
		AddError(TEXT("The hull has no body setup at all."));

		return false;
	}

	const int32 Primitives = Body->AggGeom.GetElementCount();

	const ECollisionTraceFlag Flag = Body->CollisionTraceFlag;

	AddInfo(FString::Printf(
		TEXT("'%s': %d simple primitive(s), collision trace flag %d."),
		*Mesh->GetName(), Primitives, static_cast<int32>(Flag)));

	TestTrue(
		TEXT("The hull carries simple collision for a character to sweep against"),
		Primitives > 0);

	// The flag that makes every one of those primitives unreachable.
	TestTrue(
		TEXT("...and is not set to use complex collision as simple, which would hide them all"),
		Flag != ECollisionTraceFlag::CTF_UseComplexAsSimple);

	if (Primitives <= 0)
	{
		return false;
	}

	const FBox Collision = Body->AggGeom.CalcAABB(FTransform::Identity);

	const FVector CollisionExtent = Collision.GetExtent();
	const FVector RenderExtent = Mesh->GetBounds().BoxExtent;

	AddInfo(FString::Printf(
		TEXT("collision extent %s cm against render extent %s cm."),
		*CollisionExtent.ToCompactString(), *RenderExtent.ToCompactString()));

	// Generous, because a convex hull is an approximation and nobody should be editing this test to
	// tune a mesh. Wide enough to pass anything reasonable, narrow enough to catch collision left
	// behind at a hundred times the size of the thing it belongs to.
	const double Largest = FMath::Max3(RenderExtent.X, RenderExtent.Y, RenderExtent.Z);

	const double LargestCollision =
		FMath::Max3(CollisionExtent.X, CollisionExtent.Y, CollisionExtent.Z);

	TestTrue(
		FString::Printf(
			TEXT("The collision is the size of the ship (%.2f cm against %.2f cm)"),
			LargestCollision, Largest),
		LargestCollision > Largest * 0.5 && LargestCollision < Largest * 2.0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
