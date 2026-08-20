#include "SpaceMMOPreviewMarker.h"

#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "SpaceMMOAuthoringLog.h"
#include "SpaceMMOPreviewBody.h"

#if WITH_EDITOR
#include "LevelEditorViewport.h"
#endif

namespace
{
	/**
	 * How tall a marker stands, as a fraction of the body's radius.
	 *
	 * <strong>Not to scale, on purpose.</strong> A deposit is three metres tall on a 20 km planet,
	 * which on a 500 m preview is seven centimetres — invisible, and useless to grab. These are
	 * pins showing where something is, not models of what it is, and pretending otherwise would
	 * make the tool unusable in exchange for an accuracy nobody is reading off the screen.
	 */
	constexpr double PinHeightFraction = 0.012;

	FLinearColor ColourFor(const ESpaceMMOMarkerStatus Status)
	{
		switch (Status)
		{
		case ESpaceMMOMarkerStatus::Moved:
			return FLinearColor(1.0f, 0.75f, 0.1f);

		case ESpaceMMOMarkerStatus::Added:
			return FLinearColor(0.3f, 1.0f, 0.4f);

		case ESpaceMMOMarkerStatus::Removed:
			return FLinearColor(1.0f, 0.25f, 0.2f);

		default:
			return FLinearColor::White;
		}
	}
}

