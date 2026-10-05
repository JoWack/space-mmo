#pragma once

#include "Commandlets/Commandlet.h"
#include "CoreMinimal.h"

#include "SpaceMMOImportClipsCommandlet.generated.h"

/**
 * Brings animation clips made in Blender onto the skeleton every character shares, and points the
 * animation blueprint at them (tasks 175 and 176). The first is the Humanoid man's jump start.
 *
 *   UnrealEditor-Cmd.exe client/SpaceMMO.uproject "-run=SpaceMMOAuthoring.SpaceMMOImportClips"
 *
 * Run it from PowerShell or cmd with the editor closed, for the reasons USpaceMMOBuildMenusCommandlet
 * gives: the module prefix is needed because this module loads late, Git Bash rewrites the path
 * arguments, and PowerShell splits the unquoted argument at its dot. It takes no arguments of its own --
 * the clips are a table in the .cpp, because this machine mangles dotted command-line values
 * (docs/setup.md).
 *
 * <strong>Every clip gets Force Root Lock and no root motion.</strong> The server owns where a character
 * is, and task 125 lost four attempts to root motion reaching the pose before Force Root Lock stopped it.
 *
 * <strong>It measures what it imported</strong> rather than trusting the import: the frame count and
 * rate, that the root never moves, the pelvis's height against what Blender exported -- which is what
 * catches a clip arriving at a hundredth or a hundred times its size -- and, in the blueprint, that
 * nothing still plays the clip this one replaces. Any disagreement fails the run.
 */
UCLASS()
class USpaceMMOImportClipsCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	USpaceMMOImportClipsCommandlet();

	virtual int32 Main(const FString& Params) override;
};
