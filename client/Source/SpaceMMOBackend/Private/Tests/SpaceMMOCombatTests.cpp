#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "SpaceMMOBackendProtocol.h"
#include "SpaceMMOCombatComponent.h"
#include "SpaceMMOCombatHud.h"
#include "SpaceMMOShot.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Task 194 on the client: the server's words read right, health comes back by the API's rule, a shot meets the
 * person it should, and the HUD's glow follows Joe's rule.
 *
 * <strong>The JSON is the API's own</strong>, captured on 16 October from the endpoint tests: a status, a hit that
 * landed (a head shot from behind, 99), and one refused. Built by hand, they would only show the client agreeing
 * with itself.
 */
namespace SpaceMMOCombatTests
{
	const TCHAR* RealStatus = TEXT(R"({"characterId":1,"health":1000,"maxHealth":1000,"weapon":{"itemInstanceId":1,"itemKey":"laser_rifle","name":"Laser Rifle","skillKey":"laser_rifle","damage":55,"cooldownMs":500,"rangeMetres":150},"secondsSinceCombat":null})");
	const TCHAR* RealHit = TEXT(R"({"landed":true,"refusal":null,"damage":99,"targetHealth":901,"targetMaxHealth":1000,"killed":false,"weaponSkillKey":"laser_rifle","weaponXp":12})");
	const TCHAR* RealRefusal = TEXT(R"({"landed":false,"refusal":"not_allowed","damage":0,"targetHealth":0,"targetMaxHealth":0,"killed":false,"weaponSkillKey":null,"weaponXp":0})");

	/** The status above with an empty Weapon 1 and a fight 4.5 s ago, as the same serializer writes them. */
	const TCHAR* StatusAfterAFight = TEXT(R"({"characterId":1,"health":640,"maxHealth":1000,"weapon":null,"secondsSinceCombat":4.5})");

