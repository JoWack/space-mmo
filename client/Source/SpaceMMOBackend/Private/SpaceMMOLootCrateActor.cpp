#include "SpaceMMOLootCrateActor.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "SpaceMMOBackendLog.h"
#include "SpaceMMOCharacterPawn.h"
#include "SpaceMMOGlow.h"
#include "SpaceMMOPlanet.h"
#include "SpaceMMOPlanetTerrain.h"
#include "SpaceMMORenderOrigin.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** The engine's cube and cylinder are 100 units across. */
	constexpr double CrateMeshUnits = 100.0;

	/** The crate's dark grey, lit like the ground around it. */
	const FLinearColor CrateBodyColour(0.035f, 0.038f, 0.042f, 1.0f);

	/** Amber, bright enough to glow: the band and the beam. Linear, and over 1. */
	const FLinearColor CrateAmber(5.0f, 2.1f, 0.25f, 1.0f);

	/** The beam's thickness: thin, a line in the sky rather than a pillar. */
	constexpr double CrateBeamCentimetres = 6.0;

	/** The band round the crate's middle. */
	constexpr double CrateBandCentimetres = 7.0;

	UStaticMeshComponent* MakeCratePart(AActor* Owner, const TCHAR* Name, UStaticMesh* Mesh, USceneComponent* Parent)
	{
		UStaticMeshComponent* Part = Owner->CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Part->SetStaticMesh(Mesh);
		Part->SetupAttachment(Parent);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetGenerateOverlapEvents(false);
		Part->SetMobility(EComponentMobility::Movable);

		return Part;
	}
}

ASpaceMMOLootCrateActor::ASpaceMMOLootCrateActor()
{
	PrimaryActorTick.bCanEverTick = true;

	bReplicates = true;

	// Each machine places it from its own render origin; a replicated transform would be the server's frame.
	SetReplicateMovement(false);

	// Few, and placed by system position rather than by the server's world location, which is the server's frame:
	// relevancy measured there would be measured in the wrong place.
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(2.0f);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	RootComponent = Root;

	// Local Z is up, the ground's normal; the root stands on the ground.
	Crate = MakeCratePart(this, TEXT("Crate"), Cube.Object, Root);
	Crate->SetRelativeLocation(FVector(0.0, 0.0, CrateMetres * 50.0));
	Crate->SetRelativeScale3D(FVector(CrateMetres * 100.0 / CrateMeshUnits));

	Band = MakeCratePart(this, TEXT("Band"), Cube.Object, Root);
	Band->SetRelativeLocation(FVector(0.0, 0.0, CrateMetres * 50.0));
	Band->SetRelativeScale3D(FVector(
		(CrateMetres * 100.0 + 2.0) / CrateMeshUnits, (CrateMetres * 100.0 + 2.0) / CrateMeshUnits, CrateBandCentimetres / CrateMeshUnits));
	Band->SetCastShadow(false);

	Beam = MakeCratePart(this, TEXT("Beam"), Cylinder.Object, Root);
	Beam->SetRelativeLocation(FVector(0.0, 0.0, CrateMetres * 100.0 + BeamMetres * 50.0));
	Beam->SetRelativeScale3D(FVector(CrateBeamCentimetres / CrateMeshUnits, CrateBeamCentimetres / CrateMeshUnits, BeamMetres * 100.0 / CrateMeshUnits));
	Beam->SetCastShadow(false);
}

void ASpaceMMOLootCrateActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ASpaceMMOLootCrateActor, ContainerId);
	DOREPLIFETIME(ASpaceMMOLootCrateActor, DroppedBy);
	DOREPLIFETIME(ASpaceMMOLootCrateActor, PositionKilometres);
	DOREPLIFETIME(ASpaceMMOLootCrateActor, Up);
	DOREPLIFETIME(ASpaceMMOLootCrateActor, ExpiresServerSeconds);
	DOREPLIFETIME(ASpaceMMOLootCrateActor, Items);
}

