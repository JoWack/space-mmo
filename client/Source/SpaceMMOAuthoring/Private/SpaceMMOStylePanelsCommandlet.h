#pragma once

#include "Commandlets/Commandlet.h"
#include "CoreMinimal.h"

#include "SpaceMMOStylePanelsCommandlet.generated.h"

/**
 * Brings the game's panels into the menus' style, by editing their Widget Blueprints in place (task 173).
 *
 *   UnrealEditor-Cmd.exe client/SpaceMMO.uproject -run=SpaceMMOAuthoring.SpaceMMOStylePanels
 *
 * <strong>In place, not rebuilt.</strong> These Blueprints are Joe's, made in the designer, and their
 * graphs do real work -- the inventory's move prompt and the station's order prompt live there -- so
 * unlike the menus they are restyled rather than regenerated. What this changes: fonts, colours, the
 * glass frame and its corner brackets, buttons, fields, the market's column headings (Joe's wording,
 * 2 October), the station's tab strip, the skill row's layout, and a light dim over the world. What it
 * removes: only the colour bindings the rows now set themselves (USpaceMMOPanelRow), with their functions.
 * Every graph with logic in it is left exactly as it was.
 *
 * <strong>Safe to run again.</strong> Each structural change looks for what it adds before adding it, and
 * the styling is the same every time.
 *
 * <strong>It checks what it did.</strong> Every part a Blueprint's C++ class binds that was in the tree
 * before must still be there after, each Blueprint must compile, and the parts the new styling needs
 * must exist; a Blueprint that fails any of these is not saved.
 *
 * Same run rules as the other commandlets here: the module prefix is required, run it from PowerShell or
 * cmd rather than Git Bash, and close the editor first.
 */
UCLASS()
class USpaceMMOStylePanelsCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	USpaceMMOStylePanelsCommandlet();

	virtual int32 Main(const FString& Params) override;
};
