#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/AutomationTest.h"
#include "SpaceMMOBackendProtocol.h"
#include "SpaceMMOCharacterPawn.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * Task 182: every character draws their race's body.
 *
 * The races are not written by hand: this is the API's own GET /world/races, the capture
 * SpaceMMOMenusTests uses (1 October), because the body table is keyed by the number the server
 * sends and a hand-built list would agree with the table by construction.
 */
namespace SpaceMMOCharacterBodyTests
{
	const TCHAR* const Races =
		TEXT("[{\"race\":0,\"name\":\"Humanoid\",\"faction\":0,\"factionName\":\"Humanity United\",")
		TEXT("\"homeBodyKey\":\"body_terra\",\"homeBodyName\":\"Humanoid\"},")
		TEXT("{\"race\":1,\"name\":\"Martian\",\"faction\":0,\"factionName\":\"Humanity United\",")
		TEXT("\"homeBodyKey\":\"body_ares\",\"homeBodyName\":\"Martian\"},")
		TEXT("{\"race\":2,\"name\":\"Space Elf\",\"faction\":1,\"factionName\":\"Tusk and Thorn\",")
		TEXT("\"homeBodyKey\":\"body_verdance\",\"homeBodyName\":\"SpaceElf\"},")
		TEXT("{\"race\":3,\"name\":\"Space Orc\",\"faction\":1,\"factionName\":\"Tusk and Thorn\",")
		TEXT("\"homeBodyKey\":\"body_grimhold\",\"homeBodyName\":\"SpaceOrc\"}]");

	/** The skeleton ABP_Human and every library clip play on (175). */
	const TCHAR* const Mannequin =
		TEXT("/Game/FreeAnimationLibrary/Demo/Characters/Mannequins/Meshes/SK_Mannequin.SK_Mannequin");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOEveryRaceHasABodyTest,
	"SpaceMMO.Character.EveryRaceTheServerSendsHasABody",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOEveryRaceHasABodyTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMOCharacterBodyTests;

	TArray<FBackendRace> Sent;

	TestTrue(TEXT("The captured races parse"), FSpaceMMOBackendProtocol::ParseRaces(Races, Sent));
	TestEqual(TEXT("Every race the client knows"), Sent.Num(), static_cast<int32>(EBackendRace::SpaceOrc) + 1);

	const TArray<FSpaceMMOCharacterBody>& Bodies = GetDefault<ASpaceMMOCharacterPawn>()->GetRaceBodies();

	for (const FBackendRace& Race : Sent)
	{
		const FSpaceMMOCharacterBody* const Body =
			ASpaceMMOCharacterPawn::FindBody(Bodies, static_cast<int32>(Race.Race));

		if (!TestNotNull(FString::Printf(TEXT("%s has a body in DefaultGame.ini"), *Race.Name), Body))
		{
			continue;
		}

		const USkeletalMesh* const Mesh = Cast<USkeletalMesh>(Body->Mesh.TryLoad());

		if (!TestNotNull(FString::Printf(TEXT("%s's body %s loads"), *Race.Name, *Body->Mesh.ToString()), Mesh))
		{
			continue;
		}

		// On the shared skeleton, or ABP_Human has nothing to play on it and it stands in its bind pose.
		const USkeleton* const Skeleton = Mesh->GetSkeleton();

		TestEqual(FString::Printf(TEXT("%s's body is on SK_Mannequin"), *Race.Name),
			Skeleton != nullptr ? Skeleton->GetPathName() : FString(), FString(Mannequin));

		// Each is drawn at its own height, so config's height must be the one the body was made at, read
		// the way the pawn reads it. Otherwise the pawn quietly stretches it to whatever config says.
		const double Authored = Mesh->GetBounds().BoxExtent.Z * 2.0;

		TestTrue(FString::Printf(TEXT("%s's body measures %.1f cm and config draws it at %.1f"),
				*Race.Name, Authored, Body->HeightCentimetres),
			FMath::Abs(Authored - Body->HeightCentimetres) < 1.0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMORaceWithoutABodyTest,
	"SpaceMMO.Character.ARaceWithoutABodyDrawsTheDefault",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMORaceWithoutABodyTest::RunTest(const FString& Parameters)
{
	const TArray<FSpaceMMOCharacterBody>& Bodies = GetDefault<ASpaceMMOCharacterPawn>()->GetRaceBodies();

	// Null draws CharacterMesh: before the owning client has said, and for a race config does not know.
	TestNull(TEXT("No race yet"), ASpaceMMOCharacterPawn::FindBody(Bodies, INDEX_NONE));
	TestNull(TEXT("A race nobody configured"), ASpaceMMOCharacterPawn::FindBody(Bodies, 99));

	// By race, not by position: config lists them in any order.
	TArray<FSpaceMMOCharacterBody> OutOfOrder;
	FSpaceMMOCharacterBody Orc;
	Orc.Race = 3;
	Orc.HeightCentimetres = 235.0;
	FSpaceMMOCharacterBody MartianBody;
	MartianBody.Race = 1;
	MartianBody.HeightCentimetres = 215.0;
	OutOfOrder.Add(Orc);
	OutOfOrder.Add(MartianBody);

	const FSpaceMMOCharacterBody* const Martian = ASpaceMMOCharacterPawn::FindBody(OutOfOrder, 1);

	TestTrue(TEXT("Race 1 finds race 1's body, wherever it is listed"),
		Martian != nullptr && Martian->HeightCentimetres == 215.0);

	return true;
}

#endif