float ASpaceMMOLootCrateActor::BeamStrength(const double SecondsLeft)
{
	return static_cast<float>(FMath::Clamp(SecondsLeft / BeamFadeSeconds, 0.0, 1.0));
}

FSystemCoordinate ASpaceMMOLootCrateActor::BesideTheBody(
	const FPlanetConfig& Planet, const FPlanetTerrainConfig& Terrain, const FSystemCoordinate& Feet, const FVector& Right)
{
	// A metre to the right, then dropped or lifted onto the ground there: beside the body on a slope as on the flat.
	const FVector Beside = Feet.Kilometres + Right.GetSafeNormal() * BesideMetres / 1000.0;

	return FPlanetTerrain::SurfacePosition(Planet, Terrain, Beside - Planet.Centre.Kilometres);
}

void ASpaceMMOLootCrateActor::BeginPlay()
{
	Super::BeginPlay();

	// The prototyping kit's glow, as the bolt's: its colour goes on the part, where the material reads it (SpaceMMOGlow).
	SpaceMMO::Glow::Apply(Band, CrateAmber);
	SpaceMMO::Glow::Apply(Beam, CrateAmber);

	if (UMaterialInterface* Shape = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
	{
		UMaterialInstanceDynamic* Body = UMaterialInstanceDynamic::Create(Shape, this);
		Body->SetVectorParameterValue(TEXT("Color"), CrateBodyColour);
		Crate->SetMaterial(0, Body);
	}

	Place();
}

void ASpaceMMOLootCrateActor::SetContainer(
	const FBackendLootContainer& Container, const FVector& InUp, const double ServerNowSeconds)
{
	ContainerId = Container.Id;
	DroppedBy = Container.DroppedBy;
	PositionKilometres = Container.PositionKilometres;
	Up = InUp.GetSafeNormal().IsNearlyZero() ? FVector::UpVector : InUp.GetSafeNormal();
	ExpiresServerSeconds = ServerNowSeconds + Container.SecondsLeft;

	SetItems(Container.Items);

	bPlaced = false;
	Place();
}

void ASpaceMMOLootCrateActor::SetItems(const TArray<FBackendLootItem>& InItems)
{
	Items = InItems;

	OnRep_Items();

	// Sent now rather than at the next of its two updates a second: a window open on it should not show a stack
	// for half a second after somebody else took it.
	if (HasAuthority())
	{
		ForceNetUpdate();
	}
}

void ASpaceMMOLootCrateActor::OnRep_Items()
{
	++ItemsRevision;
}

void ASpaceMMOLootCrateActor::OnRep_Placement()
{
	bPlaced = false;
	Place();
}

double ASpaceMMOLootCrateActor::ServerNowSeconds() const
{
	const UWorld* World = GetWorld();

	if (World == nullptr)
	{
		return 0.0;
	}

	const AGameStateBase* GameState = World->GetGameState();

	return GameState != nullptr ? GameState->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
}

double ASpaceMMOLootCrateActor::SecondsLeft() const
{
	return FMath::Max(0.0, ExpiresServerSeconds - ServerNowSeconds());
}

void ASpaceMMOLootCrateActor::Place()
{
	const UWorld* World = GetWorld();

	const USpaceMMORenderOriginSubsystem* Origin =
		World != nullptr ? World->GetSubsystem<USpaceMMORenderOriginSubsystem>() : nullptr;

	if (Origin == nullptr || ContainerId == 0)
	{
		return;
	}

	if (bPlaced && PlacedAtRevision == Origin->GetRevision())
	{
		return;
	}

	SetActorLocation(Origin->ToWorldLocation(FSystemCoordinate(PositionKilometres)));
	SetActorRotation(FQuat::FindBetweenNormals(FVector::UpVector, Up));

	if (!bPlaced && !bSaidPlaced)
	{
		bSaidPlaced = true;

		// Said once where it lands on each machine, so a crate nobody can see is told from one never placed.
		UE_LOG(LogSpaceMMOBackend, Log, TEXT("Loot: crate %lld (dropped by %s, %d stacks) placed at %s km, %.0f s left%s."),
			ContainerId, *DroppedBy, Items.Num(), *PositionKilometres.ToString(), SecondsLeft(),
			HasAuthority() ? TEXT(", server") : TEXT(""));
	}

	PlacedAtRevision = Origin->GetRevision();
	bPlaced = true;
}

void ASpaceMMOLootCrateActor::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

#if !UE_BUILD_SHIPPING
	// A look sample stays beside its character, at the time it was given.
	if (const ASpaceMMOCharacterPawn* Beside = LookBeside.Get())
	{
		PositionKilometres = Beside->GetSystemPosition().Kilometres
			+ (Beside->GetActorForwardVector() * LookAheadMetres + Beside->GetActorRightVector() * LookSideMetres) / 1000.0;
		Up = Beside->GetSurfaceNormal().GetSafeNormal();
		ExpiresServerSeconds = ServerNowSeconds() + LookLasting;
		bPlaced = false;
	}
#endif

	Place();

	const double Left = SecondsLeft();

	// Gone when its time is up: the API has already stopped handing anything out of it.
	if (HasAuthority() && ContainerId != 0 && ExpiresServerSeconds > 0.0 && Left <= 0.0)
	{
		UE_LOG(LogSpaceMMOBackend, Log, TEXT("Loot: crate %lld is past its time; what was left in it is destroyed."),
			ContainerId);

		Destroy();

		return;
	}

	// The beam fades over its last minute, so a corpse run can see how long is left from a distance.
	const float Strength = BeamStrength(Left);

	if (!FMath::IsNearlyEqual(Strength, ShownBeamStrength, 0.01f))
	{
		ShownBeamStrength = Strength;

		Beam->SetVisibility(Strength > 0.0f);

		// Dimmer and, through its alpha, fainter.
		SpaceMMO::Glow::SetColour(Beam, FLinearColor(CrateAmber.R * Strength, CrateAmber.G * Strength, CrateAmber.B * Strength, Strength));

		// Thinner as well as dimmer: a glow material may not dim to nothing against a bright sky.
		const double Width = CrateBeamCentimetres * (0.35 + 0.65 * Strength) / CrateMeshUnits;
		Beam->SetRelativeScale3D(FVector(Width, Width, BeamMetres * 100.0 / CrateMeshUnits));
	}
}

