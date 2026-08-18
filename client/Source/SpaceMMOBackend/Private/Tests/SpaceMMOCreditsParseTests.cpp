#include "Misc/AutomationTest.h"
#include "SpaceMMOBackendProtocol.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Reading a typed price.
 *
 * The conversion was a Blueprint's job for exactly one playtest, and the first order placed through
 * it went in at a hundredth of the price intended -- one credit typed, 0.01 cr placed, no error
 * anywhere. These tests exist so that never again depends on remembering to multiply.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOCreditsRoundTripTest,
	"SpaceMMO.HUD.CreditsSurviveTheRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOCreditsRoundTripTest::RunTest(const FString& Parameters)
{
	int64 Parsed = 0;

	// A whole number means whole credits. This is the one that was wrong.
	TestTrue(TEXT("plain number parses"), FSpaceMMOBackendProtocol::ParseCredits(TEXT("1"), Parsed));
	TestEqual(TEXT("one credit is a hundred minor units"), Parsed, 100LL);

	TestTrue(TEXT("decimal parses"), FSpaceMMOBackendProtocol::ParseCredits(TEXT("20.00"), Parsed));
	TestEqual(TEXT("twenty credits"), Parsed, 2000LL);

	// The case a double gets wrong: 20.10 is not exactly representable, and a parse that went
	// through one could land a minor unit either side of it.
	TestTrue(TEXT("tenths parse"), FSpaceMMOBackendProtocol::ParseCredits(TEXT("20.10"), Parsed));
	TestEqual(TEXT("no drift through a float"), Parsed, 2010LL);

	TestTrue(TEXT("one hundredth parses"), FSpaceMMOBackendProtocol::ParseCredits(TEXT("0.01"), Parsed));
	TestEqual(TEXT("the smallest price"), Parsed, 1LL);

	// Whatever a player types around the number, including a figure copied off the screen.
	TestTrue(TEXT("grouped parses"), FSpaceMMOBackendProtocol::ParseCredits(TEXT("1,234.56"), Parsed));
	TestEqual(TEXT("separators ignored"), Parsed, 123456LL);

	TestTrue(TEXT("unit parses"), FSpaceMMOBackendProtocol::ParseCredits(TEXT(" 12.50 cr "), Parsed));
	TestEqual(TEXT("unit and spaces ignored"), Parsed, 1250LL);

	// Anything the market prints must read back as what it printed, or a suggestion button would
	// offer a price the box could not accept.
	for (const int64 MinorUnits : { 1LL, 99LL, 100LL, 2010LL, 123456LL, 100000000LL })
	{
		int64 Back = 0;

		const FString Printed = FSpaceMMOBackendProtocol::FormatCredits(MinorUnits);

		TestTrue(*FString::Printf(TEXT("%s parses"), *Printed),
			FSpaceMMOBackendProtocol::ParseCredits(Printed, Back));

		TestEqual(*FString::Printf(TEXT("%s round-trips"), *Printed), Back, MinorUnits);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOCreditsRefusesNonsenseTest,
	"SpaceMMO.HUD.CreditsRefuseWhatIsNotAPrice",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOCreditsRefusesNonsenseTest::RunTest(const FString& Parameters)
{
	int64 Parsed = 0;

	// Refused rather than read as zero. An order at zero is one the server floors to a hundredth of
	// a credit and places, which is how a typo becomes a sale.
	TestFalse(TEXT("empty"), FSpaceMMOBackendProtocol::ParseCredits(FString(), Parsed));
	TestFalse(TEXT("letters"), FSpaceMMOBackendProtocol::ParseCredits(TEXT("twenty"), Parsed));
	TestFalse(TEXT("trailing junk"), FSpaceMMOBackendProtocol::ParseCredits(TEXT("20x"), Parsed));
	TestFalse(TEXT("two points"), FSpaceMMOBackendProtocol::ParseCredits(TEXT("2.0.0"), Parsed));

	// Finer than the currency goes. Rounding it would charge a price nobody typed.
	TestFalse(TEXT("thousandths"), FSpaceMMOBackendProtocol::ParseCredits(TEXT("1.005"), Parsed));

	return true;
}

#endif
