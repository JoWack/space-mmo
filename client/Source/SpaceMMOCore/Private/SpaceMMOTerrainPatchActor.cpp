#include "SpaceMMOTerrainPatchActor.h"

#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/World.h"
#include "SpaceMMOLog.h"
#include "Materials/MaterialInterface.h"
#include "SpaceMMORenderOrigin.h"
#include "UObject/ConstructorHelpers.h"

using namespace UE::Geometry;

ASpaceMMOTerrainPatchActor::ASpaceMMOTerrainPatchActor()
{
	PrimaryActorTick.bCanEverTick = true;

	Ground = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("Ground"));
	SetRootComponent(Ground);

	// Collision comes later, and from the height function rather than from this mesh. The server
	// has no mesh to collide against and must still agree about where the ground is, so the mesh
	// can never be the authority on it.
	Ground->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// The same placeholder material the ship and planet use. Without one the component falls back
	// to the engine default, so the ground was the one thing in the scene not lit like the rest of
	// it — which makes judging the lighting harder than it needs to be.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> GroundMaterial(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	if (GroundMaterial.Succeeded())
	{
		Ground->SetMaterial(0, GroundMaterial.Object);
	}
}

void ASpaceMMOTerrainPatchActor::BeginPlay()
{
	Super::BeginPlay();

	ApplyRenderTransform();
}

