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

	/**
	 * Name and credits, shown while on foot. Must derive from <c>USpaceMMOOnFootReadout</c>.
	 *
	 * Shares the flight readout's corner; the controller shows exactly one of them.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "HUD",
		meta = (MetaClass = "/Script/SpaceMMOBackend.SpaceMMOOnFootReadout"))
	FSoftClassPath OnFootReadout;

	/**
	 * The deposit prompt above the reticle. Must derive from <c>USpaceMMODepositPrompt</c>.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "HUD",
		meta = (MetaClass = "/Script/SpaceMMOBackend.SpaceMMODepositPrompt"))
	FSoftClassPath DepositPrompt;

	/**
	 * The skills screen opened with K. Must derive from <c>USpaceMMOSkillsScreen</c>.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "HUD",
		meta = (MetaClass = "/Script/SpaceMMOBackend.SpaceMMOSkillsScreen"))
	FSoftClassPath SkillsScreen;

	/**
	 * Messages floating above the pawn. Must derive from <c>USpaceMMOTransientMessages</c>.
	 *
	 * Unset means gather results fall back to the debug panel's transient line — no colour and no
	 * position, but visible, which matters because it is the only feedback a gather key gives.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "HUD",
		meta = (MetaClass = "/Script/SpaceMMOBackend.SpaceMMOTransientMessages"))
	FSoftClassPath TransientMessages;

	/**
	 * The station screen shown while docked. Must derive from <c>USpaceMMOStationOverlay</c>.
	 *
	 * Unset means the debug panel remains the only way to reach a market, an industry queue or a
	 * quest — which is what it is today, so unset is a working state rather than a broken one.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "HUD",
		meta = (MetaClass = "/Script/SpaceMMOBackend.SpaceMMOStationOverlay"))
	FSoftClassPath StationOverlay;

	/**
	 * Everything the character owns, opened with I. Must derive from
	 * <c>USpaceMMOInventoryScreen</c>.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "HUD",
		meta = (MetaClass = "/Script/SpaceMMOBackend.SpaceMMOInventoryScreen"))
	FSoftClassPath InventoryScreen;

	/**
	 * The sign-in screen. Must derive from <c>USpaceMMOLoginScreen</c>.
	 *
	 * Unset means the game behaves exactly as it did before task 107: credentials come from
	 * secrets/player-login.txt or the command line, and a machine with neither simply has no
	 * character. That is a working state, and it is the one every automated run is in.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "HUD",
		meta = (MetaClass = "/Script/SpaceMMOBackend.SpaceMMOLoginScreen"))
	FSoftClassPath LoginScreen;

	virtual FName GetCategoryName() const override { return TEXT("Game"); }
};