	/** Somebody standing at a point on flat ground, facing +X. */
	FSpaceMMOShotTarget Standing(const int32 Index, const FVector& FeetMetres, const FVector& Facing = FVector::ForwardVector)
	{
		FSpaceMMOShotTarget Target;
		Target.Index = Index;
		Target.FeetKilometres = FeetMetres / 1000.0;
		Target.Up = FVector::UpVector;
		Target.Forward = Facing;
		Target.RadiusMetres = 0.34;
		Target.HeightMetres = 1.8;

		return Target;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOCombatReadsTheServersWordsTest,
	"SpaceMMO.Combat.ReadsTheServersWords",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOCombatReadsTheServersWordsTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMOCombatTests;

	FBackendCombatStatus Status;

	if (TestTrue(TEXT("The real status parses"), FSpaceMMOBackendProtocol::ParseCombatStatus(RealStatus, Status)))
	{
		TestEqual(TEXT("Health"), Status.Health, 1000);
		TestEqual(TEXT("Maximum"), Status.MaxHealth, 1000);
		TestTrue(TEXT("A weapon in hand"), Status.bHasWeapon);
		TestEqual(TEXT("Its cooldown"), Status.Weapon.CooldownMs, 500);
		TestEqual(TEXT("Its range"), Status.Weapon.RangeMetres, 150);
		TestEqual(TEXT("Its name"), Status.Weapon.Name, FString(TEXT("Laser Rifle")));
		TestTrue(TEXT("Never in a fight reads as negative"), Status.SecondsSinceCombat < 0.0);
	}

	FBackendCombatStatus Hurt;

	if (TestTrue(TEXT("A hurt status parses"), FSpaceMMOBackendProtocol::ParseCombatStatus(StatusAfterAFight, Hurt)))
	{
		TestFalse(TEXT("A null weapon is no weapon, not a weapon of zeroes"), Hurt.bHasWeapon);
		TestEqual(TEXT("Seconds since the fight"), Hurt.SecondsSinceCombat, 4.5);
	}

	FBackendHitResult Hit;

	if (TestTrue(TEXT("The real hit parses"), FSpaceMMOBackendProtocol::ParseHitResult(RealHit, Hit)))
	{
		TestTrue(TEXT("Landed"), Hit.bLanded);
		TestEqual(TEXT("99, a head shot from behind"), Hit.Damage, 99);
		TestEqual(TEXT("Their health after"), Hit.TargetHealth, 901);
		TestEqual(TEXT("XP"), Hit.WeaponXp, int64(12));
		TestTrue(TEXT("No refusal"), Hit.Refusal.IsEmpty());
	}

	FBackendHitResult Refused;

	if (TestTrue(TEXT("The real refusal parses"), FSpaceMMOBackendProtocol::ParseHitResult(RealRefusal, Refused)))
	{
		TestFalse(TEXT("Not landed"), Refused.bLanded);
		TestEqual(TEXT("And why"), Refused.Refusal, FString(TEXT("not_allowed")));
	}

	// And what the server sends: the zone and facing as the numbers HitZone and HitFacing pin, head 0 and back 2.
	TSharedPtr<FJsonObject> Body;
	FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FSpaceMMOBackendProtocol::MakeHitBody(
		7, 9, EBackendHitZone::Head, EBackendHitFacing::Back, FVector(-200.123456789, 0.5, 12.0))), Body);

	if (TestTrue(TEXT("The hit body parses"), Body.IsValid()))
	{
		TestEqual(TEXT("Attacker"), static_cast<int32>(Body->GetNumberField(TEXT("attackerCharacterId"))), 7);
		TestEqual(TEXT("Target"), static_cast<int32>(Body->GetNumberField(TEXT("targetCharacterId"))), 9);
		TestEqual(TEXT("Head is 0"), static_cast<int32>(Body->GetNumberField(TEXT("zone"))), 0);
		TestEqual(TEXT("Back is 2"), static_cast<int32>(Body->GetNumberField(TEXT("facing"))), 2);
		TestEqual(TEXT("Position kept to the metre and beyond"), Body->GetNumberField(TEXT("x")), -200.123456789, 1e-9);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOCombatHealthComesBackByTheServersRuleTest,
	"SpaceMMO.Combat.HealthComesBackByTheServersRule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOCombatHealthComesBackByTheServersRuleTest::RunTest(const FString& Parameters)
{
	// The same table as the API's CombatHealthTests: drawn here from the replicated value and the server's clock,
	// and wrong here means a bar that fills faster or slower than the health it shows.
	using Combat = USpaceMMOCombatComponent;

	TestEqual(TEXT("Nothing for 9 s"), Combat::HealthAt(400, 1000, 100.0, 109.0), 400);
	TestEqual(TEXT("Nothing at 10 s"), Combat::HealthAt(400, 1000, 100.0, 110.0), 400);
	TestEqual(TEXT("5% a second after"), Combat::HealthAt(400, 1000, 100.0, 114.0), 600);
	TestEqual(TEXT("To the millisecond"), Combat::HealthAt(400, 1000, 100.0, 110.5), 425);
	TestEqual(TEXT("Up to the maximum"), Combat::HealthAt(400, 1000, 100.0, 160.0), 1000);
	TestEqual(TEXT("The dead stay dead"), Combat::HealthAt(0, 1000, 100.0, 1000.0), 0);
	TestEqual(TEXT("Never in a fight"), Combat::HealthAt(1000, 1000, -1.0, 50.0), 1000);
	TestEqual(TEXT("A fallen maximum caps it"), Combat::HealthAt(1500, 1000, -1.0, 50.0), 1000);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOCombatAShotMeetsWhoItShouldTest,
	"SpaceMMO.Combat.AShotMeetsWhoItShould",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOCombatAShotMeetsWhoItShouldTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMOCombatTests;

	// Fired along +X from the origin at chest height, 1.35 m, at somebody 20 m away facing us.
	const FVector Muzzle = FVector(0.0, 0.0, 1.35) / 1000.0;

	TArray<FSpaceMMOShotTarget> Targets = {Standing(0, FVector(20.0, 0.0, 0.0), -FVector::ForwardVector)};

	FSpaceMMOShotHit Hit;

	if (TestTrue(TEXT("Straight at them"), SpaceMMO::Shot::Resolve(Muzzle, FVector::ForwardVector, 150.0, Targets, Hit)))
	{
		TestEqual(TEXT("Met at the front of their body"), Hit.DistanceMetres, 20.0 - 0.34, 0.01);
		TestEqual(TEXT("Chest height is the body"), Hit.Zone, EBackendHitZone::Body);
		TestEqual(TEXT("They face us: the front"), Hit.Facing, EBackendHitFacing::Front);
	}

	// Half a metre to the side passes them: wider than a body, which is 0.34 m either side of its axis.
	TestFalse(TEXT("Wide misses"),
		SpaceMMO::Shot::Resolve(Muzzle, FVector(20.0, 0.5, 0.0), 150.0, Targets, Hit));

	// Out of range is a miss, however true the aim.
	TestFalse(TEXT("Beyond range"), SpaceMMO::Shot::Resolve(Muzzle, FVector::ForwardVector, 15.0, Targets, Hit));

	// Behind the muzzle is never hit.
	TestFalse(TEXT("Behind"), SpaceMMO::Shot::Resolve(Muzzle, -FVector::ForwardVector, 150.0, Targets, Hit));

	// Aimed at their head: 1.7 m of a 1.8 m body.
	const FVector AtHead = FVector(20.0, 0.0, 1.7 - 1.35);

	if (TestTrue(TEXT("At the head"), SpaceMMO::Shot::Resolve(Muzzle, AtHead, 150.0, Targets, Hit)))
	{
		TestEqual(TEXT("The head"), Hit.Zone, EBackendHitZone::Head);
	}

	// At their knees.
	if (TestTrue(TEXT("At the legs"), SpaceMMO::Shot::Resolve(Muzzle, FVector(20.0, 0.0, 0.5 - 1.35), 150.0, Targets, Hit)))
	{
		TestEqual(TEXT("The legs"), Hit.Zone, EBackendHitZone::Legs);
	}

	// Two in a line: the nearer is hit, whichever order they are listed in.
	Targets = {Standing(0, FVector(40.0, 0.0, 0.0)), Standing(1, FVector(25.0, 0.0, 0.0))};

	if (TestTrue(TEXT("Two in a line"), SpaceMMO::Shot::Resolve(Muzzle, FVector::ForwardVector, 150.0, Targets, Hit)))
	{
		TestEqual(TEXT("The nearer"), Hit.Index, 1);

		// They face +X, away from us: shot in the back.
		TestEqual(TEXT("From behind"), Hit.Facing, EBackendHitFacing::Back);
	}

	// Facing across the shot: the side.
	TestEqual(TEXT("Across"), SpaceMMO::Shot::FacingOf(FVector::RightVector, FVector::UpVector, FVector::ForwardVector),
		EBackendHitFacing::Side);

	// A shot from a hill above still came from in front of them: facing is along the ground.
	TestEqual(TEXT("From above, in front"),
		SpaceMMO::Shot::FacingOf(-FVector::ForwardVector, FVector::UpVector, FVector(1.0, 0.0, -0.8)),
		EBackendHitFacing::Front);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOCombatHealthBarsGlowAsHealthFallsTest,
	"SpaceMMO.Combat.HealthBarsGlowAsHealthFalls",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOCombatHealthBarsGlowAsHealthFallsTest::RunTest(const FString& Parameters)
{
	// Joe, 16 October: red always, glowing harder when low.
	TestEqual(TEXT("Faint at full"), USpaceMMOCombatHud::GlowStrength(1.0f), 0.15f);
	TestEqual(TEXT("Faint at half"), USpaceMMOCombatHud::GlowStrength(0.5f), 0.15f);
	TestEqual(TEXT("Harder at a quarter"), USpaceMMOCombatHud::GlowStrength(0.25f), 0.575f, 0.001f);
	TestEqual(TEXT("Full at nothing"), USpaceMMOCombatHud::GlowStrength(0.0f), 1.0f);

	TestEqual(TEXT("Steady above half"), USpaceMMOCombatHud::PulseRate(0.6f), 0.0f);
	TestTrue(TEXT("Pulsing below it"), USpaceMMOCombatHud::PulseRate(0.25f) > 0.0f);
	TestTrue(TEXT("Faster the lower"), USpaceMMOCombatHud::PulseRate(0.05f) > USpaceMMOCombatHud::PulseRate(0.4f));

	TestEqual(TEXT("The number"), USpaceMMOCombatHud::HealthLine(1640, 2000), FString(TEXT("1,640 / 2,000")));
	TestEqual(TEXT("Who killed you"), USpaceMMOCombatHud::DeathTitle(TEXT("Orenthal")), FString(TEXT("Killed by Orenthal")));
	TestEqual(TEXT("Nobody named"), USpaceMMOCombatHud::DeathTitle(FString()), FString(TEXT("Killed")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOBodyFallsAwayFromTheShotTest,
	"SpaceMMO.Combat.ABodyFallsAwayFromTheShotThatKilledIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOBodyFallsAwayFromTheShotTest::RunTest(const FString& Parameters)
{
	// Task 201. A target facing +X, so its right is +Y, shot by somebody somewhere around it.
	const FVector Right = FVector::RightVector;

	TestEqual(TEXT("Shot from the front: backward"),
		USpaceMMOCombatComponent::FallFrom(EBackendHitFacing::Front, FVector(-1, 0, 0), Right), ESpaceMMOFall::Backward);
	TestEqual(TEXT("Shot from behind: forward"),
		USpaceMMOCombatComponent::FallFrom(EBackendHitFacing::Back, FVector(1, 0, 0), Right), ESpaceMMOFall::Forward);

	// From their left the shot travels toward +Y, their right, and pushes them that way.
	TestEqual(TEXT("Shot from their left: to the right"),
		USpaceMMOCombatComponent::FallFrom(EBackendHitFacing::Side, FVector(0, 1, 0), Right), ESpaceMMOFall::ToRight);
	TestEqual(TEXT("Shot from their right: to the left"),
		USpaceMMOCombatComponent::FallFrom(EBackendHitFacing::Side, FVector(0.1, -1, 0).GetSafeNormal(), Right),
		ESpaceMMOFall::ToLeft);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOStaminaBarSaysWhatIsThereTest,
	"SpaceMMO.Combat.TheStaminaBarSaysWhatIsThereAndFlashesOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOStaminaBarSaysWhatIsThereTest::RunTest(const FString& Parameters)
{
	// Task 195. Rounded down: 24.9 is not enough for a 25-stamina dodge, and must not read as 25.
	TestEqual(TEXT("72 of 100"), USpaceMMOCombatHud::StaminaLine(72.0, 100.0), FString(TEXT("72 / 100")));
	TestEqual(TEXT("24.9 reads 24"), USpaceMMOCombatHud::StaminaLine(24.9, 100.0), FString(TEXT("24 / 100")));
	TestEqual(TEXT("A level-99 pool"), USpaceMMOCombatHud::StaminaLine(150.0, 150.0), FString(TEXT("150 / 150")));

	// A refused dodge flashes the bar once, and it fades back to amber within a third of a second (Joe, 19 October).
	const FLinearColor Amber = USpaceMMOCombatHud::StaminaColour(-1.0);
	const FLinearColor Flash = USpaceMMOCombatHud::StaminaColour(0.0);

	TestTrue(TEXT("The flash is brighter than amber"), Flash.GetLuminance() > Amber.GetLuminance() + 0.1f);
	TestTrue(TEXT("Fading at 0.2 s"), USpaceMMOCombatHud::StaminaColour(0.2).GetLuminance() < Flash.GetLuminance()
		&& USpaceMMOCombatHud::StaminaColour(0.2).GetLuminance() > Amber.GetLuminance());
	TestTrue(TEXT("Amber again by 0.4 s"), USpaceMMOCombatHud::StaminaColour(0.4).Equals(Amber));

	return true;
}

#endif
