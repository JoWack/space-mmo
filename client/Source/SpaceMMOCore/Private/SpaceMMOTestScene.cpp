#include "SpaceMMOTestScene.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "SpaceMMOLog.h"
#include "SpaceMMORenderOrigin.h"
#include "UObject/ConstructorHelpers.h"

ASpaceMMOTestScene::ASpaceMMOTestScene()
{
	PrimaryActorTick.bCanEverTick = true;

	Markers = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Markers"));
	SetRootComponent(Markers);

	Markers->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Markers->SetCastShadow(false);

	// Engine content, so the project needs no authored assets to be visible. Everything here is
	// scaffolding for looking at the coordinate system, not content.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(
		TEXT("/Engine/BasicShapes/Cube.Cube"));

	if (CubeMesh.Succeeded())
	{
		Markers->SetStaticMesh(CubeMesh.Object);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> CubeMaterial(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	if (CubeMaterial.Succeeded())
	{
		Markers->SetMaterial(0, CubeMaterial.Object);
	}
}

void ASpaceMMOTestScene::BeginPlay()
{
	Super::BeginPlay();

	BuildMarkerPositions();
	RefreshInstances();

	UE_LOG(LogSpaceMMO, Log, TEXT("Test scene ready: %d markers, %.0f km lattice spacing."),
		MarkerPositions.Num(), SpacingKilometres);
}

void ASpaceMMOTestScene::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const USpaceMMORenderOriginSubsystem* Origin =
		GetWorld() != nullptr
			? GetWorld()->GetSubsystem<USpaceMMORenderOriginSubsystem>()
			: nullptr;

	if (Origin == nullptr || Origin->GetRevision() == BuiltAtRevision)
	{
		return;
	}

	RefreshInstances();
}

void ASpaceMMOTestScene::BuildMarkerPositions()
{
	MarkerPositions.Reset();

	if (MarkersPerAxis <= 0 || SpacingKilometres <= 0.0)
	{
		return;
	}

	// A lattice centred on the origin, so the ship starts inside it and has markers in every
	// direction rather than only ahead.
	const double HalfExtent = (MarkersPerAxis - 1) * 0.5;

	for (int32 X = 0; X < MarkersPerAxis; ++X)
	{
		for (int32 Y = 0; Y < MarkersPerAxis; ++Y)
		{
			for (int32 Z = 0; Z < MarkersPerAxis; ++Z)
			{
				MarkerPositions.Add(FSystemCoordinate(
					(X - HalfExtent) * SpacingKilometres,
					(Y - HalfExtent) * SpacingKilometres,
					(Z - HalfExtent) * SpacingKilometres));
			}
		}
	}

	// A few distant beacons well outside the lattice, so there is still something to see after
	// flying past the near field — and so the far end of the coordinate range gets exercised
	// rather than only the comfortable middle.
	for (const double Distance : { 100.0, 1000.0, 25000.0 })
	{
		MarkerPositions.Add(FSystemCoordinate(Distance, 0.0, 0.0));
		MarkerPositions.Add(FSystemCoordinate(0.0, Distance, 0.0));
		MarkerPositions.Add(FSystemCoordinate(0.0, 0.0, Distance));
	}
}

void ASpaceMMOTestScene::RefreshInstances()
{
	if (Markers == nullptr)
	{
		return;
	}

	const UWorld* World = GetWorld();

	const USpaceMMORenderOriginSubsystem* Origin =
		World != nullptr ? World->GetSubsystem<USpaceMMORenderOriginSubsystem>() : nullptr;

	if (Origin == nullptr)
	{
		return;
	}

	// Sized in centimetres, and the engine cube is 100 cm to a side.
	const double Scale = MarkerSizeMetres;

	TArray<FTransform> Transforms;
	Transforms.Reserve(MarkerPositions.Num());

	for (const FSystemCoordinate& Position : MarkerPositions)
	{
		Transforms.Add(FTransform(
			FQuat::Identity,
			Origin->ToWorldLocation(Position),
			FVector(Scale)));
	}

	// Rebuilt wholesale rather than updated in place. This runs once per rebase — a handful of
	// times a minute at most — so the simpler code is worth more than the saved allocation.
	Markers->ClearInstances();
	Markers->AddInstances(Transforms, /*bShouldReturnIndices*/ false, /*bWorldSpace*/ true);

	// Logged because rebasing is otherwise entirely invisible — which is the point of it, and also
	// why it needs a trace when something goes wrong.
	UE_LOG(LogSpaceMMO, Verbose, TEXT("Rebuilt %d markers for render origin %s (revision %d)."),
		Transforms.Num(), *Origin->GetRenderOrigin().ToString(), Origin->GetRevision());

	BuiltAtRevision = Origin->GetRevision();
}
