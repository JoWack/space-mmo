#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "SpaceMMOHudSettings.generated.h"

/**
 * Which Widget Blueprints the HUD is made of.
 *
 * <strong>Config rather than a hard-coded path.</strong> The C++ classes decide what a readout
 * <em>says</em>; the Blueprints decide what it looks like, and which Blueprint is in play is a
 * content decision. Naming them here puts the choice in DefaultGame.ini, where it appears in a diff
 * — the same reasoning as the deposit meshes beside it.
 *
 * Soft, and unset by default. A missing or unset widget means no HUD rather than a failed load: the
 * game must still run for anyone who has not made the asset yet, and for the automated runs, which
 * have no viewport to add one to.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "SpaceMMO HUD"))
class SPACEMMOBACKEND_API USpaceMMOHudSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/**
	 * The always-on flight readout. Must derive from <c>USpaceMMOFlightReadout</c>, which is what
	 * binds its text blocks by name.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "HUD",
		meta = (MetaClass = "/Script/SpaceMMOBackend.SpaceMMOFlightReadout"))
	FSoftClassPath FlightReadout;

	virtual FName GetCategoryName() const override { return TEXT("Game"); }
};
