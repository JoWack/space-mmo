#pragma once

#include "CoreMinimal.h"

class AActor;
class APlayerController;
class UWidget;

/**
 * Putting HUD widgets over things in the world.
 *
 * Shared by the transient messages and the deposit prompt because both answer the same question —
 * where on screen is the top of that actor — and both get it wrong in the same two ways if they
 * answer it separately. The reasoning that must not be duplicated is in ProjectAbove.
 */
namespace SpaceMMO::Hud
{
	/**
	 * How big an actor looks, ignoring what is only there for developers.
	 *
	 * <strong>Not GetActorBounds.</strong> Two corrections it cannot express are both needed here:
	 * non-colliding components must count, because these meshes are deliberately collisionless and a
	 * label belongs where a thing appears rather than where it can be bumped into; and visualisation
	 * components must not, because every <c>UCameraComponent</c> registers a
	 * <c>DrawFrustumComponent</c> outside shipping builds and a frustum is a 10 m box.
	 *
	 * Falls back to the actor's own location with no extent when nothing drawable is found, rather
	 * than the zero box at the world origin that <c>GetActorBounds</c> returns (Actor.cpp:2267) —
	 * that one is not an error and does not look like one.
	 */
	SPACEMMOBACKEND_API void VisibleBounds(const AActor* Actor, FVector& Origin, FVector& Extent);

	/**
	 * Where to draw a label floating above an actor, in widget space.
	 *
	 * <strong>Up is the actor's up, not the world's.</strong> On a sphere those agree at exactly one
	 * point, so using the world's would put a label sideways everywhere else.
	 *
	 * The height is a multiple of the actor's own bounding radius rather than a fixed distance, so a
	 * character, a ship and a rock all clear themselves with nothing tuned per actor.
	 *
	 * @return false when the point is behind the camera or otherwise not on screen. A caller must do
	 *         something deliberate with that — either hide, or fall back to a fixed place — because
	 *         it happens routinely: first person puts the camera inside the pawn, and a deposit in
	 *         reach can be behind the player.
	 */
	SPACEMMOBACKEND_API bool ProjectAbove(
		const APlayerController* Controller,
		const AActor* Actor,
		float HeightScale,
		FVector2D& OutPosition);

	/**
	 * Moves a widget's canvas slot to a widget-space position, anchored by its bottom centre.
	 *
	 * The widget must be a direct child of a Canvas Panel; nothing else carries a position. Does
	 * nothing if it is not, rather than asserting — a Blueprint is allowed to be wired wrong, and
	 * the caller warns about that once rather than crashing.
	 *
	 * @return false when there was no canvas slot to write to, so a caller can say so rather than
	 *         leaving "the label did not move" and "the label moved somewhere wrong" looking alike.
	 */
	SPACEMMOBACKEND_API bool PlaceAt(UWidget* Widget, const FVector2D& Position);
}