#if !UE_BUILD_SHIPPING
void ASpaceMMOLootCrateActor::ShowSampleForLook(
	const ASpaceMMOCharacterPawn* Beside, const double AheadMetres, const double SideMetres, const double Lasting)
{
	LookBeside = Beside;
	LookAheadMetres = AheadMetres;
	LookSideMetres = SideMetres;
	LookLasting = Lasting;

	FBackendLootContainer Sample;
	Sample.Id = 1;
	Sample.DroppedBy = TEXT("Ayla1204");
	Sample.SecondsLeft = Lasting;

	FBackendLootItem Ore;
	Ore.ItemDefId = 2;
	Ore.ItemKey = TEXT("ferrite_ore");
	Ore.Name = TEXT("Ferrite Ore");
	Ore.ShownStyle = EBackendItemStyle::Terran;
	Ore.Quantity = 5;
	Ore.VolumeM3 = 0.4;

	FBackendLootItem Plate;
	Plate.ItemDefId = 6;
	Plate.ItemKey = TEXT("ferrite_plate");
	Plate.Name = TEXT("Structural Plate");
	Plate.Style = EBackendItemStyle::Martian;
	Plate.ShownStyle = EBackendItemStyle::Martian;
	Plate.Quantity = 3;
	Plate.VolumeM3 = 0.2;

	Sample.Items = {Ore, Plate};

	FVector UpHere = FVector::UpVector;

	if (Beside != nullptr)
	{
		UpHere = Beside->GetSurfaceNormal().GetSafeNormal();
		Sample.PositionKilometres = Beside->GetSystemPosition().Kilometres
			+ (Beside->GetActorForwardVector() * AheadMetres + Beside->GetActorRightVector() * SideMetres) / 1000.0;
	}

	SetContainer(Sample, UpHere, ServerNowSeconds());
}
#endif
