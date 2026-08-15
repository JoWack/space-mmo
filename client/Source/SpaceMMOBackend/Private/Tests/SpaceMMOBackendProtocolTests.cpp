#include "Misc/AutomationTest.h"
#include "SpaceMMOBackendProtocol.h"

#if WITH_DEV_AUTOMATION_TESTS

// Everything the client believes about the server arrives as text, so these are the tests that
// decide whether it believes correct things. All run without a server, a socket, or a game.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOBackendJoinUrlTest,
	"SpaceMMO.Backend.JoinUrl",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOBackendJoinUrlTest::RunTest(const FString& Parameters)
{
	TestEqual(
		TEXT("Plain join"),
		FSpaceMMOBackendProtocol::JoinUrl(TEXT("http://localhost:5000"), TEXT("accounts/login")),
		TEXT("http://localhost:5000/accounts/login"));

	// The interesting cases: both sides carrying a slash, and neither. A doubled slash is routed
	// by some servers and 404'd by others, which makes it a genuinely annoying bug to find.
	TestEqual(
		TEXT("Both sides have a slash"),
		FSpaceMMOBackendProtocol::JoinUrl(TEXT("http://localhost:5000/"), TEXT("/accounts/login")),
		TEXT("http://localhost:5000/accounts/login"));

	TestEqual(
		TEXT("Trailing slashes collapse"),
		FSpaceMMOBackendProtocol::JoinUrl(TEXT("http://localhost:5000///"), TEXT("///health")),
		TEXT("http://localhost:5000/health"));

	TestEqual(
		TEXT("Empty path leaves the base alone"),
		FSpaceMMOBackendProtocol::JoinUrl(TEXT("http://localhost:5000/"), TEXT("")),
		TEXT("http://localhost:5000"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOBackendCredentialsBodyTest,
	"SpaceMMO.Backend.CredentialsBody",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOBackendCredentialsBodyTest::RunTest(const FString& Parameters)
{
	const FString Body =
		FSpaceMMOBackendProtocol::MakeCredentialsBody(TEXT("player@example.com"), TEXT("hunter2"));

	TestTrue(TEXT("Carries the email"), Body.Contains(TEXT("player@example.com")));

	// The point of building this through a JSON writer: a password containing a quote must not be
	// able to end the string early and produce a malformed body.
	const FString Awkward = FSpaceMMOBackendProtocol::MakeCredentialsBody(
		TEXT("a@b.com"), TEXT("pa\"ss\\word"));

	FBackendSession Ignored;

	TestTrue(
		TEXT("A password with quotes and backslashes still produces parseable JSON"),
		Awkward.Contains(TEXT("\\\"")) && Awkward.Contains(TEXT("\\\\")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOBackendParseSessionTest,
	"SpaceMMO.Backend.ParseSession",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOBackendParseSessionTest::RunTest(const FString& Parameters)
{
	FBackendSession Session;

	const bool bParsed = FSpaceMMOBackendProtocol::ParseSession(
		TEXT(R"({"accountId":7,"token":"abc.def","expiresAt":"2026-08-01T12:00:00Z"})"),
		Session);

	TestTrue(TEXT("Parses a well-formed session"), bParsed);
	TestEqual(TEXT("Account id"), Session.AccountId, 7);
	TestEqual(TEXT("Token"), Session.Token, TEXT("abc.def"));
	TestTrue(TEXT("Session is usable"), Session.IsValid());

	// A session with no usable token must fail rather than be stored, or every later request 401s
	// for reasons that look nothing like a login problem.
	FBackendSession Empty;

	TestFalse(
		TEXT("An empty token is refused"),
		FSpaceMMOBackendProtocol::ParseSession(TEXT(R"({"accountId":7,"token":""})"), Empty));

	TestFalse(
		TEXT("A missing token is refused"),
		FSpaceMMOBackendProtocol::ParseSession(TEXT(R"({"accountId":7})"), Empty));

	TestFalse(
		TEXT("Garbage is refused"),
		FSpaceMMOBackendProtocol::ParseSession(TEXT("not json at all"), Empty));

	TestFalse(
		TEXT("An empty body is refused"),
		FSpaceMMOBackendProtocol::ParseSession(FString(), Empty));

	// Expiry is informational, so a missing one must not fail the login.
	FBackendSession NoExpiry;

	TestTrue(
		TEXT("A missing expiry still logs in"),
		FSpaceMMOBackendProtocol::ParseSession(
			TEXT(R"({"accountId":3,"token":"t"})"), NoExpiry));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOBackendParseCharactersTest,
	"SpaceMMO.Backend.ParseCharacters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOBackendParseCharactersTest::RunTest(const FString& Parameters)
{
	TArray<FBackendCharacter> Characters;

	const bool bParsed = FSpaceMMOBackendProtocol::ParseCharacterList(
		TEXT(R"([
			{"id":1,"name":"Vale","race":3,"faction":1,"homeBodyId":4,"balanceMinorUnits":1300000},
			{"id":2,"name":"Rook","race":0,"faction":0,"homeBodyId":1,"balanceMinorUnits":0}
		])"),
		Characters);

	TestTrue(TEXT("Parses the list"), bParsed);
	TestEqual(TEXT("Two characters"), Characters.Num(), 2);
	TestEqual(TEXT("Name"), Characters[0].Name, TEXT("Vale"));
	TestEqual(TEXT("Race maps by value"), Characters[0].Race, EBackendRace::SpaceOrc);
	TestEqual(TEXT("Faction maps by value"), Characters[0].Faction, EBackendFaction::B);
	TestEqual(TEXT("Balance stays exact"), Characters[0].BalanceMinorUnits, 1300000LL);

	// An unknown enum value must land on a defined member rather than being cast blindly, or a
	// server that adds a race crashes every client that has not shipped yet.
	TArray<FBackendCharacter> Future;

	FSpaceMMOBackendProtocol::ParseCharacterList(
		TEXT(R"([{"id":1,"name":"Newcomer","race":99,"faction":42}])"), Future);

	TestEqual(TEXT("Unknown race falls back"), Future[0].Race, EBackendRace::Humanoid);
	TestEqual(TEXT("Unknown faction falls back"), Future[0].Faction, EBackendFaction::A);

	// An empty list is a success, not a failure: a new account has no characters yet.
	TArray<FBackendCharacter> None;

	TestTrue(
		TEXT("An empty array parses"),
		FSpaceMMOBackendProtocol::ParseCharacterList(TEXT("[]"), None));

	TestEqual(TEXT("And yields nothing"), None.Num(), 0);

	// One malformed entry must not discard the rest.
	TArray<FBackendCharacter> Mixed;

	FSpaceMMOBackendProtocol::ParseCharacterList(
		TEXT(R"([{"id":1,"name":"Good"},{"nonsense":true},{"id":3,"name":"AlsoGood"}])"), Mixed);

	TestEqual(TEXT("Skips only the bad entry"), Mixed.Num(), 2);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOBackendLargeNumberTest,
	"SpaceMMO.Backend.LargeNumbers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOBackendLargeNumberTest::RunTest(const FString& Parameters)
{
	// JSON numbers are doubles, which hold 53 bits of integer exactly. A balance past that would
	// silently round, so it is refused instead — and accepted as a string, which is how the server
	// can send one truthfully.
	TArray<FBackendCharacter> Characters;

	FSpaceMMOBackendProtocol::ParseCharacterList(
		TEXT(R"([{"id":1,"name":"Rich","balanceMinorUnits":9007199254740993}])"), Characters);

	TestEqual(
		TEXT("A balance past 2^53 is refused rather than rounded"),
		Characters[0].BalanceMinorUnits,
		0LL);

	TArray<FBackendCharacter> AsString;

	FSpaceMMOBackendProtocol::ParseCharacterList(
		TEXT(R"([{"id":1,"name":"Rich","balanceMinorUnits":"9007199254740993"}])"), AsString);

	TestEqual(
		TEXT("The same value as a string is exact"),
		AsString[0].BalanceMinorUnits,
		9007199254740993LL);

	// The boundary itself is representable and must be accepted.
	TArray<FBackendCharacter> AtLimit;

	FSpaceMMOBackendProtocol::ParseCharacterList(
		TEXT(R"([{"id":1,"name":"Edge","balanceMinorUnits":9007199254740991}])"), AtLimit);

	TestEqual(TEXT("2^53 - 1 is accepted"), AtLimit[0].BalanceMinorUnits, 9007199254740991LL);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOBackendFormatBalanceTest,
	"SpaceMMO.Backend.FormatBalance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOBackendFormatBalanceTest::RunTest(const FString& Parameters)
{
	FBackendCharacter Character;

	Character.BalanceMinorUnits = 1300000;
	TestEqual(TEXT("13,000 credits"), Character.FormatBalance(), TEXT("13,000.00"));

	Character.BalanceMinorUnits = 1;
	TestEqual(TEXT("One minor unit"), Character.FormatBalance(), TEXT("0.01"));

	Character.BalanceMinorUnits = 0;
	TestEqual(TEXT("Zero"), Character.FormatBalance(), TEXT("0.00"));

	// The fractional part is padded, or 1205 minor units prints as "12.5" instead of "12.05".
	Character.BalanceMinorUnits = 1205;
	TestEqual(TEXT("Fraction is zero-padded"), Character.FormatBalance(), TEXT("12.05"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOBackendClassifyFailureTest,
	"SpaceMMO.Backend.ClassifyFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOBackendClassifyFailureTest::RunTest(const FString& Parameters)
{
	// Zero means the request never reached the server, which is worth retrying — unlike a 401,
	// which means logging in again, or a 409, which means telling the player what the rule was.
	TestEqual(
		TEXT("No connection"),
		FSpaceMMOBackendProtocol::ClassifyFailure(0, FString()).Error,
		EBackendError::Transport);

	TestEqual(
		TEXT("200 is not a failure"),
		FSpaceMMOBackendProtocol::ClassifyFailure(200, TEXT("[]")).Error,
		EBackendError::None);

	TestEqual(
		TEXT("201 is not a failure"),
		FSpaceMMOBackendProtocol::ClassifyFailure(201, TEXT("{}")).Error,
		EBackendError::None);

	TestEqual(
		TEXT("401"),
		FSpaceMMOBackendProtocol::ClassifyFailure(401, FString()).Error,
		EBackendError::Unauthenticated);

	TestEqual(
		TEXT("404"),
		FSpaceMMOBackendProtocol::ClassifyFailure(404, FString()).Error,
		EBackendError::NotFound);

	TestEqual(
		TEXT("409"),
		FSpaceMMOBackendProtocol::ClassifyFailure(409, FString()).Error,
		EBackendError::Rejected);

	TestEqual(
		TEXT("500"),
		FSpaceMMOBackendProtocol::ClassifyFailure(500, FString()).Error,
		EBackendError::Server);

	// Both error shapes the API actually returns.
	TestEqual(
		TEXT("Explicit error field"),
		FSpaceMMOBackendProtocol::ClassifyFailure(
			409, TEXT(R"({"error":"That character name is taken."})")).Message,
		TEXT("That character name is taken."));

	TestEqual(
		TEXT("Problem details detail field"),
		FSpaceMMOBackendProtocol::ClassifyFailure(
			400, TEXT(R"({"title":"One or more validation errors.","detail":"Name is too short."})")).Message,
		TEXT("Name is too short."));

	// A failure with an unreadable body still has to say something useful.
	TestFalse(
		TEXT("An empty body still produces a message"),
		FSpaceMMOBackendProtocol::ClassifyFailure(503, FString()).Message.IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOBackendParseSkillsAndInventoryTest,
	"SpaceMMO.Backend.ParseSkillsAndInventory",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOBackendParseSkillsAndInventoryTest::RunTest(const FString& Parameters)
{
	TArray<FBackendSkill> Skills;

	// Copied from what the API actually answers with -- the capped skill and a part-trained one,
	// including the progress figures the skills screen draws its bars from.
	FSpaceMMOBackendProtocol::ParseSkills(
		TEXT(R"([{"key":"mining","name":"Mining","category":0,"xp":13034431,"level":99,
			  "xpToNextLevel":0,"progressToNextLevel":1},
			 {"key":"refining","name":"Refining","category":0,"xp":100,"level":2,
			  "xpToNextLevel":74,"progressToNextLevel":0.18681318681318682}])"),
		Skills);

	TestEqual(TEXT("Two skills"), Skills.Num(), 2);
	TestEqual(TEXT("Key"), Skills[0].Key, TEXT("mining"));

	// The level comes from the server. Deriving it here would be a second implementation of the
	// XP curve, and two implementations of a rule are two chances to disagree about it.
	TestEqual(TEXT("Level 99 at the curve's cap"), Skills[0].Level, 99);
	TestEqual(TEXT("XP survives as int64"), Skills[0].Xp, 13034431LL);

	// Progress arrives the same way and for the same reason. SkillCurve's thresholds are built with
	// order-sensitive flooring, so a C++ copy of the curve would reproduce that or disagree quietly.
	TestEqual(TEXT("Nothing left to earn at the cap"), Skills[0].XpToNextLevel, 0LL);
	TestTrue(TEXT("The cap reads as a full bar"), Skills[0].ProgressToNextLevel > 0.999f);

	TestEqual(TEXT("XP to the next level"), Skills[1].XpToNextLevel, 74LL);
	TestEqual(
		TEXT("Part way through level 2"), Skills[1].ProgressToNextLevel, 0.1868f, 0.0001f);
	TestTrue(TEXT("Both skills report progress"), Skills[1].HasProgress());

	// A server too old to send progress must produce a screen with no bars, not one where every
	// skill claims to have just started its level -- which is what a plain zero default would say.
	TArray<FBackendSkill> WithoutProgress;

	FSpaceMMOBackendProtocol::ParseSkills(
		TEXT(R"([{"key":"mining","name":"Mining","category":0,"xp":100,"level":2}])"),
		WithoutProgress);

	TestEqual(TEXT("Still parsed"), WithoutProgress.Num(), 1);
	TestEqual(TEXT("Level still read"), WithoutProgress[0].Level, 2);
	TestFalse(TEXT("But it says it has no progress"), WithoutProgress[0].HasProgress());

	TArray<FBackendInventoryItem> Items;
	TArray<FBackendItemInstance> Instances;
	TArray<FBackendInventoryContainer> Containers;

	// Two lists, because owning something takes two shapes. This was a bare array of stacks, and
	// anything with condition — every tool, weapon and hull — was simply absent from it, so a
	// player could craft the mining laser the questline gives them and find nothing.
	FSpaceMMOBackendProtocol::ParseInventory(
		TEXT(R"({"stacks":[
			{"itemDefId":4,"itemKey":"ferrite_ore","name":"Ferrite Ore","quantity":250,
			 "factionBuyPriceMinorUnits":null,"kind":1,"stationId":null}],
		 "items":[
			{"id":77,"itemDefId":9,"itemKey":"crude_mining_laser","name":"Crude Mining Laser",
			 "condition":87,"kind":2,"stationId":3}]})"),
		Items,
		Instances,
		Containers);

	TestEqual(TEXT("One stack"), Items.Num(), 1);
	TestEqual(TEXT("Item key"), Items[0].ItemKey, TEXT("ferrite_ore"));
	TestEqual(TEXT("Quantity"), Items[0].Quantity, 250);

	TestEqual(TEXT("One instance"), Instances.Num(), 1);
	TestEqual(TEXT("Instance id"), Instances[0].Id, static_cast<int64>(77));
	TestEqual(TEXT("Instance name"), Instances[0].Name, TEXT("Crude Mining Laser"));
	TestEqual(TEXT("Condition survives"), Instances[0].Condition, 87);
	TestEqual(TEXT("Where it is"), Instances[0].StationId, 3);

	// A server that still answers with stacks alone leaves a client with no tools rather than no
	// inventory, which is the milder of the two failures.
	TArray<FBackendInventoryItem> OnlyStacks;
	TArray<FBackendItemInstance> NoInstances;
	TArray<FBackendInventoryContainer> NoContainers;

	TestTrue(
		TEXT("An envelope without instances still parses"),
		FSpaceMMOBackendProtocol::ParseInventory(
			TEXT(R"({"stacks":[]})"), OnlyStacks, NoInstances, NoContainers));

	TArray<FBackendSkill> NoSkills;

	TestTrue(
		TEXT("An empty skill list parses"),
		FSpaceMMOBackendProtocol::ParseSkills(TEXT("[]"), NoSkills));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOParseResourceNodesTest,
	"SpaceMMO.Backend.ParseResourceNodes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOParseResourceNodesTest::RunTest(const FString& Parameters)
{
	// The real response body, copied from the running API rather than invented, so this test
	// fails if the endpoint's field names ever drift away from what the client reads.
	const FString Json = TEXT(R"([
		{"id":1,"key":"node_capital_ferrite_a","bodyId":5,"itemKey":"ferrite_ore",
		 "itemName":"Ferrite Ore","skillKey":"mining","requiredLevel":1,"quantityMax":200,
		 "directionX":-0.9998000599800071,"directionY":0.01999600119960014,"directionZ":0,
		 "requiredToolKey":"crude_mining_laser","requiredToolName":"Crude Mining Laser"},
		{"id":3,"key":"node_capital_scrap_a","bodyId":5,"itemKey":"scrap_alloy",
		 "itemName":"Scrap Alloy","skillKey":"gathering","requiredLevel":1,"quantityMax":25,
		 "directionX":-0.9991291389208884,"directionY":0.03996516555683554,
		 "directionZ":-0.011989549667050662,
		 "requiredToolKey":null,"requiredToolName":null}
	])");

	TArray<FBackendResourceNode> Nodes;

	TestTrue(TEXT("Parsed"), FSpaceMMOBackendProtocol::ParseResourceNodes(Json, Nodes));
	TestEqual(TEXT("Both deposits"), Nodes.Num(), 2);

	if (Nodes.Num() != 2)
	{
		return false;
	}

	TestEqual(TEXT("Key"), Nodes[0].Key, FString(TEXT("node_capital_ferrite_a")));
	TestEqual(TEXT("Item"), Nodes[0].ItemKey, FString(TEXT("ferrite_ore")));
	TestEqual(TEXT("Name"), Nodes[0].ItemName, FString(TEXT("Ferrite Ore")));
	TestEqual(TEXT("Skill"), Nodes[0].SkillKey, FString(TEXT("mining")));
	TestEqual(TEXT("Id"), Nodes[0].Id, static_cast<int64>(1));
	TestEqual(TEXT("Body"), Nodes[0].BodyId, 5);
	TestEqual(TEXT("Quantity"), Nodes[0].QuantityMax, 200);

	// Both deposits in the fixture are the two real cases, and the second is why it is a scrap node
	// rather than a second ferrite one: a bare-handed deposit sends null, not an absent field, and
	// a parser that treated null as "some tool" would gate the very deposit the onboarding chain
	// starts with.
	TestTrue(TEXT("A mining deposit needs a tool"), Nodes[0].NeedsTool());
	TestEqual(
		TEXT("Tool key"), Nodes[0].RequiredToolKey, FString(TEXT("crude_mining_laser")));
	TestEqual(
		TEXT("Tool name"), Nodes[0].RequiredToolName, FString(TEXT("Crude Mining Laser")));

	TestFalse(TEXT("A gathering deposit needs no tool"), Nodes[1].NeedsTool());
	TestTrue(TEXT("Null tool leaves the key empty"), Nodes[1].RequiredToolKey.IsEmpty());

	// Direction is what everything else depends on. A deposit whose direction arrived wrong is
	// drawn somewhere the server does not think it is, and gathering fails for invisible reasons.
	TestTrue(
		TEXT("Direction is a unit vector"),
		FMath::IsNearlyEqual(Nodes[0].Direction.Size(), 1.0, 0.000001));

	TestTrue(
		TEXT("Direction points the way the server said"),
		Nodes[0].Direction.X < -0.99 && Nodes[0].Direction.Y > 0.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOResourceNodeWithoutDirectionIsDroppedTest,
	"SpaceMMO.Backend.ResourceNodeWithoutDirectionIsDropped",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOResourceNodeWithoutDirectionIsDroppedTest::RunTest(const FString& Parameters)
{
	// Three deposits the client cannot place: no direction at all, a zero vector, and a direction
	// that is not a number. Dropping them is the point — a defaulted direction would put a deposit
	// at the planet's core, where it is unreachable and nothing ever reported a problem.
	const FString Json = TEXT(R"([
		{"id":1,"key":"no_direction","itemKey":"ferrite_ore","quantityMax":10},
		{"id":2,"key":"zero_direction","itemKey":"ferrite_ore","quantityMax":10,
		 "directionX":0,"directionY":0,"directionZ":0},
		{"id":3,"key":"text_direction","itemKey":"ferrite_ore","quantityMax":10,
		 "directionX":"north","directionY":0,"directionZ":0},
		{"id":4,"key":"good","itemKey":"ferrite_ore","quantityMax":10,
		 "directionX":0,"directionY":0,"directionZ":1}
	])");

	TArray<FBackendResourceNode> Nodes;

	TestTrue(TEXT("Parsed"), FSpaceMMOBackendProtocol::ParseResourceNodes(Json, Nodes));

	// The array as a whole is still valid; only the unusable entries are gone. One bad deposit
	// must not cost a player every other deposit on the planet.
	TestEqual(TEXT("Only the placeable deposit survives"), Nodes.Num(), 1);

	if (Nodes.Num() == 1)
	{
		TestEqual(TEXT("And it is the good one"), Nodes[0].Key, FString(TEXT("good")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOParseBodiesTest,
	"SpaceMMO.Backend.ParseBodies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOParseBodiesTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"([
		{"id":2,"key":"body_ares","name":"Ares","starSystemId":1,"radiusKm":339},
		{"id":5,"key":"body_capital","name":"The Capital","starSystemId":1,"radiusKm":700}
	])");

	TArray<FBackendBody> Bodies;

	TestTrue(TEXT("Parsed"), FSpaceMMOBackendProtocol::ParseBodies(Json, Bodies));
	TestEqual(TEXT("Both bodies"), Bodies.Num(), 2);

	if (Bodies.Num() != 2)
	{
		return false;
	}

	// Looked up by key, because ids are assigned by whichever database seeded last.
	TestEqual(TEXT("Capital key"), Bodies[1].Key, FString(TEXT("body_capital")));
	TestEqual(TEXT("Capital id"), Bodies[1].Id, 5);
	TestEqual(TEXT("Capital radius"), Bodies[1].RadiusKilometres, 700.0);

	return true;
}

#endif
