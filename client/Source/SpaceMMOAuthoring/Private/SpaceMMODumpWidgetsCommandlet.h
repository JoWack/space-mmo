#pragma once

#include "Commandlets/Commandlet.h"
#include "CoreMinimal.h"

#include "SpaceMMODumpWidgetsCommandlet.generated.h"

/**
 * Prints what is inside the game's Widget Blueprints: every widget in each tree, with its slot and the
 * properties a restyle would touch, and every node in each graph (task 173).
 *
 *   UnrealEditor-Cmd.exe client/SpaceMMO.uproject -run=SpaceMMOAuthoring.SpaceMMODumpWidgets [-Path=/Game/UI]
 *
 * <strong>Why it exists.</strong> A Widget Blueprint cannot be read from outside the editor -- the
 * .uasset is compressed (task 104) -- and the panels' Blueprints are Joe's, authored in the designer,
 * with logic in their graphs: the inventory's quantity prompt and the station's order prompt live there.
 * Restyling them without knowing what is in them is how that logic gets lost. This reads; it never saves.
 *
 * Same run rules as the other commandlets here: the module prefix is required, run it from PowerShell or
 * cmd rather than Git Bash, and close the editor first.
 */
UCLASS()
class USpaceMMODumpWidgetsCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	USpaceMMODumpWidgetsCommandlet();

	virtual int32 Main(const FString& Params) override;
};
