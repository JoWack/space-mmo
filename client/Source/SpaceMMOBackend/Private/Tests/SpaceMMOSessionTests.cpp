#include "Engine/GameInstance.h"
#include "Misc/AutomationTest.h"
#include "SpaceMMOBackendClient.h"
#include "SpaceMMOPlayerController.h"
#include "SpaceMMOSessionEndedScreen.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Task 199: one character in the world per account, and a window that loses its server never carries on as an
 * offline game of its own.
 *
 * What these cannot reach is the wiring -- the server finding the earlier connection, the kick, the client falling
 * back to the empty map and showing the panel there. That is two clients on one account, by hand.
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOSignInEndsTheAccountsOtherConnectionsTest,
	"SpaceMMO.Session.ASignInEndsEveryOtherConnectionOnItsAccount",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOSignInEndsTheAccountsOtherConnectionsTest::RunTest(const FString& Parameters)
{
	// Per account, Joe's choice on 18 October: Orenthal signing in ends Spelf on the same account, not only a second
	// Orenthal. The id says nothing about the character, which is the point.
	TestTrue(TEXT("The same account"), ASpaceMMOPlayerController::EndedBySignIn(8, 8));
	TestFalse(TEXT("Another account"), ASpaceMMOPlayerController::EndedBySignIn(9, 8));

	// A connection still signing in has no account yet, and is nobody's earlier session.
	TestFalse(TEXT("Not signed in yet"), ASpaceMMOPlayerController::EndedBySignIn(0, 8));
	TestFalse(TEXT("Nobody signing in"), ASpaceMMOPlayerController::EndedBySignIn(0, 0));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOOnlyAClientLosesItsServerTest,
	"SpaceMMO.Session.OnlyAClientsOwnConnectionFailingEndsItsSession",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOOnlyAClientLosesItsServerTest::RunTest(const FString& Parameters)
{
	// The two a client has: playing, and connecting. The 03:19 run of 17 October failed while connecting.
	TestTrue(TEXT("Playing"), USpaceMMOBackendClient::LosesTheServer(false, NAME_GameNetDriver));
	TestTrue(TEXT("Connecting"), USpaceMMOBackendClient::LosesTheServer(false, NAME_PendingNetDriver));

	// A dedicated server hears of every client it loses, and must not end its own session over one.
	TestFalse(TEXT("A server's"), USpaceMMOBackendClient::LosesTheServer(true, NAME_GameNetDriver));

	TestFalse(TEXT("A beacon's"), USpaceMMOBackendClient::LosesTheServer(false, NAME_BeaconNetDriver));
	TestFalse(TEXT("A replay's"), USpaceMMOBackendClient::LosesTheServer(false, NAME_DemoNetDriver));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOFirstReasonHoldsTest,
	"SpaceMMO.Session.TheFirstReasonASessionEndedForHolds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOFirstReasonHoldsTest::RunTest(const FString& Parameters)
{
	// Inside a game instance, which the subsystem's class insists on; never initialised, so nothing is bound or sent.
	UGameInstance* GameInstance = NewObject<UGameInstance>(GetTransientPackage());
	USpaceMMOBackendClient* Client = NewObject<USpaceMMOBackendClient>(GameInstance);

	TestEqual(TEXT("Over only once something ends it"), Client->GetSessionEnd(), ESpaceMMOSessionEnd::None);

	Client->EndSession(ESpaceMMOSessionEnd::None);

	TestEqual(TEXT("None ends nothing"), Client->GetSessionEnd(), ESpaceMMOSessionEnd::None);

	// The server says why, then the connection drops: the drop must not turn "signed in elsewhere" into "lost".
	Client->EndSession(ESpaceMMOSessionEnd::SignedInElsewhere);
	Client->EndSession(ESpaceMMOSessionEnd::LostConnection);

	TestEqual(TEXT("The server's reason, not the drop's"), Client->GetSessionEnd(), ESpaceMMOSessionEnd::SignedInElsewhere);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOSessionEndedWordsTest,
	"SpaceMMO.Session.ThePanelSaysWhatJoeApproved",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOSessionEndedWordsTest::RunTest(const FString& Parameters)
{
	// The mock's words, 18 October. The title's capitals are its style's.
	TestEqual(TEXT("Elsewhere: title"), USpaceMMOSessionEndedScreen::TitleFor(ESpaceMMOSessionEnd::SignedInElsewhere),
		FString(TEXT("Signed in elsewhere")));
	TestEqual(TEXT("Elsewhere: line"), USpaceMMOSessionEndedScreen::NoteFor(ESpaceMMOSessionEnd::SignedInElsewhere),
		FString(TEXT("This account was signed in from another window or computer, so this one has been disconnected.")));

	TestEqual(TEXT("Lost: title"), USpaceMMOSessionEndedScreen::TitleFor(ESpaceMMOSessionEnd::LostConnection),
		FString(TEXT("Disconnected")));
	TestEqual(TEXT("Lost: line"), USpaceMMOSessionEndedScreen::NoteFor(ESpaceMMOSessionEnd::LostConnection),
		FString(TEXT("The connection to the server was lost.")));

	return true;
}

#endif
