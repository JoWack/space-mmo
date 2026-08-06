#include "SpaceMMODepositActor.h"

#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "SpaceMMOBackendLog.h"
#include "SpaceMMODepositSettings.h"
#include "SpaceMMORenderOrigin.h"
#include "UObject/ConstructorHelpers.h"

namespace SpaceMMODeposit
{
	/** The engine cylinder is 100 cm tall and 100 cm across. */
	constexpr double EngineCylinderSizeCentimetres = 100.0;

	/**
	 * How big a deposit stands, in metres.
	 *
	 * A character is about two metres, so this is a shape you walk up to rather than one you have
	 * to hunt for. Worth stating in metres and converting once: a hull radius was set to 0.02
	 * kilometres earlier in this project on the assumption it was metres, and wrapped a two-metre
	 * cone in a twenty-metre collision sphere.
	 */
	constexpr double WidthMetres = 2.0;
	constexpr double HeightMetres = 3.0;
}

ASpaceMMODepositActor::ASpaceMMODepositActor()
{
	PrimaryActorTick.bCanEverTick = true;

	Marker = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Marker"));
	SetRootComponent(Marker);

	// No collision, for the same reason the planet and the ship have none: contact here is decided
	// by FPlanetTerrain, not by Chaos, and a solver body would be a second opinion about where
	// solid things are. Gathering range is measured, not collided.
	Marker->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));

	if (CylinderMesh.Succeeded())
	{
		Marker->SetStaticMesh(CylinderMesh.Object);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	if (Material.Succeeded())
	{
		Marker->SetMaterial(0, Material.Object);
	}
}

void ASpaceMMODepositActor::Configure(
	const FBackendResourceNode& InNode,
	const FPlanetConfig& InPlanet,
	const FPlanetTerrainConfig& InTerrain)
{
	Node = InNode;
	Planet = InPlanet;
	Terrain = InTerrain;

	ApplyConfiguredMesh();

	// The one place a direction becomes a position. Both machines run this same call on the same
	// inputs, which is why neither has to be told the answer.
	SurfacePosition = FPlanetTerrain::SurfacePosition(Planet, Terrain, Node.Direction);

	ApplyRenderTransform();
}

void ASpaceMMODepositActor::ApplyConfiguredMesh()
{
	const USpaceMMODepositSettings* Settings = GetDefault<USpaceMMODepositSettings>();

	if (Settings == nullptr || Marker == nullptr)
	{
		return;
	}

	const TSoftObjectPtr<UStaticMesh>* Configured = Settings->Meshes.Find(Node.ItemKey);

	if (Configured == nullptr || Configured->IsNull())
	{
		// Keeps the cylinder the constructor attached. An unmapped ore is still minable, and a
		// deposit that rendered as nothing would look exactly like one that was never placed.
		return;
	}

	// Loaded synchronously, and deliberately: deposits are placed once when the world is built,
	// not per frame, and a rock that popped in a second late would have players walking through
	// the space where it was about to be.
	UStaticMesh* Mesh = Configured->LoadSynchronous();

	if (Mesh == nullptr)
	{
		UE_LOG(LogSpaceMMOBackend, Warning,
			TEXT("Deposit mesh for '%s' is configured but failed to load; using the placeholder."),
			*Node.ItemKey);

		return;
	}

	Marker->SetStaticMesh(Mesh);

	// The mesh brings its own materials. The placeholder material the constructor set is for the
	// engine cylinder, and leaving it on would repaint an authored model in flat grey.
	Marker->EmptyOverrideMaterials();
}

void ASpaceMMODepositActor::BeginPlay()
{
	Super::BeginPlay();

	ApplyRenderTransform();

	UE_LOG(LogSpaceMMOBackend, Log,
		TEXT("Deposit %s (%s x%d) at %s, %.4f km above the nominal radius."),
		*Node.Key,
		*Node.ItemKey,
		Node.QuantityMax,
		*SurfacePosition.ToString(),
		(SurfacePosition.Kilometres - Planet.Centre.Kilometres).Size() - Planet.RadiusKilometres);

	// How the mesh was fitted, because a deposit that looks wrong looks wrong for one of two
	// reasons and they need opposite fixes. A pivot the code guessed badly shows up as a lift that
	// disagrees with the mesh's own extents; a deposit placed correctly but sitting in ground that
	// renders higher than the height function says shows up as a lift that looks entirely sensible.
	// Guessing between those from a screenshot cost a round already.
	if (const UStaticMesh* Mesh = Marker != nullptr ? Marker->GetStaticMesh() : nullptr)
	{
		const FBoxSphereBounds MeshBounds = Mesh->GetBounds();

		UE_LOG(LogSpaceMMOBackend, Log,
			TEXT("  mesh %s: extent %s, origin %s -> scale %.3f, lift %.1f cm."),
			*Mesh->GetName(),
			*MeshBounds.BoxExtent.ToCompactString(),
			*MeshBounds.Origin.ToCompactString(),
			FDepositPlacement::UniformScale(MeshBounds.BoxExtent),
			FDepositPlacement::BaseLift(
				MeshBounds.Origin,
				MeshBounds.BoxExtent,
				FDepositPlacement::UniformScale(MeshBounds.BoxExtent)));
	}
}

