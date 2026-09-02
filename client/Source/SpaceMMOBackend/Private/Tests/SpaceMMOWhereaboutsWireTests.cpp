#include "Misc/AutomationTest.h"

#include "SpaceMMOBackendProtocol.h"
#include "SpaceMMOBackendTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Where a returning player is put back, as it arrives on the wire (task 147).
 *
 * <strong>The absence of a position is what these are about.</strong> A wrong position shows up on
 * the first sign-in and points at itself. A missing one that reads as <c>(0, 0, 0)</c> is a real
 * place — the centre of the star system, 640 km below the ground — and it looks exactly like an
 * answer, which is why the flag exists and why it is asserted here rather than trusted.
 *
 * The field names are the ones ASP.NET produces from <c>ResolvedCharacter</c>. A rename on either
 * side would leave every field at its default and put every returning player back at a spawn, with
 * no error anywhere: <c>WhereaboutsEndpointTests</c> pins the same names from the other end.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOWhereaboutsParseTest,
	"SpaceMMO.Whereabouts.Parse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOWhereaboutsParseTest::RunTest(const FString& Parameters)
{
	FBackendResolvedCharacter Resolved;

	TestTrue(
		TEXT("A full answer parses"),
		FSpaceMMOBackendProtocol::ParseResolvedCharacter(
			TEXT("{\"accountId\":3,\"characterId\":7,\"characterName\":\"Wanderer\",")
			TEXT("\"dockedStationId\":null,\"lastSystemX\":640.5,\"lastSystemY\":-12.25,")
			TEXT("\"lastSystemZ\":3.75,\"lastSeenFlying\":true}"),
			Resolved));

	TestTrue(TEXT("...and says it has a position"), Resolved.bHasLastPosition);

	// Asserted to the millimetre, in kilometres. A position rounded to six significant figures --
	// which is what a plain float printf does -- moves a character by a hundred metres, and this
	// is the one number in the game where that is the whole feature going wrong quietly.
	TestEqual(TEXT("X"), Resolved.LastPositionKilometres.X, 640.5, 1e-9);
	TestEqual(TEXT("Y"), Resolved.LastPositionKilometres.Y, -12.25, 1e-9);
	TestEqual(TEXT("Z"), Resolved.LastPositionKilometres.Z, 3.75, 1e-9);

	TestTrue(TEXT("...and that they were flying"), Resolved.bLastSeenFlying);

	return true;
}

/**
 * A character who has never been anywhere is nowhere, not at the origin.
 *
 * The server sends nulls, which is the shape every brand new character arrives in. Reading that as
 * a position would place them at the centre of the system on their very first sign-in — the one
 * case where nobody would think to check.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOWhereaboutsNoPositionTest,
	"SpaceMMO.Whereabouts.NoPosition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOWhereaboutsNoPositionTest::RunTest(const FString& Parameters)
{
	FBackendResolvedCharacter Nulls;

	TestTrue(
		TEXT("Nulls still resolve an identity"),
		FSpaceMMOBackendProtocol::ParseResolvedCharacter(
			TEXT("{\"accountId\":3,\"characterId\":7,\"characterName\":\"New\",")
			TEXT("\"dockedStationId\":null,\"lastSystemX\":null,\"lastSystemY\":null,")
			TEXT("\"lastSystemZ\":null,\"lastSeenFlying\":false}"),
			Nulls));

	TestEqual(TEXT("The character is still resolved"), Nulls.CharacterId, 7);
	TestFalse(TEXT("...and has no position"), Nulls.bHasLastPosition);

	// Absent fields as well as null ones, because an older server, or one this client is newer
	// than, sends neither.
	FBackendResolvedCharacter Absent;

	TestTrue(
		TEXT("An answer with no whereabouts at all parses"),
		FSpaceMMOBackendProtocol::ParseResolvedCharacter(
			TEXT("{\"accountId\":3,\"characterId\":7,\"characterName\":\"Old\"}"),
			Absent));

	TestFalse(TEXT("...and has no position either"), Absent.bHasLastPosition);
	TestFalse(TEXT("...nor a claim about flying"), Absent.bLastSeenFlying);

	// Half a position is no position, which is the same rule the server applies to its own
	// columns. Two coordinates and an implied zero is a point in space that looks entirely real.
	FBackendResolvedCharacter Half;

	TestTrue(
		TEXT("A half-written position parses"),
		FSpaceMMOBackendProtocol::ParseResolvedCharacter(
			TEXT("{\"accountId\":3,\"characterId\":7,\"characterName\":\"Half\",")
			TEXT("\"lastSystemX\":5.0,\"lastSystemY\":6.0}"),
			Half));

	TestFalse(TEXT("...and is not a place to be put"), Half.bHasLastPosition);

	return true;
}

/**
 * The origin is a real place when the server says so.
 *
 * The mirror of the test above, and the reason "has a position" is a flag rather than a comparison
 * against zero: a client that decided by looking at the numbers would refuse to restore anybody
 * standing at the one point it cannot tell from silence.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOWhereaboutsOriginTest,
	"SpaceMMO.Whereabouts.Origin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOWhereaboutsOriginTest::RunTest(const FString& Parameters)
{
	FBackendResolvedCharacter Origin;

	TestTrue(
		TEXT("Explicit zeroes parse"),
		FSpaceMMOBackendProtocol::ParseResolvedCharacter(
			TEXT("{\"accountId\":3,\"characterId\":7,\"characterName\":\"Drifting\",")
			TEXT("\"lastSystemX\":0,\"lastSystemY\":0,\"lastSystemZ\":0,")
			TEXT("\"lastSeenFlying\":true}"),
			Origin));

	TestTrue(TEXT("The origin is somewhere"), Origin.bHasLastPosition);
	TestTrue(TEXT("...and they were flying there"), Origin.bLastSeenFlying);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
