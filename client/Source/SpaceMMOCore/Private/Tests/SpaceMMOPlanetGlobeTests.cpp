#include "Misc/AutomationTest.h"
#include "SpaceMMOPlanetActor.h"
#include "SpaceMMOPlanetGlobe.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FPlanetConfig GlobeTestPlanet()
	{
		FPlanetConfig Planet;
		Planet.Centre = FSystemCoordinate(FVector(200.0, 0.0, 0.0));
		Planet.RadiusKilometres = 20.0;
		Planet.AtmosphereHeightKilometres = 12.0;

		return Planet;
	}

	FPlanetTerrainConfig GlobeTestTerrain()
	{
		FPlanetTerrainConfig Terrain;
		Terrain.Seed = 20260801;
		Terrain.MaxElevationKilometres = 0.5;

		return Terrain;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOGlobeStandingOnAMountainIsStandingTest,
	"SpaceMMO.Globe.StandingOnAMountainIsStanding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOGlobeStandingOnAMountainIsStandingTest::RunTest(const FString& Parameters)
{
	const FPlanetConfig Planet = GlobeTestPlanet();

	// Half a kilometre of relief on a planet whose surface band is two hundred metres. Measured
	// against the sphere, a player standing on that peak is two and a half bands up and reads as
	// flying — which is what a landed ship reporting ATMOSPHERE was.
	TestEqual(
		TEXT("Feet on the ground is Surface, whatever the ground's own height"),
		FPlanetPhysics::ClassifyProximityAtAltitude(Planet, 0.0, EPlanetProximity::Atmospheric),
		EPlanetProximity::Surface);

	// Leaving takes more than arriving: the hysteresis is a whole kilometre against a band of two
	// hundred metres, so the surface state holds until 1.2 km. Deliberately sticky, and it is what
	// stops a hovering ship flickering between states — but it means "just above the band" is
	// still Surface, which is the opposite of what this test first asserted.
	TestEqual(
		TEXT("Within the hysteresis, still Surface"),
		FPlanetPhysics::ClassifyProximityAtAltitude(Planet, 0.5, EPlanetProximity::Surface),
		EPlanetProximity::Surface);

	TestEqual(
		TEXT("Past the hysteresis, no longer Surface"),
		FPlanetPhysics::ClassifyProximityAtAltitude(Planet, 2.0, EPlanetProximity::Surface),
		EPlanetProximity::Atmospheric);

	TestEqual(
		TEXT("Arriving from above, the band itself is what counts"),
		FPlanetPhysics::ClassifyProximityAtAltitude(Planet, 0.5, EPlanetProximity::Atmospheric),
		EPlanetProximity::Atmospheric);

	TestEqual(
		TEXT("Above the atmosphere is orbital"),
		FPlanetPhysics::ClassifyProximityAtAltitude(Planet, 20.0, EPlanetProximity::Atmospheric),
		EPlanetProximity::Orbital);

	// The position overload has to keep measuring against the sphere: callers with no terrain
	// config cannot ask a height function anything, and silently changing what it means would
	// move every boundary on a planet that has relief.
	const FSystemCoordinate OnTheNominalSurface(
		Planet.Centre.Kilometres + FVector(Planet.RadiusKilometres, 0.0, 0.0));

	TestEqual(
		TEXT("Sphere-relative overload is unchanged"),
		FPlanetPhysics::ClassifyProximity(Planet, OnTheNominalSurface),
		EPlanetProximity::Surface);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOGlobeTopologyTest,
	"SpaceMMO.Globe.Topology",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOGlobeTopologyTest::RunTest(const FString& Parameters)
{
	FPlanetGlobeConfig Config;
	Config.Resolution = 9;

	const FPlanetGlobeMesh Mesh =
		FPlanetGlobe::Build(GlobeTestPlanet(), GlobeTestTerrain(), Config);

	TestEqual(TEXT("Six faces of resolution squared"), Mesh.Positions.Num(), 6 * 81);
	TestEqual(TEXT("One normal per vertex"), Mesh.Normals.Num(), 6 * 81);
	TestEqual(TEXT("Two triangles per cell per face"), Mesh.Triangles.Num(), 6 * 8 * 8 * 6);

	for (const int32 Index : Mesh.Triangles)
	{
		if (!Mesh.Positions.IsValidIndex(Index))
		{
			AddError(FString::Printf(TEXT("Triangle index %d addresses no vertex."), Index));

			return false;
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOGlobeSitsOnTheHeightFieldTest,
	"SpaceMMO.Globe.SitsOnTheHeightField",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOGlobeSitsOnTheHeightFieldTest::RunTest(const FString& Parameters)
{
	const FPlanetConfig Planet = GlobeTestPlanet();
	const FPlanetTerrainConfig Terrain = GlobeTestTerrain();

	FPlanetGlobeConfig Config;
	Config.Resolution = 12;

	const FPlanetGlobeMesh Mesh = FPlanetGlobe::Build(Planet, Terrain, Config);

	// The whole point of generating this rather than scaling a ball: every vertex is on the same
	// surface the physics resolves against and the patch tessellates. A globe that merely looked
	// round would put the drawn ground somewhere other than the walked ground, which is exactly
	// what the engine sphere did.
	double WorstErrorMetres = 0.0;

	for (const FVector& Position : Mesh.Positions)
	{
		const double Kilometres =
			Position.Size() / SpaceMMO::Coordinates::CentimetresPerKilometre;

		const double Expected =
			FPlanetTerrain::SurfaceRadiusKilometres(Planet, Terrain, Position);

		WorstErrorMetres = FMath::Max(WorstErrorMetres, FMath::Abs(Kilometres - Expected) * 1000.0);
	}

	TestTrue(
		FString::Printf(TEXT("Worst vertex is %.4f m off the height field"), WorstErrorMetres),
		WorstErrorMetres < 0.01);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOGlobeFacesOutwardTest,
	"SpaceMMO.Globe.FacesOutward",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOGlobeFacesOutwardTest::RunTest(const FString& Parameters)
{
	FPlanetGlobeConfig Config;
	Config.Resolution = 8;

	const FPlanetGlobeMesh Mesh =
		FPlanetGlobe::Build(GlobeTestPlanet(), GlobeTestTerrain(), Config);

	// Six faces, six chances to get a handedness backwards, and the symptom is a patch of planet
	// lit from the inside — easy to miss on one face of a sphere you are flying past. The patch
	// builder shipped with exactly this bug once.
	int32 Inward = 0;

	for (int32 Index = 0; Index + 2 < Mesh.Triangles.Num(); Index += 3)
	{
		const FVector A = Mesh.Positions[Mesh.Triangles[Index]];
		const FVector B = Mesh.Positions[Mesh.Triangles[Index + 1]];
		const FVector C = Mesh.Positions[Mesh.Triangles[Index + 2]];

		const FVector FaceNormal = FVector::CrossProduct(B - A, C - A);

		// Outward is away from the planet's centre, and the centre is the local origin here.
		if (FVector::DotProduct(FaceNormal, (A + B + C) / 3.0) <= 0.0)
		{
			++Inward;
		}
	}

	TestEqual(TEXT("Triangles wound inward"), Inward, 0);

	for (const FVector& Normal : Mesh.Normals)
	{
		if (!FMath::IsNearlyEqual(Normal.Size(), 1.0, 0.001))
		{
			AddError(TEXT("A vertex normal is not a unit vector."));

			return false;
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOGlobeClosesAtItsSeamsTest,
	"SpaceMMO.Globe.ClosesAtItsSeams",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOGlobeClosesAtItsSeamsTest::RunTest(const FString& Parameters)
{
	constexpr int32 Resolution = 7;

	FPlanetGlobeConfig Config;
	Config.Resolution = Resolution;

	const FPlanetGlobeMesh Mesh =
		FPlanetGlobe::Build(GlobeTestPlanet(), GlobeTestTerrain(), Config);

	// A vertex on the rim of a cube face is also on the rim of the neighbouring face, and the two
	// faces generate it separately. If they do not land on precisely the same point the planet has
	// twelve cracks in it, one along each cube edge, visible as a hairline of space through the
	// ground. "Nearly" is not good enough at 20 km: a hundredth of a percent is two metres.
	int32 Checked = 0;

	for (int32 Vertex = 0; Vertex < Mesh.Positions.Num(); ++Vertex)
	{
		const int32 Face = Vertex / (Resolution * Resolution);
		const int32 Local = Vertex % (Resolution * Resolution);
		const int32 Row = Local / Resolution;
		const int32 Column = Local % Resolution;

		const bool bOnRim =
			Row == 0 || Column == 0 || Row == Resolution - 1 || Column == Resolution - 1;

		if (!bOnRim)
		{
			continue;
		}

		++Checked;

		bool bMatched = false;

		for (int32 Other = 0; Other < Mesh.Positions.Num() && !bMatched; ++Other)
		{
			if (Other / (Resolution * Resolution) == Face)
			{
				continue;
			}

			bMatched = (Mesh.Positions[Other] - Mesh.Positions[Vertex]).Size() < 0.001;
		}

		if (!bMatched)
		{
			AddError(FString::Printf(
				TEXT("Rim vertex %d on face %d has no counterpart on any other face."),
				Vertex,
				Face));

			return false;
		}
	}

	TestTrue(TEXT("Rim vertices were actually examined"), Checked > 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOGlobeHorizonGrowsWithAltitudeTest,
	"SpaceMMO.Globe.HorizonGrowsWithAltitude",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOGlobeHorizonGrowsWithAltitudeTest::RunTest(const FString& Parameters)
{
	const FPlanetConfig Planet = GlobeTestPlanet();

	TestEqual(
		TEXT("Nothing is over the horizon when standing on it"),
		FPlanetGlobe::VisibleCapDegrees(Planet, 0.0),
		0.0);

	// acos(20 / 32) is 51.3 degrees: from the top of the atmosphere rather more than a quarter of
	// the planet is in view, which is why a patch fixed at four degrees left the rest to a sphere.
	TestTrue(
		TEXT("Over half a hemisphere from the atmosphere's edge"),
		FMath::IsNearlyEqual(FPlanetGlobe::VisibleCapDegrees(Planet, 12.0), 51.3, 0.1));

	double Previous = -1.0;

	for (double Altitude = 0.0; Altitude <= 12.0; Altitude += 0.5)
	{
		const double Cap = FPlanetGlobe::VisibleCapDegrees(Planet, Altitude);

		if (Cap <= Previous)
		{
			AddError(FString::Printf(
				TEXT("Horizon stopped growing at %.1f km: %.2f after %.2f degrees."),
				Altitude,
				Cap,
				Previous));

			return false;
		}

		Previous = Cap;
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOGlobePatchCoversWhatIsVisibleTest,
	"SpaceMMO.Globe.PatchCoversWhatIsVisible",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOGlobePatchCoversWhatIsVisibleTest::RunTest(const FString& Parameters)
{
	const FPlanetConfig Planet = GlobeTestPlanet();

	// Standing on the ground the patch stays narrow and therefore detailed. Widening it to the
	// horizon here would spread the same vertex budget over kilometres to cover the few hundred
	// metres a person can actually see.
	TestEqual(
		TEXT("On the ground the patch is at its floor"),
		ASpaceMMOPlanetActor::PatchDegreesForAltitude(Planet, 0.0),
		4.0);

	// The globe is hidden whenever a patch exists, so anywhere the patch falls short of the
	// horizon is a piece of missing planet. It has to keep up over the whole atmospheric band,
	// which is where a descent is flown.
	for (double Altitude = 0.2; Altitude <= 12.0; Altitude += 0.2)
	{
		const double Patch = ASpaceMMOPlanetActor::PatchDegreesForAltitude(Planet, Altitude);
		const double Horizon = FPlanetGlobe::VisibleCapDegrees(Planet, Altitude);

		if (Patch < Horizon)
		{
			AddError(FString::Printf(
				TEXT("At %.1f km the patch spans %.1f degrees but %.1f is in view."),
				Altitude,
				Patch,
				Horizon));

			return false;
		}
	}

	return true;
}

#endif
