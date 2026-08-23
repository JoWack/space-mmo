#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "SpaceMMOStationSettings.generated.h"

class UStaticMesh;

/**
 * What a station looks like, and how large it stands.
 *
 * <strong>Config rather than code, for the reason the deposit settings give.</strong> Every station
 * in the game was the same engine cube — a trading hub, a spaceport and somebody's house all
 * identical — because <c>kind</c> reached the client as an opaque string and was used in exactly one
 * log line. Naming meshes here means a new kind of building is a settings entry rather than a
 * recompile, and the mapping lands in DefaultGame.ini where a diff will show it.
 *
 * <strong>Keyed by the authored kind and key, never by the database id.</strong> Ids are assigned by
 * whichever database seeded last; a mapping keyed by id points at the wrong building the first time
 * the database is rebuilt in a different order. The kind is what the server sends —
 * <c>WorldEndpoints.cs</c> serialises the enum by name, so these keys read <c>TradingHub</c> and
 * <c>Spaceport</c>.
 *
 * Soft references, so a world with one station does not pay to load every model in the catalogue.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "SpaceMMO Stations"))
class SPACEMMOBACKEND_API USpaceMMOStationSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/**
	 * Station kind to mesh, e.g. <c>TradingHub</c>.
	 *
	 * A kind with no entry keeps the engine cube rather than rendering nothing. A station that
	 * failed to draw would still be dockable and still be invisible, which reads as the station not
	 * existing and is far worse than an ugly placeholder.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Stations")
	TMap<FString, TSoftObjectPtr<UStaticMesh>> MeshesByKind;

	/**
	 * One named station's own mesh, e.g. <c>station_capital_hub</c>, overriding its kind.
	 *
	 * So a single landmark can differ without inventing a kind for it — and it is what a
	 * settlement's anchor will use when 97 gets built, since a town is a cluster of ordinary kinds
	 * with one of them carrying the look of the place.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Stations")
	TMap<FString, TSoftObjectPtr<UStaticMesh>> MeshesByKey;

	/**
	 * How large each kind stands, in metres.
	 *
	 * <strong>Judged against the horizon, not against a picture of a space station.</strong> This
	 * planet has a radius of twenty kilometres, so from eye height the horizon is about two hundred
	 * and sixty metres away: a sixty-metre building subtends thirteen degrees at that range and
	 * reads as a structure the size of the visible world, which is exactly how the first one looked.
	 *
	 * A single value for every kind was the previous state, and twenty-five metres of house is the
	 * absurdity that came with it.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Stations")
	TMap<FString, double> SizeMetresByKind;

	/** What an unlisted kind stands at. The value every station used before kinds were sized. */
	UPROPERTY(EditAnywhere, Config, Category = "Stations")
	double DefaultSizeMetres = 25.0;

	virtual FName GetCategoryName() const override { return TEXT("Game"); }
};

/**
 * Choosing a station's look from its key and kind.
 *
 * Pure statics, so the resolution order and the arithmetic can be checked without a mesh, a world or
 * an editor — the same treatment, and the same reasoning, as <see cref="FDepositPlacement"/>.
 */
struct SPACEMMOBACKEND_API FStationAppearance
{
	/**
	 * The mesh for a station, or null to keep the placeholder.
	 *
	 * The station's own key wins over its kind, because an override that could be beaten by the
	 * general case would not be an override.
	 */
	static TSoftObjectPtr<UStaticMesh> MeshFor(
		const USpaceMMOStationSettings& Settings, const FString& Key, const FString& Kind);

	/** How large this kind stands, in metres, falling back to the default. */
	static double SizeMetresFor(const USpaceMMOStationSettings& Settings, const FString& Kind);

	/**
	 * Uniform scale that fits a mesh's largest dimension to a target size.
	 *
	 * <strong>Uniform, and fitted rather than assumed.</strong> The previous version divided a
	 * target by the engine cube's known hundred centimetres, which is correct exactly as long as
	 * every station is that cube. Any authored model would have arrived at whatever size it was
	 * exported at, which is the same trap deposits already hit and solved.
	 *
	 * Returns 1 for a mesh with no extent, leaving the model as authored rather than collapsing it
	 * to a point — a station that vanished would read as one that never spawned.
	 */
	static double UniformScaleForSize(const FVector& LocalBoxExtent, double TargetSizeCentimetres);
};
