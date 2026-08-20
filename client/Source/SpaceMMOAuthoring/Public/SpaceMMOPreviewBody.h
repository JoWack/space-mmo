#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SpaceMMOPlanet.h"
#include "SpaceMMOPlanetGlobe.h"
#include "SpaceMMOPlanetTerrain.h"
#include "SpaceMMOWorldDocument.h"
#include "SpaceMMOPreviewBody.generated.h"

class UDynamicMeshComponent;

/**
 * How an authored body becomes something you can walk a camera around.
 *
 * <strong>A table-top globe, and the reason is that directions do not care.</strong> A deposit is
 * authored as a direction from the body's centre and nothing else, so any radius shows the same
 * placement — but not the same <em>picture</em>. Ares is 339 km authored and half a kilometre of
 * relief on that is 0.15% of the radius: a smooth ball with no visible ground to place anything
 * against. The game draws it at 20 km (task 123), where the same relief is 2.5% and reads as
 * mountains.
 *
 * So the preview keeps the proportion the game draws and shrinks both numbers together: relief is
 * scaled by preview radius over drawn radius, which makes the silhouette the one a player sees at
 * a size that fits in a viewport. Terrain is a function of direction alone, so the pattern of hills
 * is identical either way — only the scale changes.
 *
 * Pure statics so the scaling can be checked in a test, which matters because getting it wrong
 * (scaling relief against the <em>authored</em> radius rather than the drawn one) produces a
 * preview that looks plausible and is not the planet.
 */
struct SPACEMMOAUTHORING_API FSpaceMMOPreviewScale
{
	/** Five hundred metres: large enough to fly a viewport camera around, small enough to frame. */
	static constexpr double DefaultPreviewRadiusCentimetres = 50000.0;

	/** What the game actually draws a planet at, which is what the preview is a scale model of. */
	static double DrawnRadiusKilometres();

	static FPlanetConfig PlanetFor(double PreviewRadiusCentimetres);

	static FPlanetTerrainConfig TerrainFor(
		const FSpaceMMOAuthoredBody& Body,
		double DrawnRadiusKilometres,
		double PreviewRadiusCentimetres);
};

/**
 * An authored body, drawn so things can be placed on it.
 *
 * The mesh comes from <see cref="FPlanetGlobe"/> and the ground from <see cref="FPlanetTerrain"/> —
 * the same two functions the client and the dedicated server use — so a marker cannot be standing
 * on ground the game does not have. Anything that built its own approximation of the surface here
 * would let the editor and the game disagree about where the ground is, which is the one failure
 * this tool cannot be allowed to have.
 *
 * Transient and spawned by the panel: it is never saved into a level, because a level holding a
 * copy of the world would be the second source of truth task 96 exists to avoid.
 */
UCLASS(NotPlaceable, Transient)
class SPACEMMOAUTHORING_API ASpaceMMOPreviewBody : public AActor
{
	GENERATED_BODY()

public:
	ASpaceMMOPreviewBody();

	/** Tessellates the body and paints it with its authored palette, if it has one. */
	void Build(const FSpaceMMOAuthoredBody& InBody, double PreviewRadiusCentimetres);

	/** The authored body this is a scale model of. */
	const FSpaceMMOAuthoredBody& GetAuthoredBody() const { return AuthoredBody; }

	/** How far the ground is from the centre along a direction, in preview centimetres. */
	double SurfaceRadiusCentimetres(const FVector& Direction) const;

	/** Where something authored in a direction stands, in world space. */
	FVector SurfaceLocation(const FVector& Direction) const;

	/** The direction a world location names, as content would author it. */
	FVector DirectionOf(const FVector& WorldLocation) const;

	/** Preview centimetres per authored kilometre, for reporting how far something moved. */
	double KilometresPerPreviewCentimetre() const;

private:
	UPROPERTY()
	TObjectPtr<UDynamicMeshComponent> Surface;

	UPROPERTY()
	TObjectPtr<class UMaterialInstanceDynamic> SurfaceMaterial;

	FSpaceMMOAuthoredBody AuthoredBody;

	FPlanetConfig PreviewPlanet;

	FPlanetTerrainConfig PreviewTerrain;

	FPlanetGlobeConfig GlobeConfig;
};