void ASpaceMMODepositActor::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const UWorld* World = GetWorld();

	const USpaceMMORenderOriginSubsystem* Origin =
		World != nullptr ? World->GetSubsystem<USpaceMMORenderOriginSubsystem>() : nullptr;

	// Drawn every frame, because a debug line with a zero lifetime lasts one. This is the only place
	// that runs often enough for a cross to actually be on screen when somebody looks.
	if (FParse::Param(FCommandLine::Get(), TEXT("ShowDepositAnchors")))
	{
		constexpr float ArmCentimetres = 60.0f;

		const FVector Side = FVector::CrossProduct(AnchorUp, FVector::ForwardVector).GetSafeNormal();
		const FVector Other = FVector::CrossProduct(AnchorUp, Side).GetSafeNormal();

		// Magenta because nothing else in this world is: the terrain renders in white and black,
		// and a marker that could be mistaken for scenery would answer nothing.
		DrawDebugLine(GetWorld(), AnchorWorldLocation - (Side * ArmCentimetres),
			AnchorWorldLocation + (Side * ArmCentimetres), FColor::Magenta, false, 0.0f, 0, 4.0f);

		DrawDebugLine(GetWorld(), AnchorWorldLocation - (Other * ArmCentimetres),
			AnchorWorldLocation + (Other * ArmCentimetres), FColor::Magenta, false, 0.0f, 0, 4.0f);

		// Straight up, so the cross is findable from above and its height is readable against the
		// ground beside it.
		DrawDebugLine(GetWorld(), AnchorWorldLocation,
			AnchorWorldLocation + (AnchorUp * ArmCentimetres * 3.0f),
			FColor::Magenta, false, 0.0f, 0, 4.0f);
	}

	// A deposit does not move, so between rebases its transform is already right. Only a change of
	// what "world zero" means can invalidate it.
	if (Origin == nullptr || Origin->GetRevision() == BuiltAtRevision)
	{
		return;
	}

	ApplyRenderTransform();
}

void ASpaceMMODepositActor::ApplyRenderTransform()
{
	const UWorld* World = GetWorld();

	const USpaceMMORenderOriginSubsystem* Origin =
		World != nullptr ? World->GetSubsystem<USpaceMMORenderOriginSubsystem>() : nullptr;

	if (Origin == nullptr || Marker == nullptr)
	{
		return;
	}

	// Stand along the ground's normal rather than along the radius. On a slope those differ, and it
	// is the difference between a deposit growing out of a hillside and one skewered through it at
	// whatever angle the planet's centre happens to be.
	const FVector Up = FPlanetTerrain::SurfaceNormal(Planet, Terrain, Node.Direction);

	// The cylinder's axis is Z, so aligning Z to the surface normal stands it up. Any rotation
	// about that axis is as good as any other for a rock.
	const FQuat Rotation = FQuat::FindBetweenNormals(FVector::UpVector, Up);

	// Measured from whatever mesh is actually attached rather than assumed from the engine
	// cylinder's dimensions. A model authored at any size, with its pivot at either its base or its
	// middle, lands the same way -- and neither of those is something an artist should have to know
	// about the code to get right.
	double Scale = 1.0;
	double Lift = 0.0;

	if (const UStaticMesh* Mesh = Marker->GetStaticMesh())
	{
		const FBoxSphereBounds MeshBounds = Mesh->GetBounds();

		Scale = FDepositPlacement::UniformScale(MeshBounds.BoxExtent);
		Lift = FDepositPlacement::BaseLift(MeshBounds.Origin, MeshBounds.BoxExtent, Scale);
	}

	const FVector Anchor = Origin->ToWorldLocation(SurfacePosition);

	SetActorLocation(Anchor + (Up * Lift));
	SetActorRotation(Rotation);

	Marker->SetWorldScale3D(FVector(Scale));

	// Kept for the debug draw, which happens in Tick. Drawing it here instead produced nothing
	// visible: this function runs at spawn and then only on a render-origin rebase, and a debug line
	// with a zero lifetime lasts exactly one frame. The cross was rendered once, during level load,
	// and never again.
	AnchorWorldLocation = Anchor;
	AnchorUp = Up;

	BuiltAtRevision = Origin->GetRevision();
}
