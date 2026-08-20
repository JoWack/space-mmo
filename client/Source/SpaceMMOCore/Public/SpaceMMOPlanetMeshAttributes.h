#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"

/**
 * Putting what the terrain knows about itself onto a mesh a material can read.
 *
 * <strong>Its own thing, because this is where a working terrain stopped being visible.</strong>
 * The builders that compute height and steepness are tested and were correct throughout; the step
 * that carries them onto the mesh was not tested, and it put them in a UV channel materials read as
 * a constant. Every measurement passed and the ground was one flat colour, which is the exact shape
 * of failure a test between the two would have caught.
 */
class SPACEMMOCORE_API FPlanetMeshAttributes
{
public:
	/**
	 * Writes the surface parameterisation and the ground's own description onto a built mesh.
	 *
	 * UV0 carries cube-face coordinates in 0..1, unscaled — the material owns how large a texture
	 * reads, and this owns only where a point is.
	 *
	 * Height and steepness go in the <strong>vertex colour</strong>, red and green, not a second UV
	 * channel. They were in UV1 first and the mesh carried them correctly — the scene proxy forwards
	 * every layer it finds — and a material reading TexCoord[1] still got a constant. Vertex colour
	 * is the channel the engine and every terrain material already agree on for blend weights, and
	 * it has no index to get wrong. Blue is left free for whatever the third thing turns out to be.
	 *
	 * Both the globe and the patch go through here rather than each writing its own, because they
	 * are two samplings of one surface and a material fed differently by each would draw a line
	 * around the patch where they meet.
	 *
	 * Does nothing when either array is shorter than the mesh has vertices: a partly written overlay
	 * would be worse than none, since it would blend toward whatever the missing values defaulted to
	 * and look deliberate.
	 */
	static void Write(
		UE::Geometry::FDynamicMesh3& Mesh,
		const TArray<FVector2D>& SurfaceUVs,
		const TArray<FVector2D>& GroundKinds);
};
