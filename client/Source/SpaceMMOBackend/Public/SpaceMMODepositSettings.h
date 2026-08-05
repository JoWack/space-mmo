#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "SpaceMMODepositSettings.generated.h"

/**
 * Which mesh stands in the world for which material.
 *
 * <strong>Config rather than code.</strong> Deposits used one hard-coded engine cylinder for every
 * material in the game, which was fine while there was one ore and stops being fine the moment
 * there are eight. Naming meshes here means a new ore added in content is a settings entry rather
 * than a recompile, and the mapping lands in DefaultGame.ini where it shows up in a diff — the same
 * reason recipes and deposits live in data/ rather than in a header.
 *
 * <strong>Keyed by item key, not by id.</strong> Ids are assigned by whichever database seeded
 * last; keys are what content authored and what the server sends. A mapping keyed by id would point
 * at the wrong rock the first time the database was rebuilt in a different order.
 *
 * Soft references, so a mesh loads when a deposit that needs it is placed rather than when the
 * module does. A world with two ferrite nodes should not pay for every model in the catalogue.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "SpaceMMO Deposits"))
class SPACEMMOBACKEND_API USpaceMMODepositSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/**
	 * Item key to mesh, e.g. <c>ferrite_ore</c>.
	 *
	 * An item with no entry falls back to the engine cylinder rather than to nothing. A deposit
	 * that failed to render would still be minable and still be invisible, which reads as the
	 * deposit not existing and is far worse than an ugly placeholder.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Deposits")
	TMap<FString, TSoftObjectPtr<UStaticMesh>> Meshes;

	virtual FName GetCategoryName() const override { return TEXT("Game"); }
};

/**
 * Fitting an arbitrary mesh into the space a deposit is supposed to occupy.
 *
 * Pure statics, so the arithmetic can be tested without a mesh, a world, or an editor. The two
 * things it exists to absorb are the two that differ between any two models somebody authors: how
 * big they were exported and where their pivot sits.
 */
struct SPACEMMOBACKEND_API FDepositPlacement
{
	/** How wide a deposit stands, in centimetres. Roughly a character's width. */
	static constexpr double TargetWidthCentimetres = 200.0;

	/** How tall a deposit stands, in centimetres. A shape you walk up to, not one you hunt for. */
	static constexpr double TargetHeightCentimetres = 300.0;

	/**
	 * Uniform scale that fits a mesh inside the target box.
	 *
	 * <strong>Uniform, and deliberately not per-axis.</strong> Stretching each axis to its target
	 * independently would make every model that was not authored at exactly a 2:3 ratio look
	 * squashed, and the artist would have no way to tell whether their proportions were wrong or the
	 * game was lying about them. Fitting instead means a model of any dimensions arrives at a
	 * sensible size with its shape intact.
	 *
	 * @param LocalBoxExtent Half-extents of the mesh's bounds, unscaled.
	 */
	static double UniformScale(const FVector& LocalBoxExtent);

	/**
	 * How far to lift a scaled mesh so its lowest point sits on the surface.
	 *
	 * Handles both pivot conventions without being told which is in use: a model whose origin is at
	 * its base lifts by nothing, and one centred on its origin lifts by half its height. Assuming
	 * either would half-bury or float every model authored the other way, and the symptom is a rock
	 * that looks wrong without anything reporting an error.
	 */
	static double BaseLift(const FVector& LocalOrigin, const FVector& LocalBoxExtent, double Scale);
};
