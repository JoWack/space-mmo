#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PhysicsEngine/BodySetup.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "SpaceMMODepositSettings.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The meshes deposits are drawn with: one for every material content puts in the ground, and each one
 * solid and dressed.
 *
 * <strong>Task 189.</strong> Oxide salt, rootstone and blackiron ore were authored with deposits on
 * 9 October (184) and drew as the engine cylinder until they were modelled, because a missing entry in
 * DefaultGame.ini falls back to the placeholder on purpose -- an invisible deposit would be worse -- and
 * nothing said the entry was missing. And a mesh with no simple collision is one a character walks
 * straight through however solid it looks, which is how the first two arrived (130).
 *
 * Read from the shipped content and the shipped config, not from fixtures: the claim is about what the
 * game draws.
 */

namespace SpaceMMODepositMeshTests
{
	bool ReadAuthoredUniverse(TSharedPtr<FJsonObject>& OutRoot, FString& OutWhere)
	{
		OutWhere = FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("data"), TEXT("universe"),
				TEXT("origin.json")));

		FString Text;

		if (!FFileHelper::LoadFileToString(Text, *OutWhere))
		{
			return false;
		}

		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);

		return FJsonSerializer::Deserialize(Reader, OutRoot) && OutRoot.IsValid();
	}

	/**
	 * Materials that still draw as the placeholder cylinder, knowingly.
	 *
	 * <strong>Meant to shrink to nothing.</strong> The four level-15 planet-locked materials were never
	 * modelled: this test found them on its first run, 10 October, beside the three it was written for.
	 * They were outside what task 189 was asked to make, and the credits had run out, so they are task 190.
	 * Written down here, in a diff, so the next material left on the cylinder has to be added on purpose,
	 * and the test fails when one of these gets a mesh and is not taken off.
	 */
	const TSet<FString> KnownPlaceholders = {
		TEXT("terran_ferrite"), TEXT("ares_regolith"), TEXT("verdant_amber"), TEXT("grimhold_slag"),
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMODepositEveryDepositedItemHasAMeshTest,
	"SpaceMMO.Deposit.EveryDepositedItemHasAMesh",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMODepositEveryDepositedItemHasAMeshTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FJsonObject> Universe;
	FString Where;

	if (!SpaceMMODepositMeshTests::ReadAuthoredUniverse(Universe, Where))
	{
		AddError(FString::Printf(TEXT("Could not read the authored universe from %s"), *Where));

		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;

	if (!Universe->TryGetArrayField(TEXT("resourceNodes"), Nodes) || Nodes == nullptr)
	{
		AddError(TEXT("origin.json has no resourceNodes"));

		return false;
	}

	TSet<FString> Items;

	for (const TSharedPtr<FJsonValue>& Node : *Nodes)
	{
		FString Item;

		if (Node.IsValid() && Node->AsObject().IsValid() && Node->AsObject()->TryGetStringField(TEXT("item"), Item))
		{
			Items.Add(Item);
		}
	}

	// Not vacuous: the file does place deposits.
	TestTrue(TEXT("Content places deposits"), Items.Num() > 0);

	const USpaceMMODepositSettings* Settings = GetDefault<USpaceMMODepositSettings>();

	for (const FString& Item : Items)
	{
		const TSoftObjectPtr<UStaticMesh>* Mesh = Settings->Meshes.Find(Item);

		if (SpaceMMODepositMeshTests::KnownPlaceholders.Contains(Item))
		{
			TestTrue(
				FString::Printf(
					TEXT("%s has a mesh now; take it off KnownPlaceholders so the list keeps meaning "
						"something"),
					*Item),
				Mesh == nullptr || Mesh->IsNull());

			continue;
		}

		TestTrue(
			FString::Printf(
				TEXT("%s has deposits in origin.json and no mesh under Meshes in DefaultGame.ini, so it "
					"draws as the placeholder cylinder"),
				*Item),
			Mesh != nullptr && !Mesh->IsNull());
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMODepositEveryMeshIsSolidAndDressedTest,
	"SpaceMMO.Deposit.EveryMeshIsSolidAndDressed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMODepositEveryMeshIsSolidAndDressedTest::RunTest(const FString& Parameters)
{
	const USpaceMMODepositSettings* Settings = GetDefault<USpaceMMODepositSettings>();

	TestTrue(TEXT("Some deposit meshes are configured"), Settings->Meshes.Num() > 0);

	const UMaterialInterface* const EngineDefault = UMaterial::GetDefaultMaterial(MD_Surface);

	for (const TPair<FString, TSoftObjectPtr<UStaticMesh>>& Entry : Settings->Meshes)
	{
		const UStaticMesh* Mesh = Entry.Value.LoadSynchronous();

		if (!TestNotNull(
				FString::Printf(TEXT("%s's mesh %s loads"), *Entry.Key, *Entry.Value.ToString()), Mesh))
		{
			continue;
		}

		// Simple collision, which is all a character's sweep reads (130): complex geometry is invisible
		// to it, however solid the deposit looks.
		const UBodySetup* Body = Mesh->GetBodySetup();

		TestTrue(
			FString::Printf(TEXT("%s's mesh has simple collision, or a character walks through it"), *Entry.Key),
			Body != nullptr && Body->AggGeom.GetElementCount() > 0);

		// Wearing something other than the engine's checker, which is what an import that lost its
		// material assignment leaves.
		const UMaterialInterface* Material = Mesh->GetMaterial(0);

		TestTrue(
			FString::Printf(TEXT("%s's mesh wears its own material"), *Entry.Key),
			Material != nullptr && Material != EngineDefault);
	}

	return true;
}

#endif
