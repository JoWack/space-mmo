#include "SpaceMMOPreviewBody.h"

#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "SpaceMMOAuthoringLog.h"
#include "SpaceMMOCoordinates.h"
#include "SpaceMMOPlanetActor.h"
#include "SpaceMMOPlanetMeshAttributes.h"
#include "SpaceMMOWorldSubsystem.h"

using namespace UE::Geometry;

double FSpaceMMOPreviewScale::DrawnRadiusKilometres()
{
	// Asked of the thing that draws it rather than written down again here. The radius is still
	// compiled in rather than authored (task 123), and the day it becomes content this is the one
	// place that has to notice.
	return USpaceMMOWorldSubsystem::StartingPlanet().RadiusKilometres;
}

FPlanetConfig FSpaceMMOPreviewScale::PlanetFor(const double PreviewRadiusCentimetres)
{
	FPlanetConfig Planet;

	Planet.Centre = FSystemCoordinate(0.0, 0.0, 0.0);
	Planet.RadiusKilometres =
		PreviewRadiusCentimetres / SpaceMMO::Coordinates::CentimetresPerKilometre;

	return Planet;
}

FPlanetTerrainConfig FSpaceMMOPreviewScale::TerrainFor(
	const FSpaceMMOAuthoredBody& Body,
	const double DrawnRadiusKilometres,
	const double PreviewRadiusCentimetres)
{
	FPlanetTerrainConfig Terrain;

	Terrain.Seed = Body.TerrainSeed;
	Terrain.BaseFrequency = Body.BaseFrequency;

	const double PreviewRadiusKilometres =
		PreviewRadiusCentimetres / SpaceMMO::Coordinates::CentimetresPerKilometre;

	// Relief against the radius the planet is <em>drawn</em> at, not the radius it is authored at.
	// The picture is what is being scaled, and the picture a player sees is a 20 km planet.
	const double Ratio = DrawnRadiusKilometres > 0.0
		? PreviewRadiusKilometres / DrawnRadiusKilometres
		: 1.0;

	Terrain.MaxElevationKilometres = Body.MaxElevationKilometres * Ratio;

	return Terrain;
}

