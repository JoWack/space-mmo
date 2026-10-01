#include "Misc/AutomationTest.h"
#include "SpaceMMOBackendProtocol.h"
#include "SpaceMMOCharacterScreens.h"
#include "SpaceMMOGameMenus.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * Task 110's menus: what they parse, what they say, and which one appears.
 *
 * The JSON here is not written by hand. Each body was captured from the API's own response in
 * SpaceMMO.Api.Tests.CharacterSelectTests on 1 October and pasted verbatim, because five green tests
 * on hand-built inputs once missed a market filter wired to nothing (CLAUDE.md, "Feed in the real
 * value at least once").
 */
namespace SpaceMMOMenusTests
{
	/** A character list in which the character was last seen on a world, on foot. */
	const TCHAR* const ListOnFoot =
		TEXT("[{\"id\":1,\"name\":\"Selector\",\"race\":1,\"faction\":0,\"homeBodyId\":1,")
		TEXT("\"balanceMinorUnits\":0,\"activeShipItemInstanceId\":null,\"lastSeenWorld\":\"Big World\",")
		TEXT("\"lastSeenFlying\":false,\"lastSeenShip\":null}]");

	/** And one sitting in a Shuttle, with no position recorded. */
	const TCHAR* const ListInShip =
		TEXT("[{\"id\":1,\"name\":\"Selector\",\"race\":1,\"faction\":0,\"homeBodyId\":1,")
		TEXT("\"balanceMinorUnits\":0,\"activeShipItemInstanceId\":null,\"lastSeenWorld\":null,")
		TEXT("\"lastSeenFlying\":true,\"lastSeenShip\":\"Shuttle\"}]");

	const TCHAR* const Races =
		TEXT("[{\"race\":0,\"name\":\"Humanoid\",\"faction\":0,\"factionName\":\"Humanity United\",")
		TEXT("\"homeBodyKey\":\"body_terra\",\"homeBodyName\":\"Humanoid\"},")
		TEXT("{\"race\":1,\"name\":\"Martian\",\"faction\":0,\"factionName\":\"Humanity United\",")
		TEXT("\"homeBodyKey\":\"body_ares\",\"homeBodyName\":\"Martian\"},")
		TEXT("{\"race\":2,\"name\":\"Space Elf\",\"faction\":1,\"factionName\":\"Tusk and Thorn\",")
		TEXT("\"homeBodyKey\":\"body_verdance\",\"homeBodyName\":\"SpaceElf\"},")
		TEXT("{\"race\":3,\"name\":\"Space Orc\",\"faction\":1,\"factionName\":\"Tusk and Thorn\",")
		TEXT("\"homeBodyKey\":\"body_grimhold\",\"homeBodyName\":\"SpaceOrc\"}]");

	const TCHAR* const RefusedName =
		TEXT("{\"type\":\"https://tools.ietf.org/html/rfc9110#section-15.5.1\",")
		TEXT("\"title\":\"One or more validation errors occurred.\",\"status\":400,")
		TEXT("\"errors\":{\"name\":[\"Name must be between 3 and 20 characters.\"]},")
		TEXT("\"traceId\":\"00-92f2438f0fcb50132f1b53075d5825dc-ec11f8cc7626cca7-00\"}");

	FBackendCharacter Character(
		const FString& World, const bool bFlying, const FString& Ship, const EBackendRace Race = EBackendRace::Martian)
	{
		FBackendCharacter Made;
		Made.Id = 7;
		Made.Name = TEXT("Kestrel");
		Made.Race = Race;
		Made.BalanceMinorUnits = 125000;
		Made.LastSeenWorld = World;
		Made.bLastSeenFlying = bFlying;
		Made.LastSeenShip = Ship;

		return Made;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOMenusCharacterListWireTest,
	"SpaceMMO.Menus.CharacterListCarriesLastSeen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOMenusCharacterListWireTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMOMenusTests;

	TArray<FBackendCharacter> OnFoot;

	TestTrue(TEXT("Parses"), FSpaceMMOBackendProtocol::ParseCharacterList(ListOnFoot, OnFoot));
	TestEqual(TEXT("One character"), OnFoot.Num(), 1);

	if (OnFoot.Num() == 1)
	{
		TestEqual(TEXT("World"), OnFoot[0].LastSeenWorld, FString(TEXT("Big World")));
		TestFalse(TEXT("On foot"), OnFoot[0].bLastSeenFlying);
		TestTrue(TEXT("A null ship is empty"), OnFoot[0].LastSeenShip.IsEmpty());
	}

	TArray<FBackendCharacter> InShip;

	FSpaceMMOBackendProtocol::ParseCharacterList(ListInShip, InShip);

	if (InShip.Num() == 1)
	{
		TestTrue(TEXT("A null world is empty"), InShip[0].LastSeenWorld.IsEmpty());
		TestTrue(TEXT("Flying"), InShip[0].bLastSeenFlying);
		TestEqual(TEXT("Ship"), InShip[0].LastSeenShip, FString(TEXT("Shuttle")));
	}
	else
	{
		AddError(TEXT("The in-ship list did not parse to one character"));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOMenusRacesWireTest,
	"SpaceMMO.Menus.RacesParse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOMenusRacesWireTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMOMenusTests;

	TArray<FBackendRace> Parsed;

	TestTrue(TEXT("Parses"), FSpaceMMOBackendProtocol::ParseRaces(Races, Parsed));

	// Counted against the enum, not as a literal 4: a fifth race would be added to both ends at once.
	TestEqual(TEXT("Every race the client knows"), Parsed.Num(), static_cast<int32>(EBackendRace::SpaceOrc) + 1);

	const FBackendRace* Elf = Parsed.FindByPredicate(
		[](const FBackendRace& Race) { return Race.Race == EBackendRace::SpaceElf; });

	if (Elf == nullptr)
	{
		AddError(TEXT("No Space Elf"));

		return false;
	}

	TestEqual(TEXT("Display name"), Elf->Name, FString(TEXT("Space Elf")));
	TestTrue(TEXT("Faction"), Elf->Faction == EBackendFaction::B);
	TestEqual(TEXT("Faction name"), Elf->FactionName, FString(TEXT("Tusk and Thorn")));
	TestEqual(TEXT("Home key"), Elf->HomeBodyKey, FString(TEXT("body_verdance")));

	// A race this build has no value for is dropped, not clamped into a second Humanoid row that
	// would create the wrong thing.
	TArray<FBackendRace> WithUnknown;

	FSpaceMMOBackendProtocol::ParseRaces(
		TEXT("[{\"race\":9,\"name\":\"Newcomer\",\"faction\":0,\"factionName\":\"Humanity United\",")
		TEXT("\"homeBodyKey\":\"body_x\",\"homeBodyName\":null}]"),
		WithUnknown);

	TestEqual(TEXT("Unknown race dropped"), WithUnknown.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOMenusRefusalWordsTest,
	"SpaceMMO.Menus.RefusalIsTheServersWords",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOMenusRefusalWordsTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMOMenusTests;

	// The real 400. Before task 110 this read as its title, "One or more validation errors occurred."
	TestEqual(
		TEXT("Validation problem"),
		FSpaceMMOBackendProtocol::ClassifyFailure(400, RefusedName).Message,
		FString(TEXT("Name must be between 3 and 20 characters.")));

	TestEqual(
		TEXT("Conflict"),
		FSpaceMMOBackendProtocol::ClassifyFailure(409, TEXT("{\"error\":\"That character name is taken.\"}")).Message,
		FString(TEXT("That character name is taken.")));

	// A problem with no field errors still says its detail, as before.
	TestEqual(
		TEXT("Plain problem"),
		FSpaceMMOBackendProtocol::ExtractErrorMessage(TEXT("{\"title\":\"Bad\",\"detail\":\"Specific\"}")),
		FString(TEXT("Specific")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOMenusLastSeenTest,
	"SpaceMMO.Menus.LastSeenLine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOMenusLastSeenTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMOMenusTests;

	auto Line = [](const FBackendCharacter& Made) { return USpaceMMOCharacterSelectScreen::DescribeLastSeen(Made); };

	TestEqual(TEXT("Never played"), Line(Character(TEXT(""), false, TEXT(""))), FString(TEXT("Not played yet")));
	TestEqual(TEXT("On foot"), Line(Character(TEXT("Ares"), false, TEXT(""))), FString(TEXT("Last seen: Ares, on foot")));
	TestEqual(TEXT("Flying a hull"), Line(Character(TEXT("Ares"), true, TEXT("Shuttle"))), FString(TEXT("Last seen: Ares, in the Shuttle")));

	// Sitting in a parked ship is still in it.
	TestEqual(TEXT("Landed in a hull"), Line(Character(TEXT("Ares"), false, TEXT("Shuttle"))), FString(TEXT("Last seen: Ares, in the Shuttle")));

	// The unowned prop ship has no name to give.
	TestEqual(TEXT("Flying the prop ship"), Line(Character(TEXT("Ares"), true, TEXT(""))), FString(TEXT("Last seen: Ares, in a ship")));

	// And from the wire, end to end.
	TArray<FBackendCharacter> FromWire;

	FSpaceMMOBackendProtocol::ParseCharacterList(ListOnFoot, FromWire);

	if (FromWire.Num() == 1)
	{
		TestEqual(TEXT("From the wire"), Line(FromWire[0]), FString(TEXT("Last seen: Big World, on foot")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOMenusCharacterRowTest,
	"SpaceMMO.Menus.CharacterRow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOMenusCharacterRowTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMOMenusTests;

	TArray<FBackendRace> Parsed;

	FSpaceMMOBackendProtocol::ParseRaces(Races, Parsed);

	const FSpaceMMOCharacterRowText Row =
		USpaceMMOCharacterSelectScreen::BuildRow(Character(TEXT("Ares"), false, TEXT("")), Parsed);

	TestEqual(TEXT("Name"), Row.Name, FString(TEXT("Kestrel")));
	TestEqual(TEXT("Lineage"), Row.Lineage, FString(TEXT("Martian · Humanity United")));
	TestEqual(TEXT("Credits"), Row.Credits, FString(TEXT("1,250.00 cr")));
	TestEqual(TEXT("Id"), Row.CharacterId, 7);

	// Before the races arrive: blank, not the enum's spelling.
	const FSpaceMMOCharacterRowText Early =
		USpaceMMOCharacterSelectScreen::BuildRow(Character(TEXT("Ares"), false, TEXT("")), {});

	TestTrue(TEXT("No lineage before races load"), Early.Lineage.IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOMenusRaceRowTest,
	"SpaceMMO.Menus.RaceRow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOMenusRaceRowTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMOMenusTests;

	FBackendRace Martian;
	Martian.Race = EBackendRace::Martian;
	Martian.Name = TEXT("Martian");
	Martian.FactionName = TEXT("Humanity United");
	Martian.HomeBodyKey = TEXT("body_ares");
	Martian.HomeBodyName = TEXT("Ares");

	FBackendBody Ares;
	Ares.Key = TEXT("body_ares");
	Ares.bHasAppearance = true;
	Ares.LowColour = FLinearColor(0.5f, 0.1f, 0.05f);

	FBackendBody Elsewhere;
	Elsewhere.Key = TEXT("body_terra");
	Elsewhere.bHasAppearance = true;
	Elsewhere.LowColour = FLinearColor(0.0f, 0.3f, 0.0f);

	const FSpaceMMORaceRowText Row = USpaceMMONewCharacterScreen::BuildRaceRow(Martian, {Elsewhere, Ares});

	TestEqual(TEXT("The agreed line"), Row.Summary, FString(TEXT("Martian — Humanity United — home world Ares")));
	TestTrue(TEXT("Has a palette"), Row.bHasPalette);

	// Joined by key, so the other body's colour cannot be the one shown.
	TestEqual(TEXT("Ares's colour, not Terra's"), Row.LowColour, Ares.LowColour);

	const FSpaceMMORaceRowText Unpainted = USpaceMMONewCharacterScreen::BuildRaceRow(Martian, {Elsewhere});

	TestFalse(TEXT("No palette without its body"), Unpainted.bHasPalette);

	FBackendRace Unseeded = Martian;
	Unseeded.HomeBodyName.Reset();

	TestEqual(
		TEXT("Key when the body is unseeded"),
		USpaceMMONewCharacterScreen::BuildRaceRow(Unseeded, {}).HomeWorld,
		FString(TEXT("home world body_ares")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOMenusOpeningTest,
	"SpaceMMO.Menus.WhatFollowsSignIn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOMenusOpeningTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMOMenusTests;

	const TArray<FBackendCharacter> Two = {Character(TEXT("Ares"), false, TEXT("")), Character(TEXT(""), false, TEXT(""))};
	const TArray<FBackendCharacter> None;

	auto Decide = [](int32 Desired, bool bFile, const TArray<FBackendCharacter>& Characters, bool bSelect, bool bNew)
	{
		return FSpaceMMOOpening::Decide(Desired, bFile, Characters, bSelect, bNew);
	};

	// -CharacterId= wins over everything.
	FSpaceMMOOpening Opening = Decide(42, false, Two, true, true);
	TestTrue(TEXT("Named character is claimed"), Opening.Kind == ESpaceMMOOpening::Claim);
	TestEqual(TEXT("The named one"), Opening.CharacterId, 42);

	// The credentials file plays the first, as before: two clients on one desktop.
	Opening = Decide(0, true, Two, true, true);
	TestTrue(TEXT("File sign-in claims"), Opening.Kind == ESpaceMMOOpening::Claim);
	TestEqual(TEXT("The first"), Opening.CharacterId, Two[0].Id);

	// No screen: exactly the old behaviour.
	TestTrue(TEXT("No select screen claims"), Decide(0, false, Two, false, false).Kind == ESpaceMMOOpening::Claim);
	TestTrue(TEXT("No screens, no characters"), Decide(0, false, None, false, false).Kind == ESpaceMMOOpening::NothingToPlay);

	// The screens.
	TestTrue(TEXT("Characters: choose"), Decide(0, false, Two, true, true).Kind == ESpaceMMOOpening::ChooseCharacter);
	TestTrue(TEXT("None: create"), Decide(0, false, None, true, true).Kind == ESpaceMMOOpening::CreateCharacter);
	TestTrue(TEXT("None, file-signed: still somewhere to create"), Decide(0, true, None, true, true).Kind == ESpaceMMOOpening::CreateCharacter);
	TestTrue(TEXT("None and no creation screen: an empty select"), Decide(0, false, None, true, false).Kind == ESpaceMMOOpening::ChooseCharacter);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOMenusSettingsTextTest,
	"SpaceMMO.Menus.SettingsText",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOMenusSettingsTextTest::RunTest(const FString& Parameters)
{
	FIntPoint Parsed;

	TestTrue(TEXT("Round trip"), USpaceMMOSettingsScreen::ParseResolution(
		USpaceMMOSettingsScreen::DescribeResolution(FIntPoint(2560, 1440)), Parsed));
	TestTrue(TEXT("Same resolution"), Parsed == FIntPoint(2560, 1440));

	TestFalse(TEXT("Not a resolution"), USpaceMMOSettingsScreen::ParseResolution(TEXT("Custom"), Parsed));
	TestFalse(TEXT("Zero"), USpaceMMOSettingsScreen::ParseResolution(TEXT("0 x 1080"), Parsed));

	for (const FString& Name : USpaceMMOSettingsScreen::WindowModeNames())
	{
		TestEqual(
			*FString::Printf(TEXT("Window mode '%s' round trips"), *Name),
			USpaceMMOSettingsScreen::WindowModeName(USpaceMMOSettingsScreen::WindowModeFromName(Name)),
			Name);
	}

	// -1 is Unreal's answer when the scalability groups disagree, and must not be applied as a level.
	TestEqual(TEXT("Custom"), USpaceMMOSettingsScreen::QualityName(-1), FString(TEXT("Custom")));
	TestEqual(TEXT("Custom is no level"), USpaceMMOSettingsScreen::QualityFromName(TEXT("Custom")), -1);
	TestEqual(TEXT("Epic"), USpaceMMOSettingsScreen::QualityFromName(TEXT("Epic")), 3);

	return true;
}

#endif
