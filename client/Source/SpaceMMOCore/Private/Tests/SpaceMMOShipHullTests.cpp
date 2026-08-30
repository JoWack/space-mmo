#include "Misc/AutomationTest.h"

#include "Engine/StaticMesh.h"
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

#endif // WITH_DEV_AUTOMATION_TESTS