void ASpaceMMOTerrainPatchActor::BuildPatch(
	const FPlanetConfig& InPlanet,
	const FPlanetTerrainConfig& InTerrain,
	const FVector& Direction)
{
	Planet = InPlanet;
	Terrain = InTerrain;
	PatchConfig.CentreDirection = Direction;

	// A dedicated server has no renderer and no reason to hold a few thousand triangles per
	// landing site. It already knows the ground through FPlanetTerrain, which is the only thing
	// it needs to decide where a player may stand.
	if (IsRunningDedicatedServer())
	{
		return;
	}

	const FPlanetPatchMesh Patch = FPlanetPatch::Build(Planet, Terrain, PatchConfig);

	if (!Patch.IsValid())
	{
		UE_LOG(LogSpaceMMO, Warning, TEXT("Terrain patch generated nothing."));

		return;
	}

	PatchOrigin = Patch.Origin;
	TriangleCount = Patch.Triangles.Num() / 3;

	FDynamicMesh3 Mesh;
	Mesh.EnableAttributes();

	for (const FVector& Position : Patch.Positions)
	{
		Mesh.AppendVertex(FVector3d(Position));
	}

	for (int32 Index = 0; Index + 2 < Patch.Triangles.Num(); Index += 3)
	{
		Mesh.AppendTriangle(
			Patch.Triangles[Index], Patch.Triangles[Index + 1], Patch.Triangles[Index + 2]);
	}

	// Normals were accumulated per-vertex during generation, so hills catch the light. Letting the
	// component recompute them would work too, but it would throw away information the generator
	// already had and pay for it again.
	if (FDynamicMeshNormalOverlay* Normals =
		Mesh.Attributes() != nullptr ? Mesh.Attributes()->PrimaryNormals() : nullptr)
	{
		Normals->ClearElements();

		TArray<int32> Elements;
		Elements.Reserve(Patch.Normals.Num());

		for (const FVector& Normal : Patch.Normals)
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

	Ground->SetMesh(MoveTemp(Mesh));
	Ground->NotifyMeshUpdated();

	ApplyRenderTransform();

	UE_LOG(LogSpaceMMO, Log,
		TEXT("Terrain patch at %s: %d triangles across %.1f degrees."),
		*PatchOrigin.ToString(),
		TriangleCount,
		PatchConfig.AngularRadiusDegrees);

	// Where the mesh physically ended up, in the space the renderer works in.
	//
	// Every check so far said this patch is under the viewer's feet, and the viewer reports no
	// ground there. One of those is wrong, and arithmetic done away from the running game has not
	// been able to say which. These are the numbers the renderer actually gets.
	const FVector ActorLocation = GetActorLocation();
	const FBoxSphereBounds MeshBounds = Ground->Bounds;

	// What the component is actually holding, as opposed to what was handed to it. A null material
	// draws as engine grey rather than as nothing, and an empty mesh would have zero bounds, so
	// both are already unlikely — but they are cheap to rule out and expensive to assume.
	const UMaterialInterface* Material = Ground->GetMaterial(0);

	// IsVisible() is the component's own flag and says nothing about whether the actor around it is
	// being rendered, or whether the component was ever registered with the scene. A component can
	// report itself visible, hold a mesh, carry a material and still never reach the renderer if
	// its owner is hidden or it never registered — which is the remaining difference between this
	// and the globe, since the globe's component is created on an actor that spawns deferred while
	// this one is spawned mid-Tick and moved afterwards.
	UE_LOG(LogSpaceMMO, Log,
		TEXT("  component holds %d triangles, material %s, visible %d, scale %s."),
		Ground->GetDynamicMesh() != nullptr
			? Ground->GetDynamicMesh()->GetTriangleCount()
			: -1,
		Material != nullptr ? *Material->GetName() : TEXT("NONE"),
		Ground->IsVisible() ? 1 : 0,
		*Ground->GetComponentScale().ToCompactString());

	UE_LOG(LogSpaceMMO, Log,
		TEXT("  actor hidden %d, component registered %d, has proxy %d, render in main pass %d."),
		IsHidden() ? 1 : 0,
		Ground->IsRegistered() ? 1 : 0,
		Ground->SceneProxy != nullptr ? 1 : 0,
		Ground->bRenderInMainPass ? 1 : 0);

	const int32 Centre = (PatchConfig.Resolution * PatchConfig.Resolution) / 2;

	UE_LOG(LogSpaceMMO, Log,
		TEXT("  patch actor at %s, mesh centre vertex local %s, bounds origin %s radius %.0f cm."),
		*ActorLocation.ToCompactString(),
		Patch.Positions.IsValidIndex(Centre)
			? *Patch.Positions[Centre].ToCompactString()
			: TEXT("none"),
		*MeshBounds.Origin.ToCompactString(),
		MeshBounds.SphereRadius);

	if (const APlayerController* Controller =
		GetWorld() != nullptr ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		FVector ViewLocation = FVector::ZeroVector;
		FRotator ViewRotation = FRotator::ZeroRotator;

		const_cast<APlayerController*>(Controller)->GetPlayerViewPoint(ViewLocation, ViewRotation);

		UE_LOG(LogSpaceMMO, Log,
			TEXT("  camera at %s, %.0f cm from the patch's centre vertex."),
			*ViewLocation.ToCompactString(),
			FVector::Dist(
				ViewLocation,
				ActorLocation
					+ (Patch.Positions.IsValidIndex(Centre)
						? Patch.Positions[Centre]
						: FVector::ZeroVector)));
	}
}

void ASpaceMMOTerrainPatchActor::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const UWorld* World = GetWorld();

	const USpaceMMORenderOriginSubsystem* Origin =
		World != nullptr ? World->GetSubsystem<USpaceMMORenderOriginSubsystem>() : nullptr;

	// Only when the origin actually moves. The patch does not move on its own — the ground stays
	// where it is — so between rebases its transform is already correct.
	if (Origin == nullptr || Origin->GetRevision() == BuiltAtRevision)
	{
		return;
	}

	ApplyRenderTransform();
}

void ASpaceMMOTerrainPatchActor::ApplyRenderTransform()
{
	const UWorld* World = GetWorld();

	const USpaceMMORenderOriginSubsystem* Origin =
		World != nullptr ? World->GetSubsystem<USpaceMMORenderOriginSubsystem>() : nullptr;

	if (Origin == nullptr)
	{
		return;
	}

	// Vertices are relative to the patch origin, so placing the actor at that origin is the whole
	// transform. This is why the mesh could be generated far from the system origin without losing
	// precision in the first place.
	SetActorLocation(Origin->ToWorldLocation(PatchOrigin));

	BuiltAtRevision = Origin->GetRevision();
}
