#pragma once

#include "CoreMinimal.h"

class UStaticMesh;

/**
 * Whether a mesh a player can walk into is actually solid.
 *
 * <strong>Written because "no collision" and "no bug" produce identical logs.</strong> A character
 * sweeps with bTraceComplex left false, and the engine picks exactly one of simple or complex
 * collision from that flag -- never both. So a mesh imported without simple collision is invisible
 * to the sweep however solid it looks, and walking through it is indistinguishable from a fault in
 * the movement code. ADR-0013 accepts that cost; this is what keeps it a cost rather than a trap.
 *
 * Deposits and ships use it. Stations will the day their hulls stop being deliberately intangible,
 * and so will the first city prop -- which is why this lives in Core with a name of its own rather
 * than inline in whichever module noticed first.
 */
namespace SpaceMMOSolidity
{
	/**
	 * Warns, naming the asset, when a mesh has no simple collision for a character to hit.
	 *
	 * Measured off the built mesh rather than off the import settings that produced it, because the
	 * two disagree quietly: an FBX can be re-imported, a collision primitive added by hand in the
	 * Static Mesh editor, or a UCX_ mesh renamed, and none of that shows up in what the pipeline
	 * was once asked to do.
	 *
	 * @param Mesh    The mesh as it was loaded. Nothing is reported for a null mesh -- failing to
	 *                load is a different fault, and already has its own message.
	 * @param What    What kind of thing carries it, for the message: "Deposit", "Station".
	 * @param Which   Which one, so the line names something findable in the world.
	 */
	SPACEMMOCORE_API void ReportIfIntangible(
		const UStaticMesh* Mesh, const TCHAR* What, const FString& Which);
}
