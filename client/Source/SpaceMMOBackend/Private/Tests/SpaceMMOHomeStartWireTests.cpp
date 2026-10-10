#include "Misc/AutomationTest.h"
#include "SpaceMMOBackendProtocol.h"
#include "SpaceMMOBackendTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * Task 145: what the game server needs to stand a new character at home, as the backend sends it.
 *
 * The bodies payload is the real one, captured verbatim from GET /world/bodies on 10 October after
 * seeding, not typed by hand: the rule task 116 earned for anything that crosses the wire.
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOHomeStartStartsArriveFromTheWireTest,
	"SpaceMMO.HomeStart.StartsArriveFromTheWire",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOHomeStartStartsArriveFromTheWireTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"([{"id":2,"key":"body_ares","name":"Ares","starSystemId":1,"radiusKm":339,"systemX":40,"systemY":-180,"systemZ":30,"lowColour":"0.35,0.12,0.07","highColour":"0.72,0.42,0.26","rockColour":"0.18,0.15,0.14","heightFrom":0.22,"heightTo":0.75,"slopeFrom":0.15,"slopeTo":0.45,"terrainSeed":20260802,"maxElevationKm":0.5,"baseFrequency":12,"terrainPads":[],"startX":-0.99903,"startY":-0.020398,"startZ":0.039025},{"id":5,"key":"body_capital","name":"The Capital","starSystemId":1,"radiusKm":700,"systemX":60,"systemY":0,"systemZ":0,"lowColour":"0.16,0.19,0.15","highColour":"0.62,0.60,0.55","rockColour":"0.34,0.33,0.32","heightFrom":0.24,"heightTo":0.68,"slopeFrom":0.08,"slopeTo":0.19,"terrainSeed":20260805,"maxElevationKm":0.35,"baseFrequency":6,"terrainPads":[{"stationKey":"station_capital_hub","directionX":0.026,"directionY":0,"directionZ":1,"flatRadiusKm":0.42,"blendKm":0.15,"elevationKm":0.172}],"startX":null,"startY":null,"startZ":null},{"id":4,"key":"body_grimhold","name":"Grimhold","starSystemId":1,"radiusKm":780,"systemX":-60,"systemY":-100,"systemZ":160,"lowColour":"0.03,0.03,0.03","highColour":"0.34,0.31,0.28","rockColour":"0.14,0.13,0.13","heightFrom":0.25,"heightTo":0.85,"slopeFrom":0.25,"slopeTo":0.62,"terrainSeed":20260804,"maxElevationKm":0.7,"baseFrequency":16,"terrainPads":[],"startX":-0.999451,"startY":-0.024986,"startZ":-0.021739},{"id":1,"key":"body_terra","name":"Terra","starSystemId":1,"radiusKm":637.1,"systemX":-120,"systemY":60,"systemZ":0,"lowColour":"0.10,0.14,0.09","highColour":"0.55,0.55,0.48","rockColour":"0.30,0.30,0.31","heightFrom":0.25,"heightTo":0.72,"slopeFrom":0.18,"slopeTo":0.38,"terrainSeed":20260801,"maxElevationKm":0.5,"baseFrequency":9,"terrainPads":[],"startX":-0.999516,"startY":0.029985,"startZ":0.008245},{"id":3,"key":"body_verdance","name":"Verdance","starSystemId":1,"radiusKm":610,"systemX":200,"systemY":140,"systemZ":-60,"lowColour":"0.05,0.13,0.05","highColour":"0.38,0.58,0.20","rockColour":"0.26,0.21,0.15","heightFrom":0.26,"heightTo":0.69,"slopeFrom":0.1,"slopeTo":0.26,"terrainSeed":20260803,"maxElevationKm":0.4,"baseFrequency":7,"terrainPads":[],"startX":-0.999384,"startY":0.014991,"startZ":-0.031732}])");

	TArray<FBackendBody> Bodies;

	TestTrue(TEXT("Parsed"), FSpaceMMOBackendProtocol::ParseBodies(Json, Bodies));

	int32 WithStart = 0;

	for (const FBackendBody& Body : Bodies)
	{
		if (Body.Key == TEXT("body_capital"))
		{
			// The server sends nulls for a body that is nobody's home, which must not read as a
			// direction of zero -- or as anything at all.
			TestFalse(TEXT("The Capital is nobody's home"), Body.bHasStart);
			continue;
		}

		WithStart += Body.bHasStart ? 1 : 0;

		if (Body.Key == TEXT("body_ares"))
		{
			// Each component its own value, so a parser reading one axis three times fails.
			TestEqual(TEXT("Ares' start X"), Body.StartDirection.X, -0.99903, 1e-9);
			TestEqual(TEXT("Ares' start Y"), Body.StartDirection.Y, -0.020398, 1e-9);
			TestEqual(TEXT("Ares' start Z"), Body.StartDirection.Z, 0.039025, 1e-9);
		}
	}

	TestEqual(TEXT("Every homeworld has a start"), WithStart, 4);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOHomeStartHomeBodyArrivesWithIdentityTest,
	"SpaceMMO.HomeStart.HomeBodyArrivesWithIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOHomeStartHomeBodyArrivesWithIdentityTest::RunTest(const FString& Parameters)
{
	// The shape ResolvedCharacter serialises to: a new character, never anywhere, from Ares.
	FBackendResolvedCharacter Resolved;

	TestTrue(
		TEXT("Parsed"),
		FSpaceMMOBackendProtocol::ParseResolvedCharacter(
			TEXT("{\"accountId\":3,\"characterId\":7,\"characterName\":\"Newcomer\",")
			TEXT("\"dockedStationId\":null,\"lastSystemX\":null,\"lastSystemY\":null,")
			TEXT("\"lastSystemZ\":null,\"lastSeenFlying\":false,\"homeBodyId\":2}"),
			Resolved));

	TestFalse(TEXT("Never anywhere"), Resolved.bHasLastPosition);
	TestEqual(TEXT("Home is Ares"), Resolved.HomeBodyId, 2);

	// An older server sends no home, which leaves the character at the spawn, as before this task.
	FBackendResolvedCharacter Older;

	TestTrue(
		TEXT("Parsed without one"),
		FSpaceMMOBackendProtocol::ParseResolvedCharacter(
			TEXT("{\"accountId\":3,\"characterId\":7,\"characterName\":\"Newcomer\"}"), Older));

	TestEqual(TEXT("No home"), Older.HomeBodyId, 0);

	return true;
}

#endif
