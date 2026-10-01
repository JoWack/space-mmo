#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The project stays pinned to a source engine build (task 159).
 *
 * `c565359` pinned `SpaceMMO.uproject` to the source build by GUID and setup.md explains why: a
 * version string such as "5.8" resolves to whichever 5.8 is registered, the Epic launcher registers
 * itself as one, and each engine then finds the other's binaries foreign. `1fe6d1a`, a commit about
 * deposit meshes, set it back to "5.8" without a word -- almost certainly the editor rewriting the
 * file on open and the change being swept into an unrelated commit. Nothing noticed for a month, and
 * it cost a build that reported success and loaded nothing.
 *
 * In the backend module because Json is linked here.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOProjectIsPinnedToASourceBuildTest,
	"SpaceMMO.Project.PinnedToASourceBuild",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOProjectIsPinnedToASourceBuildTest::RunTest(const FString& Parameters)
{
	const FString Where = FPaths::GetProjectFilePath();

	FString Text;

	if (!FFileHelper::LoadFileToString(Text, *Where))
	{
		AddError(FString::Printf(TEXT("Could not read the project file at %s"), *Where));

		return false;
	}

	TSharedPtr<FJsonObject> Project;

	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);

	if (!FJsonSerializer::Deserialize(Reader, Project) || !Project.IsValid())
	{
		AddError(FString::Printf(TEXT("%s is not valid JSON"), *Where));

		return false;
	}

	FString Association;
	Project->TryGetStringField(TEXT("EngineAssociation"), Association);

	// The form, not the value. The GUID is specific to one machine's registration of the source
	// build (setup.md says so), so asserting this machine's would fail every other clone; a version
	// number is wrong everywhere, and that is the regression this exists for.
	FGuid Parsed;

	const bool bIsGuid = Association.StartsWith(TEXT("{"))
		&& Association.EndsWith(TEXT("}"))
		&& FGuid::Parse(Association.Mid(1, Association.Len() - 2), Parsed);

	TestTrue(
		FString::Printf(
			TEXT("EngineAssociation is a source-build GUID, not a version (it is \"%s\"); see "
				"setup.md, \"The project is pinned to the source build by GUID\""),
			*Association),
		bIsGuid);

	return true;
}

#endif