ASpaceMMOPreviewBody::ASpaceMMOPreviewBody()
{
	PrimaryActorTick.bCanEverTick = false;

	Surface = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("Surface"));
	SetRootComponent(Surface);

	// No collision, for the same reason the game's planet has none: nothing here is simulated, and
	// a collision body the size of a globe is a poor thing to hand the solver.
	Surface->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void ASpaceMMOPreviewBody::Build(
	const FSpaceMMOAuthoredBody& InBody, const double PreviewRadiusCentimetres)
{
	AuthoredBody = InBody;

	PreviewPlanet = FSpaceMMOPreviewScale::PlanetFor(PreviewRadiusCentimetres);
	PreviewTerrain = FSpaceMMOPreviewScale::TerrainFor(
		InBody, FSpaceMMOPreviewScale::DrawnRadiusKilometres(), PreviewRadiusCentimetres);

	if (Surface == nullptr)
	{
		return;
	}

	const FPlanetGlobeMesh Globe =
		FPlanetGlobe::Build(PreviewPlanet, PreviewTerrain, GlobeConfig);

	if (!Globe.IsValid())
	{
		UE_LOG(LogSpaceMMOAuthoring, Warning,
			TEXT("The preview of '%s' tessellated nothing."), *InBody.Key);

		return;
	}

	FDynamicMesh3 Mesh;

	Mesh.EnableAttributes();

	for (const FVector& Position : Globe.Positions)
	{
		Mesh.AppendVertex(FVector3d(Position));
	}

	for (int32 Index = 0; Index + 2 < Globe.Triangles.Num(); Index += 3)
	{
		Mesh.AppendTriangle(
			Globe.Triangles[Index], Globe.Triangles[Index + 1], Globe.Triangles[Index + 2]);
	}

	if (FDynamicMeshNormalOverlay* const Normals =
		Mesh.Attributes() != nullptr ? Mesh.Attributes()->PrimaryNormals() : nullptr)
	{
		Normals->ClearElements();

		TArray<int32> Elements;

		Elements.Reserve(Globe.Normals.Num());

		for (const FVector& Normal : Globe.Normals)
		{
			Elements.Add(Normals->AppendElement(FVector3f(Normal)));
		}

		for (const int32 TriangleId : Mesh.TriangleIndicesItr())
		{
			const FIndex3i Triangle = Mesh.GetTriangle(TriangleId);

			Normals->SetTriangle(
				TriangleId,
				FIndex3i(Elements[Triangle.A], Elements[Triangle.B], Elements[Triangle.C]));
		}
	}

	// Height and steepness, in the channel the terrain material reads them from. Without these the
	// preview would draw one flat colour and the palette could not be judged by eye at all, which
	// is half of what looking at a planet in an editor is for.
	FPlanetMeshAttributes::Write(Mesh, Globe.SurfaceUVs, Globe.GroundKinds);

	Surface->SetMesh(MoveTemp(Mesh));
	Surface->NotifyMeshUpdated();

	// The same material the game is configured to draw terrain with, read from that actor's own
	// settings rather than from a second key of this module's own. Two settings for one material is
	// two things to keep in step, and the preview would be silently wrong whenever they drifted.
	const FSoftObjectPath ConfiguredMaterial =
		GetDefault<ASpaceMMOPlanetActor>()->TerrainMaterial;

	UMaterialInterface* const Material =
		ConfiguredMaterial.IsNull() ? nullptr : Cast<UMaterialInterface>(ConfiguredMaterial.TryLoad());

	if (Material == nullptr)
	{
		// Said out loud on the path that does nothing as well as the one that does. A preview drawn
		// in engine grey because no material is configured looks exactly like one drawn grey
		// because this code did not run.
		UE_LOG(LogSpaceMMOAuthoring, Warning,
			TEXT("No terrain material ('%s'); '%s' previews in the default grey."),
			ConfiguredMaterial.IsNull() ? TEXT("<unset>") : *ConfiguredMaterial.ToString(),
			*InBody.Key);
	}
	else
	{
		SurfaceMaterial = UMaterialInstanceDynamic::Create(Material, this);

		if (SurfaceMaterial != nullptr)
		{
			Surface->SetMaterial(0, SurfaceMaterial);

			if (InBody.bHasAppearance)
			{
				SurfaceMaterial->SetVectorParameterValue(TEXT("LowColour"), InBody.LowColour);
				SurfaceMaterial->SetVectorParameterValue(TEXT("HighColour"), InBody.HighColour);
				SurfaceMaterial->SetVectorParameterValue(TEXT("RockColour"), InBody.RockColour);
				SurfaceMaterial->SetScalarParameterValue(
					TEXT("HeightFrom"), InBody.PaletteRanges.X);
				SurfaceMaterial->SetScalarParameterValue(TEXT("HeightTo"), InBody.PaletteRanges.Y);
				SurfaceMaterial->SetScalarParameterValue(TEXT("SlopeFrom"), InBody.PaletteRanges.Z);
				SurfaceMaterial->SetScalarParameterValue(TEXT("SlopeTo"), InBody.PaletteRanges.W);
			}
		}
	}

	// What was built, so a preview that came out wrong can be diagnosed from the log rather than
	// from a screenshot: the scale it was drawn at, and the shape it was drawn from.
	UE_LOG(LogSpaceMMOAuthoring, Log,
		TEXT("Previewing '%s' at %.0f m (authored %.1f km, drawn %.1f km): seed %lld, "
			"relief %.1f m of preview, frequency %.1f, %d vertices."),
		*InBody.Key,
		PreviewPlanet.RadiusKilometres * 1000.0,
		InBody.RadiusKilometres,
		FSpaceMMOPreviewScale::DrawnRadiusKilometres(),
		PreviewTerrain.Seed,
		PreviewTerrain.MaxElevationKilometres * 1000.0,
		PreviewTerrain.BaseFrequency,
		Globe.Positions.Num());
}

double ASpaceMMOPreviewBody::SurfaceRadiusCentimetres(const FVector& Direction) const
{
	return FPlanetTerrain::SurfaceRadiusKilometres(PreviewPlanet, PreviewTerrain, Direction)
		* SpaceMMO::Coordinates::CentimetresPerKilometre;
}

FVector ASpaceMMOPreviewBody::SurfaceLocation(const FVector& Direction) const
{
	const FVector Unit = Direction.GetSafeNormal();

	if (Unit.IsNearlyZero())
	{
		return GetActorLocation();
	}

	return GetActorLocation() + Unit * SurfaceRadiusCentimetres(Unit);
}

FVector ASpaceMMOPreviewBody::DirectionOf(const FVector& WorldLocation) const
{
	return (WorldLocation - GetActorLocation()).GetSafeNormal();
}

double ASpaceMMOPreviewBody::KilometresPerPreviewCentimetre() const
{
	const double PreviewRadiusCentimetres =
		PreviewPlanet.RadiusKilometres * SpaceMMO::Coordinates::CentimetresPerKilometre;

	if (PreviewRadiusCentimetres <= 0.0)
	{
		return 0.0;
	}

	// Distances are reported in the kilometres of the planet the game draws, because that is the
	// ground somebody will walk across to reach the thing being placed. Preview centimetres are an
	// artefact of the model and mean nothing to anyone.
	return FSpaceMMOPreviewScale::DrawnRadiusKilometres() / PreviewRadiusCentimetres;
}
