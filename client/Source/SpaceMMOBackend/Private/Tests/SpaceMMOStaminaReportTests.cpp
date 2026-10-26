#include <limits>

#include "Misc/AutomationTest.h"
#include "SpaceMMOBackendProtocol.h"
#include "SpaceMMOCombatComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Task 101 on the game server: it reads its pool's size from the API, and reports what was spent in whole points
 * without losing a fraction or counting one twice.
 *
 * <strong>The JSON is the API's own</strong>, captured on 25 October from <c>CombatEndpointTests</c>, which writes
 * each answer to its test output: 250 spent at one XP a point, level 3, a pool of 101.
 */
namespace SpaceMMOStaminaReportTests
{
	const TCHAR* RealStaminaSpent = TEXT(R"({"characterId":1,"xpAwarded":250,"level":3,"maxStamina":101})");
	const TCHAR* RealStatusWithPool = TEXT(R"({"characterId":1,"health":1000,"maxHealth":1000,"weapon":{"itemInstanceId":1,"itemKey":"laser_rifle","name":"Laser Rifle","skillKey":"laser_rifle","damage":55,"cooldownMs":500,"rangeMetres":150},"secondsSinceCombat":null,"maxStamina":101})");

	/** Captured on 16 October, before task 101: what an older API sends, with no pool in it. */
	const TCHAR* StatusBeforeThePool = TEXT(R"({"characterId":1,"health":1000,"maxHealth":1000,"weapon":null,"secondsSinceCombat":null})");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOStaminaReadsTheApiTest,
	"SpaceMMO.Stamina.Report.ReadsTheApisWords",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOStaminaReadsTheApiTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMOStaminaReportTests;

	// What CombatEndpointTests posts, as System.Text.Json writes it: the same names, in the same case.
	TestEqual(TEXT("The body the API reads"),
		FSpaceMMOBackendProtocol::MakeStaminaSpentBody(1, 250), FString(TEXT(R"({"characterId":1,"spent":250})")));

	FBackendStaminaSpent Spent;

	if (TestTrue(TEXT("The real answer parses"), FSpaceMMOBackendProtocol::ParseStaminaSpent(RealStaminaSpent, Spent)))
	{
		TestEqual(TEXT("Whose"), Spent.CharacterId, 1);
		TestEqual(TEXT("XP"), Spent.XpAwarded, int64(250));
		TestEqual(TEXT("Level"), Spent.Level, 3);
		TestEqual(TEXT("The pool it leaves"), Spent.MaxStamina, 101);
	}

	FBackendStaminaSpent NotOne;
	TestFalse(TEXT("A body that is not a report"), FSpaceMMOBackendProtocol::ParseStaminaSpent(TEXT(R"({"error":"x"})"), NotOne));

	FBackendCombatStatus WithPool;

	if (TestTrue(TEXT("The real status parses"), FSpaceMMOBackendProtocol::ParseCombatStatus(RealStatusWithPool, WithPool)))
	{
		TestEqual(TEXT("Its pool"), WithPool.MaxStamina, 101);
	}

	FBackendCombatStatus Older;

	if (TestTrue(TEXT("An older API's status still parses"), FSpaceMMOBackendProtocol::ParseCombatStatus(StatusBeforeThePool, Older)))
	{
		TestEqual(TEXT("No pool said: 0, which the pawn ignores"), Older.MaxStamina, 0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOStaminaWholePointsTest,
	"SpaceMMO.Stamina.Report.WholePointsAndTheFractionWaits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOStaminaWholePointsTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Nothing spent"), USpaceMMOCombatComponent::WholeStaminaToReport(0.0, 0.0), 0);
	TestEqual(TEXT("Under a point waits"), USpaceMMOCombatComponent::WholeStaminaToReport(0.9, 0.0), 0);
	TestEqual(TEXT("The whole points"), USpaceMMOCombatComponent::WholeStaminaToReport(137.6, 0.0), 137);

	// The next report, from the same total: the 0.6 left over and what came since, never the 137 again.
	TestEqual(TEXT("Only what is new"), USpaceMMOCombatComponent::WholeStaminaToReport(140.2, 137.0), 3);
	TestEqual(TEXT("Nothing new"), USpaceMMOCombatComponent::WholeStaminaToReport(137.6, 137.0), 0);

	// A total behind what was reported -- a report taken back while another was counted -- claims nothing.
	TestEqual(TEXT("Behind"), USpaceMMOCombatComponent::WholeStaminaToReport(100.0, 137.0), 0);

	// The API refuses more than its limit in one report; the rest waits for the next.
	TestEqual(TEXT("Held to the API's limit"),
		USpaceMMOCombatComponent::WholeStaminaToReport(25000.0, 0.0), USpaceMMOCombatComponent::MostStaminaInOneReport);
	TestEqual(TEXT("A total that is not a number claims nothing"),
		USpaceMMOCombatComponent::WholeStaminaToReport(std::numeric_limits<double>::quiet_NaN(), 0.0), 0);

	return true;
}

#endif
