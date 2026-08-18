#include "Misc/AutomationTest.h"
#include "SpaceMMOLoginScreen.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FBackendFailure Failure(const int32 Status, const TCHAR* Message = TEXT(""))
	{
		FBackendFailure Result;
		Result.HttpStatus = Status;
		Result.Message = Message;

		return Result;
	}
}

/** Task 107. What the sign-in screen says when it does not work. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOLoginSaysWhichThingWentWrongTest,
	"SpaceMMO.HUD.LoginSaysWhichThingWentWrong",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOLoginSaysWhichThingWentWrongTest::RunTest(const FString& Parameters)
{
	// The one status with exactly one meaning here, and "Unauthorized" is not how to say it to
	// somebody who has just typed a password.
	TestEqual(
		TEXT("A refused sign-in"),
		USpaceMMOLoginScreen::DescribeFailure(Failure(401, TEXT("Unauthorized"))),
		FString(TEXT("Wrong email or password.")));

	// Nothing at all: no status, no body. That is a backend that is not running, and without a
	// message it is a blank line under the password box -- which sends somebody to check the one
	// thing that is not wrong. On this project an API refusing to start over an unapplied migration
	// has already once read as "cannot identify my character".
	TestEqual(
		TEXT("An unreachable server"),
		USpaceMMOLoginScreen::DescribeFailure(Failure(0)),
		FString(TEXT("Could not reach the server.")));

	// Anything else is the server's own words. It knows something this screen does not.
	TestEqual(
		TEXT("Everything else verbatim"),
		USpaceMMOLoginScreen::DescribeFailure(Failure(500, TEXT("Migrations pending: AddShipHolds"))),
		FString(TEXT("Migrations pending: AddShipHolds")));

	// And a refusal is never confused with a success. Empty means "nothing went wrong", which is
	// what the screen clears to, so no failure may ever produce it.
	TestFalse(
		TEXT("A failure is never silent"),
		USpaceMMOLoginScreen::DescribeFailure(Failure(503)).IsEmpty());

	return true;
}

#endif
