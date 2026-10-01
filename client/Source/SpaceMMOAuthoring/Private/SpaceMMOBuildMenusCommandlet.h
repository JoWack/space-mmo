#pragma once

#include "Commandlets/Commandlet.h"
#include "CoreMinimal.h"

#include "SpaceMMOBuildMenusCommandlet.generated.h"

/**
 * Builds task 110's six menu Widget Blueprints from code, styled to the agreed look.
 *
 *   UnrealEditor-Cmd.exe client/SpaceMMO.uproject -run=SpaceMMOAuthoring.SpaceMMOBuildMenus [-Force]
 *
 * <strong>The module prefix is required.</strong> This module loads at PostEngineInit, after the
 * engine looks for commandlet classes, so a bare <c>-run=SpaceMMOBuildMenus</c> reports "could not
 * find the class"; <c>Module.Name</c> makes the engine load the module first. And run it from
 * PowerShell or cmd, not Git Bash: Bash rewrites a <c>/Script/...</c> argument into a file path.
 * Close the editor first -- it saves the assets the editor would have open.
 *
 * <strong>Why code rather than the designer.</strong> Joe asked for the Blueprints to be made for him,
 * and the editor's MCP toolsets can name widgets but cannot set a single property on one. Code can,
 * and it is repeatable: the style guide in docs/tasks.md (task 110) is applied the same way to every
 * screen, from one set of colours below.
 *
 * <strong>It never overwrites by default.</strong> Once a Blueprint exists it is Joe's to edit in the
 * designer, and a rerun that silently replaced his edits would be the worst thing this could do. An
 * existing asset is skipped and says so; <c>-Force</c> rebuilds it.
 *
 * <strong>It checks what it built</strong> rather than trusting it: each saved Blueprint is reloaded,
 * and every BindWidgetOptional part its C++ parent declares must be in the tree with a compatible
 * class, and every screen's row class must be set. A part with a typo binds to nothing and the screen
 * shows nothing -- which reads exactly like an empty list.
 *
 * The parent classes are found by path, not linked: this module depends on nothing that talks to the
 * backend (see SpaceMMOAuthoring.Build.cs), and a class name is all a Blueprint needs.
 */
UCLASS()
class USpaceMMOBuildMenusCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	USpaceMMOBuildMenusCommandlet();

	virtual int32 Main(const FString& Params) override;
};
