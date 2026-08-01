#include "SpaceMMOTerrainPatchActor.h"

#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/World.h"
#include "SpaceMMOLog.h"
#include "SpaceMMORenderOrigin.h"

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