ASpaceMMOPreviewMarker::ASpaceMMOPreviewMarker()
{
	PrimaryActorTick.bCanEverTick = true;

	USceneComponent* const Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	SetRootComponent(Root);

	Pin = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Pin"));
	Pin->SetupAttachment(Root);
	Pin->SetCollisionEnabled(ECollisionEnabled::QueryOnly);

	Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
	Label->SetupAttachment(Root);
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void ASpaceMMOPreviewMarker::Setup(
	const FSpaceMMOAuthoredPlaceable& Entry, ASpaceMMOPreviewBody* const InBody)
{
	Original = Entry;
	PreviewBody = InBody;

	OriginalKey = Entry.Key;
	Key = Entry.Key;
	Body = Entry.BodyKey;
	SystemKey = Entry.SystemKey;

	Direction = Entry.Direction.GetSafeNormal();

	bIsDeposit = Entry.Kind == ESpaceMMOPlaceableKind::Deposit;
	bIsStation = !bIsDeposit;

	Item = Entry.Item;
	Skill = Entry.Skill;
	RequiredTool = Entry.RequiredTool;
	RequiredLevel = Entry.RequiredLevel;
	QuantityMax = Entry.QuantityMax;
	RespawnSeconds = Entry.RespawnSeconds;

	Name = Entry.Name;
	StationKind = Entry.StationKind;
	DockingRangeKilometres = Entry.DockingRangeKilometres;

	// A cylinder for something you mine and a cube for something you dock at: two silhouettes that
	// are still distinguishable at the angle a whole planet is being looked at from.
	const TCHAR* const MeshPath = bIsDeposit
		? TEXT("/Engine/BasicShapes/Cylinder.Cylinder")
		: TEXT("/Engine/BasicShapes/Cube.Cube");

	if (UStaticMesh* const Mesh = LoadObject<UStaticMesh>(nullptr, MeshPath))
	{
		Pin->SetStaticMesh(Mesh);
	}
	else
	{
		UE_LOG(LogSpaceMMOAuthoring, Warning,
			TEXT("No mesh at '%s'; '%s' has no marker to drag."), MeshPath, *Entry.Key);
	}

	SnapToSurface();
}

FSpaceMMOAuthoredPlaceable ASpaceMMOPreviewMarker::ToPlaceable() const
{
	FSpaceMMOAuthoredPlaceable Entry;

	Entry.Kind = bIsStation ? ESpaceMMOPlaceableKind::Station : ESpaceMMOPlaceableKind::Deposit;
	Entry.Key = Key;
	Entry.BodyKey = Body;
	Entry.SystemKey = SystemKey;
	Entry.Direction = Direction;

	Entry.Item = Item;
	Entry.Skill = Skill;
	Entry.RequiredTool = RequiredTool;
	Entry.RequiredLevel = RequiredLevel;
	Entry.QuantityMax = QuantityMax;
	Entry.RespawnSeconds = RespawnSeconds;

	Entry.Name = Name;
	Entry.StationKind = StationKind;
	Entry.DockingRangeKilometres = DockingRangeKilometres;

	return Entry;
}

ESpaceMMOMarkerStatus ASpaceMMOPreviewMarker::GetStatus() const
{
	if (bMarkedForRemoval)
	{
		return ESpaceMMOMarkerStatus::Removed;
	}

	if (bAdded)
	{
		return ESpaceMMOMarkerStatus::Added;
	}

	// Any edit at all counts as a change, not only a move: renaming a deposit or raising the level
	// it needs has to reach the file, and a status that only watched the gizmo would quietly drop
	// everything typed into the Details panel.
	const bool bSame =
		Key == OriginalKey
		&& Direction.Equals(Original.Direction.GetSafeNormal(), 1e-9)
		&& Item == Original.Item
		&& Skill == Original.Skill
		&& RequiredTool == Original.RequiredTool
		&& RequiredLevel == Original.RequiredLevel
		&& QuantityMax == Original.QuantityMax
		&& RespawnSeconds == Original.RespawnSeconds
		&& Name == Original.Name
		&& StationKind == Original.StationKind
		&& FMath::IsNearlyEqual(DockingRangeKilometres, Original.DockingRangeKilometres);

	return bSame ? ESpaceMMOMarkerStatus::Unchanged : ESpaceMMOMarkerStatus::Moved;
}

double ASpaceMMOPreviewMarker::MovedKilometres() const
{
	const FVector From = Original.Direction.GetSafeNormal();
	const FVector To = Direction.GetSafeNormal();

	if (From.IsNearlyZero() || To.IsNearlyZero())
	{
		return 0.0;
	}

	// Along the ground rather than through the planet, and in the kilometres of the body the game
	// draws — which is the distance somebody would actually have to walk.
	const double Angle = FMath::Acos(FMath::Clamp(FVector::DotProduct(From, To), -1.0, 1.0));

	return Angle * FSpaceMMOPreviewScale::DrawnRadiusKilometres();
}

void ASpaceMMOPreviewMarker::SetAdded()
{
	bAdded = true;

	RefreshLabel();
}

void ASpaceMMOPreviewMarker::SetRemoved(const bool bRemoved)
{
	bMarkedForRemoval = bRemoved;

	// Hidden rather than destroyed, because nothing has happened to the file yet and discarding
	// has to be able to bring it back exactly as it was.
	SetActorHiddenInGame(bRemoved);

#if WITH_EDITOR
	SetIsTemporarilyHiddenInEditor(bRemoved);
#endif

	RefreshLabel();
}

void ASpaceMMOPreviewMarker::SnapToSurface()
{
	const ASpaceMMOPreviewBody* const Preview = PreviewBody.Get();

	if (Preview == nullptr)
	{
		return;
	}

	if (Direction.IsNearlyZero())
	{
		// Standing at the centre names no point on the surface. Keeping the last good direction is
		// the only answer that does not author a deposit the validator will reject.
		Direction = FVector::UpVector;
	}

	Direction = Direction.GetSafeNormal();

	const FVector Location = Preview->SurfaceLocation(Direction);

	SetActorLocation(Location);
	SetActorRotation(FRotationMatrix::MakeFromZ(Direction).Rotator());

	const double Height = Preview->SurfaceRadiusCentimetres(Direction) * PinHeightFraction;

	if (Pin != nullptr)
	{
		// The engine shapes are a metre across with their pivot at the centre, so a pin of height H
		// is scaled by H/100 and lifted by half of itself to stand on the ground rather than half
		// buried in it.
		Pin->SetRelativeScale3D(FVector(Height * 0.004, Height * 0.004, Height * 0.01));
		Pin->SetRelativeLocation(FVector(0.0, 0.0, Height * 0.5));
	}

	if (Label != nullptr)
	{
		Label->SetRelativeLocation(FVector(0.0, 0.0, Height * 1.35));
		Label->SetWorldSize(static_cast<float>(Height * 0.45));
	}

	RefreshLabel();
}

void ASpaceMMOPreviewMarker::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

#if WITH_EDITOR
	// Turned to whoever is looking. A label fixed to the surface is edge-on and unreadable from
	// most of the places you would stand to look at a planet.
	if (Label != nullptr && GCurrentLevelEditingViewportClient != nullptr)
	{
		const FVector ToCamera =
			GCurrentLevelEditingViewportClient->GetViewLocation() - Label->GetComponentLocation();

		if (!ToCamera.IsNearlyZero())
		{
			// +X points AT the eye, not away from it.
			//
			// This was the other way round and every label rendered mirrored. The default text
			// material is two-sided, so a label facing away is not invisible -- it is legible and
			// backwards, which reads as a broken font rather than as a rotation.
			//
			// Settled by reading the engine rather than by trying the other sign:
			// TextRenderComponent.cpp:1118-1126 builds the glyph quads in the local YZ plane at
			// X = 0 with TangentZ (the surface normal) at +X, and advances characters along -Y.
			// A viewer on the +X side looking back along -X therefore has the text advancing to
			// their right, which is the way round it is meant to be read.
			Label->SetWorldRotation(FRotationMatrix::MakeFromX(ToCamera).Rotator());
		}
	}
#endif
}

#if WITH_EDITOR
void ASpaceMMOPreviewMarker::PostEditMove(const bool bFinished)
{
	Super::PostEditMove(bFinished);

	const ASpaceMMOPreviewBody* const Preview = PreviewBody.Get();

	if (Preview == nullptr)
	{
		return;
	}

	const FVector Dragged = Preview->DirectionOf(GetActorLocation());

	if (Dragged.IsNearlyZero())
	{
		return;
	}

	Direction = Dragged;

	// Back onto the ground along the new direction, every frame of the drag rather than only at the
	// end. Snapping only when the gizmo is released would let the marker be dragged away from the
	// surface and look like it stayed there, which is the state that cannot be authored.
	SnapToSurface();
}
#endif

void ASpaceMMOPreviewMarker::RefreshLabel()
{
	if (Label == nullptr)
	{
		return;
	}

	const ESpaceMMOMarkerStatus Status = GetStatus();

	Label->SetText(FText::FromString(Key));
	Label->SetTextRenderColor(ColourFor(Status).ToFColor(true));
}
