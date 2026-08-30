#include "Misc/AutomationTest.h"

#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "SpaceMMOShipPawn.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The hull has to be drawn where the ship is.
 *
 * <strong>Measured off the imported mesh, because nothing upstream of it can tell you.</strong> The
 * camera boom hangs off the pawn's origin, so a hull whose geometry sits a long way from that origin
 * is a ship that is simply not in frame -- which looks like a mesh failing to draw, or a camera
 * pointing the wrong way, or a boom that is too short. Three plausible causes and one number that
 * separates them.
 *
 * The number is the bounds origin: where the middle of the mesh is, relative to the pivot everything
 * else hangs off. An exporter that leaves an object away from its scene origin, and an import that
 * bakes that transform into the vertices, together produce a mesh that is correct in every respect
 * except where it is -- and the extent, the triangle count, the materials and the collision all look
 * exactly right while it happens.
 *
 * The assertion is deliberately about the pawn rather than about a tolerance somebody picked: the
 * ship's origin must lie inside its own hull.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOShipHullIsDrawnWhereTheShipIsTest,
	"SpaceMMO.Ship.HullIsDrawnWhereTheShipIs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOShipHullIsDrawnWhereTheShipIsTest::RunTest(const FString& Parameters)
{
	const ASpaceMMOShipPawn* const Defaults = GetDefault<ASpaceMMOShipPawn>();

	if (Defaults == nullptr)
	{
		AddError(TEXT("No ship defaults to read a hull from."));

		return false;
	}

	// An unset hull is a working state -- the placeholder cone -- but it must be said out loud, or
	// this test passes forever on a project that stopped configuring a ship at all.
	if (Defaults->GetHullMesh().IsNull())
	{
		AddInfo(TEXT("No hull is configured; the placeholder cone stands and there is nothing to "
			"measure. Set HullMesh in DefaultGame.ini to make this test do anything."));

		return true;
	}

	UStaticMesh* const Mesh = Cast<UStaticMesh>(Defaults->GetHullMesh().TryLoad());

	if (Mesh == nullptr)
	{
		AddError(FString::Printf(
			TEXT("Hull '%s' is configured but did not load."), *Defaults->GetHullMesh().ToString()));

		return false;
	}

	const FBoxSphereBounds Bounds = Mesh->GetBounds();

	AddInfo(FString::Printf(
		TEXT("'%s': extent %s cm, bounds origin %s cm."),
		*Mesh->GetName(), *Bounds.BoxExtent.ToCompactString(), *Bounds.Origin.ToCompactString()));

	// Inside its own bounding box, on every axis. A hull offset by less than its own half-width is
	// a pivot somebody chose; one offset by more than that is a transform that got baked in.
	TestTrue(
		FString::Printf(
			TEXT("The ship's origin is inside its own hull along X (origin %.1f, half-width %.1f)"),
			Bounds.Origin.X, Bounds.BoxExtent.X),
		FMath::Abs(Bounds.Origin.X) <= Bounds.BoxExtent.X);

	TestTrue(
		FString::Printf(
			TEXT("...and along Y (origin %.1f, half-width %.1f)"),
			Bounds.Origin.Y, Bounds.BoxExtent.Y),
		FMath::Abs(Bounds.Origin.Y) <= Bounds.BoxExtent.Y);

	TestTrue(
		FString::Printf(
			TEXT("...and along Z (origin %.1f, half-height %.1f)"),
			Bounds.Origin.Z, Bounds.BoxExtent.Z),
		FMath::Abs(Bounds.Origin.Z) <= Bounds.BoxExtent.Z);

	return true;
}


/**
 * A ship you can see is a ship you can bump into.
 *
 * <strong>Having collision primitives is not the same as being solid to a query.</strong> A
 * character sweeps with bTraceComplex false, and the engine reads that as a choice of simple or
 * complex geometry rather than a preference. A mesh whose collision complexity is
 * "use complex as simple" therefore answers nothing at all to that sweep, however many convex hulls
 * it is carrying -- and it carries them, and the editor draws them, and every count anyone takes
 * comes back right.
 *
 * That is the hole this test exists for, and it is one SpaceMMOSolidity had too: counting elements
 * proves the hulls are there and says nothing about whether anybody can hit them.
 *
 * The size check is here for the other half of the same idea. Collision that has drifted from the
 * mesh it belongs to -- a re-import that changed the mesh's scale and not its hulls, say -- stops a
 * character in mid-air or lets one walk through a hull, and both look like collision being broken
 * rather than like a number being stale.
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
