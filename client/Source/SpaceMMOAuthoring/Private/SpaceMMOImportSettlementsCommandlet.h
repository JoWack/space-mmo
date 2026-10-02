#pragma once

#include "Commandlets/Commandlet.h"
#include "CoreMinimal.h"

#include "SpaceMMOImportSettlementsCommandlet.generated.h"

/**
 * Turns a settlement's FBX and manifest into Unreal assets: Borlash, on the Capital (task 168).
 *
 *   UnrealEditor-Cmd.exe client/SpaceMMO.uproject -run=SpaceMMOAuthoring.SpaceMMOImportSettlements
 *
 * Run it from PowerShell or cmd with the editor closed, for the reasons USpaceMMOBuildMenusCommandlet
 * gives: the module prefix is needed because this module loads late, and Git Bash rewrites the path
 * arguments. It takes no arguments of its own -- the paths are the repository's, and this machine
 * mangles dotted command-line values (docs/setup.md).
 *
 * <strong>Everything it writes is generated, and regenerated every run.</strong> The meshes are
 * reimported, the material instances re-set from the manifest, and BP_Station_Borlash rebuilt from
 * scratch, because all of it is derived from tools/greybox/a07_borlash_city.py and a hand edit would
 * be a number that no longer traces to the script. Dress the city in a child Blueprint and point
 * BlueprintsByKey at that.
 *
 * <strong>It measures what it imported</strong> rather than trusting the import: every mesh's hull
 * count against the manifest, every mesh's bounds against the manifest's after the axis conversion --
 * which is also what proves north came out as -Y -- and the Blueprint's berths and service points
 * against the manifest's lists. Any disagreement fails the run.
 */
UCLASS()
class USpaceMMOImportSettlementsCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	USpaceMMOImportSettlementsCommandlet();

	virtual int32 Main(const FString& Params) override;
};
