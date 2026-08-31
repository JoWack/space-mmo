#include "SpaceMMOCharacterPawn.h"

#include "Animation/AnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/SpringArmComponent.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "SpaceMMOLog.h"
#include "SpaceMMOBoarding.h"
#include "SpaceMMOPlanetActor.h"
#include "SpaceMMOShipPawn.h"
#include "SpaceMMOPlanetTerrain.h"
#include "SpaceMMORenderOrigin.h"
#include "SpaceMMOViewSubsystem.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/**
	 * Prints where the character is, where its mesh is drawn, and what is looking at it.
	 *
	 * <strong>Written because three plausible causes were proposed for one symptom and all three
	 * were wrong.</strong> The character does not stay centred while moving, which can be the actor
	 * moving away from the camera, the pose drifting away from the actor, or the view being
	 * attached to something else entirely — and no amount of reasoning from a screenshot separates
	 * them. This prints the three numbers that do.
	 *
	 * A console variable rather than a command-line flag, for two reasons: this machine mangles
	 * Unreal arguments, and a running game reads its command line once, which is exactly the trap
	 * that has already cost two rounds today. Toggle it with `SpaceMMO.LogCharacterDraw 1`.
	 */
	static TAutoConsoleVariable<int32> CVarLogCharacterDraw(
		TEXT("SpaceMMO.LogCharacterDraw"),
		1,
		TEXT("Log the character's actor, mesh and camera positions once a second."),
		ECVF_Default);
}

ASpaceMMOCharacterPawn::ASpaceMMOCharacterPawn()
{
	PrimaryActorTick.bCanEverTick = true;

	bReplicates = true;

	// Off for the same reason as the ship: Unreal replicates world transforms, and world
	// transforms are not comparable between clients that rebase independently.
	SetReplicateMovement(false);

	CharacterRoot = CreateDefaultSubobject<USceneComponent>(TEXT("CharacterRoot"));
	SetRootComponent(CharacterRoot);

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(CharacterRoot);

	// Position is owned by the walk model and ground contact, not by Chaos. Leaving collision on
	// would let the solver fight the authoritative position and win intermittently.
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaceholderMesh(
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));

	if (PlaceholderMesh.Succeeded())
	{
		Body->SetStaticMesh(PlaceholderMesh.Object);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BodyMaterial(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	if (BodyMaterial.Succeeded())
	{
		Body->SetMaterial(0, BodyMaterial.Object);
	}

	// The engine cylinder is 100 cm tall with its origin at the centre, so a person-sized one is
	// scaled to 1.8 and lifted 90 cm: it then spans 0 to 180 cm above the root, standing on its
	// feet with its head at 1.8 m and both cameras -- at 160 and 165 cm -- at eye height.
	//
	// The scale was 0.9, which is what this comment's arithmetic gives if the mesh is 200 cm tall.
	// It is 100, measured: half height 50. So the character was 90 cm tall with its feet 45 cm
	// above its own origin -- floating on the flat, and buried to the knees on anything sloped,
	// which is two symptoms of one number.
	Body->SetRelativeScale3D(FVector(0.4, 0.4, 1.8));
	Body->SetRelativeLocation(FVector(0.0, 0.0, 90.0));

	// The real body, sharing the placeholder's root and its convention: the pawn's origin is the
	// character's feet, so a model whose own origin is at its feet needs no offset at all.
	BodyMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("BodyMesh"));
	BodyMesh->SetupAttachment(CharacterRoot);

	// Collision off for the same reason the placeholder's is: position is owned by the walk model
	// and ground contact, and a solver given an opinion here would fight the authoritative one and
	// win intermittently.
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Animation is drawn, not simulated. Ticking the pose after physics keeps it out of the way of
	// anything that decides where the character actually is.
	BodyMesh->PrimaryComponentTick.TickGroup = TG_PostPhysics;

	// Hidden until a mesh is actually configured, so an unset path shows the tube rather than
	// nothing at all.
	BodyMesh->SetVisibility(false);

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(CharacterRoot);
	CameraBoom->TargetArmLength = 400.0f;
	CameraBoom->SetRelativeLocation(FVector(0.0, 0.0, 160.0));
	CameraBoom->bDoCollisionTest = false;

	// Follows the character's own orientation rather than the controller's, because the character's
	// up is the ground's normal and the controller has no idea where that points.
	CameraBoom->bUsePawnControlRotation = false;
	CameraBoom->bInheritPitch = true;
	CameraBoom->bInheritYaw = true;
	CameraBoom->bInheritRoll = true;

	ThirdPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("ThirdPersonCamera"));
	ThirdPersonCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);

	FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCamera->SetupAttachment(CharacterRoot);
	FirstPersonCamera->SetRelativeLocation(FVector(20.0, 0.0, 165.0));
	FirstPersonCamera->SetActive(false);
}

void ASpaceMMOCharacterPawn::BeginPlay()
{
	Super::BeginPlay();

	Navigation = FShipNavigation();
	Navigation.SystemPosition = FSystemCoordinate(StartingSystemPositionKilometres);
	Navigation.RenderOrigin = Navigation.SystemPosition;

	double StartX = 0.0;
	double StartY = 0.0;
	double StartZ = 0.0;

	if (FParse::Value(FCommandLine::Get(), TEXT("WalkStartX="), StartX)
		| FParse::Value(FCommandLine::Get(), TEXT("WalkStartY="), StartY)
		| FParse::Value(FCommandLine::Get(), TEXT("WalkStartZ="), StartZ))
	{
		Navigation.SystemPosition = FSystemCoordinate(FVector(StartX, StartY, StartZ));
		Navigation.RenderOrigin = Navigation.SystemPosition;
	}

	// Before the ground is resolved, because it changes what is drawn rather than where anything
	// is, and a warning about a missing model is worth having in the log above the first frame.
	ApplyCharacterMesh();
	ApplyCameraView();

	// Said out loud whether it is on or off, because the alternative has already happened: a console
	// variable resets every run, and a run where somebody forgot to set it produces a log with no
	// draw lines in it -- which is indistinguishable from a run where nothing went wrong. A
	// diagnostic that can silently not happen is worse than none, because it answers anyway, and
	// this line is what makes "off" a fact rather than an absence.
	UE_LOG(LogSpaceMMO, Log,
		TEXT("Character draw logging is %s (SpaceMMO.LogCharacterDraw)."),
		CVarLogCharacterDraw.GetValueOnGameThread() > 0 ? TEXT("ON") : TEXT("OFF"));

	// Picked up rather than reset. This pawn is spawned fresh every time somebody steps out of a
	// ship, and a camera that jumped back to its default on every trip would be worse than one that
	// never moved.
	if (const UGameInstance* const Game = GetGameInstance())
	{
		if (const USpaceMMOViewSubsystem* const Remembered =
			Game->GetSubsystem<USpaceMMOViewSubsystem>())
		{
			View.ArmTargetCentimetres = Remembered->CharacterArmCentimetres;
		}
	}

	if (CameraBoom != nullptr && View.ArmTargetCentimetres > 0.0)
	{
		CameraBoom->TargetArmLength = static_cast<float>(View.ArmTargetCentimetres);
	}

	// Resolve the ground before the first step so the character starts standing on the surface with
	// its up already correct, rather than upright in world space and snapping on frame one.
	ResolveSurface();

	WalkState.Rotation = FCharacterWalkModel::AlignToSurface(GetActorQuat(), SurfaceNormal);

	PublishRenderOrigin();
	ApplyWorldTransform();

	UE_LOG(LogSpaceMMO, Log, TEXT("Character ready at %s, up %s"),
		*Navigation.SystemPosition.ToString(), *SurfaceNormal.ToCompactString());
}

void ASpaceMMOCharacterPawn::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ASpaceMMOCharacterPawn, NetState);
}

void ASpaceMMOCharacterPawn::ResolveSurface()
{
	UWorld* World = GetWorld();

	if (World == nullptr)
	{
		return;
	}

	Gravity = FVector::ZeroVector;
	const bool bWasOnGround = bOnGround;

	bOnGround = false;
	SurfaceNormal = FVector::UpVector;

	FGroundContact Ground;

	for (TActorIterator<ASpaceMMOPlanetActor> It(World); It; ++It)
	{
		const FPlanetConfig& Planet = It->GetPlanetConfig();

		Gravity += FPlanetPhysics::GravityAcceleration(Planet, Navigation.SystemPosition);

		const FGroundContact Contact = FPlanetTerrain::ResolveContact(
			Planet,
			It->GetTerrainConfig(),
			Navigation.SystemPosition,
			WalkState.Velocity,
			StandingHeightKilometres,
			FPlanetTerrain::DefaultContactToleranceKilometres,
			bWasOnGround);

		// The normal is taken from whichever body is underfoot even when not touching it, so a
		// jumping character stays oriented to the ground it left rather than snapping upright.
		SurfaceNormal = Contact.SurfaceNormal;

		if (!Contact.bOnGround)
		{
			continue;
		}

		bOnGround = true;

		// Recorded, not applied. A building's floor gets its say first, and the higher of the two
		// wins; applying this here is what put a character inside a counter.
		Ground = Contact;

		// Raw facts, and nothing derived from them.
		//
		// Two attempts at computing "how far are the feet off the ground" were both wrong -- one
		// measured along world Z on a character aligned to a sphere, the other used a bounding
		// sphere radius as a stand-in for a cylinder's half height -- and each produced a confident
		// number that disagreed with the screen. So this prints what is actually known and leaves
		// the arithmetic to whoever is reading, which is the only version that cannot be subtly
		// wrong.
		if (!bReportedStandingGap)
		{
			bReportedStandingGap = true;

			// Whichever body is actually being drawn.
			//
			// It measured the placeholder unconditionally, which stopped being the right answer
			// the moment a character model could be configured: the tube is hidden then, and the
			// diagnostic would have gone on confidently describing a cylinder nobody can see while
			// the visible character floated or sank. A measurement that silently measures the
			// wrong thing is worse than none, because it answers anyway.
			const bool bUsingModel =
				BodyMesh != nullptr
				&& BodyMesh->GetSkeletalMeshAsset() != nullptr
				&& BodyMesh->IsVisible();

			const USceneComponent* const Drawn = bUsingModel
				? static_cast<USceneComponent*>(BodyMesh)
				: static_cast<USceneComponent*>(Body);

			if (Drawn != nullptr)
			{
				// Local bounds, so this is the mesh's own shape rather than where it happens to be
				// standing this frame -- the same reason the static version read the asset's
				// bounds rather than the component's world ones.
				const FBoxSphereBounds Local = bUsingModel
					? BodyMesh->GetSkeletalMeshAsset()->GetBounds()
					: (Body->GetStaticMesh() != nullptr
						? Body->GetStaticMesh()->GetBounds()
						: FBoxSphereBounds(ForceInit));

				const FVector Scale = Drawn->GetRelativeScale3D();

				UE_LOG(LogSpaceMMO, Log,
					TEXT("Standing gap: drawing the %s; standing height %.1f cm; relative Z %.1f cm, "
						"scale Z %.2f; mesh local origin Z %.1f, half height %.1f cm -> scaled half "
						"height %.1f cm, so mesh spans %.1f..%.1f cm above the root."),
					bUsingModel ? TEXT("character model") : TEXT("placeholder tube"),
					StandingHeightKilometres * 100000.0,
					Drawn->GetRelativeLocation().Z,
					Scale.Z,
					Local.Origin.Z,
					Local.BoxExtent.Z,
					Local.BoxExtent.Z * Scale.Z,
					Drawn->GetRelativeLocation().Z
						+ ((Local.Origin.Z - Local.BoxExtent.Z) * Scale.Z),
					Drawn->GetRelativeLocation().Z
						+ ((Local.Origin.Z + Local.BoxExtent.Z) * Scale.Z));
			}
		}
	}

	// Both answers are in. One of them is now applied.
	ResolveFooting(bWasOnGround, Ground);
}

void ASpaceMMOCharacterPawn::ResolveFooting(
	const bool bWasStanding, const FGroundContact& Ground)
{
	UWorld* const World = GetWorld();

	const USpaceMMORenderOriginSubsystem* const Origin =
		World != nullptr ? World->GetSubsystem<USpaceMMORenderOriginSubsystem>() : nullptr;

	const FVector Up = SurfaceNormal.GetSafeNormal();

	// Nothing to probe with. The ground still gets applied: refusing to answer must not also
	// discard the answer that was already arrived at.
	if (World == nullptr || Origin == nullptr || CollisionRadiusCentimetres <= 0.0
		|| Up.IsNearlyZero())
	{
		StandOnGround(Ground);

		return;
	}

	const double HalfHeight =
		FMath::Max(CollisionRadiusCentimetres, CharacterHeightCentimetres * 0.5);

	const FVector Feet = Origin->ToWorldLocation(Navigation.SystemPosition);

	// The probe starts with its lowest point above the feet rather than at them, so a character
	// resting exactly on a floor sweeps against it cleanly instead of beginning inside it. Ten
	// centimetres of headroom costs nothing in a building with four metre ceilings.
	const FVector From = Feet + Up * (HalfHeight + FloorProbeLiftCentimetres);
	const FVector To = Feet + Up * (HalfHeight - FloorProbeReachCentimetres);

	// Simple collision only, as everywhere else here: a mesh imported without it is invisible to
	// this, and a floor nobody can stand on looks exactly like a floor that is not there.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SpaceMMOCharacterFloor), false, this);

	FHitResult Hit;

	const bool bFound = World->SweepSingleByChannel(
		Hit,
		From,
		To,
		FRotationMatrix::MakeFromZ(Up).ToQuat(),
		ECC_Pawn,
		FCollisionShape::MakeCapsule(
			static_cast<float>(CollisionRadiusCentimetres), static_cast<float>(HalfHeight)),
		Params);

	// A probe that begins inside something says nothing about what is underfoot -- it reports the
	// way out of what it started in. ResolveBlocking is what pushes a character out of geometry;
	// footing waits for it to have done so rather than guessing a floor from a depenetration.
	if (!bFound || !Hit.bBlockingHit || Hit.bStartPenetrating)
	{
		StandOnGround(Ground);

		return;
	}

	const FVector FloorFeet = Hit.Location - Up * HalfHeight;

	const double GapCentimetres = FVector::DotProduct(Feet - FloorFeet, Up);

	const double SeparationSpeed = FVector::DotProduct(WalkState.Velocity, Up);

	if (!FCharacterWalkModel::StandsOn(
			Hit.ImpactNormal, Up, GapCentimetres, SeparationSpeed, bWasStanding))
	{
		StandOnGround(Ground);

		return;
	}

	// Both have hold. The higher one wins, which is the whole arbitration: a floor below the ground
	// is a floor the ground is standing on top of, and a floor above it is a building.
	//
	// Compared in world space, because that is the frame the sweep answered in. Converting the
	// ground's system coordinate across is one call; treating the sweep's centimetres as kilometres
	// would be a number that looks like a comparison and is not one.
	if (Ground.bOnGround
		&& FVector::DotProduct(Origin->ToWorldLocation(Ground.Position) - FloorFeet, Up) > 0.0)
	{
		StandOnGround(Ground);

		return;
	}

	Navigation.SystemPosition = FSystemCoordinate(
		Navigation.SystemPosition.Kilometres
		+ ((FloorFeet - Feet) / SpaceMMO::Coordinates::CentimetresPerKilometre));

	bOnGround = true;

	const bool bBegan = StoodOn.Get() != Hit.GetActor();

	StoodOn = Hit.GetActor();

	// Resolved along the character's own up rather than along the surface's normal, and the
	// difference matters on the stair ramp. Up on a planet is the direction away from its centre --
	// that substitution is what makes the whole walk model work -- and a character leaning 26
	// degrees because the floor under one foot is a slope would be a body that disagrees with every
	// other thing standing in the building.
	WalkState = FCharacterWalkModel::ResolveBlockingHit(WalkState, Up, 0.0);

	if (bBegan && CVarLogCharacterDraw.GetValueOnGameThread() > 0)
	{
		// Where the thing being stood on changes, and not otherwise.
		//
		// The first version of this logged where standing began and asked bOnGround, which
		// ResolveSurface clears at the top of every call -- so it fired on almost every one, twice a
		// frame, 8622 times in a session, and said nothing by saying it constantly. Tracking what is
		// underfoot rather than whether anything is says the same thing in one line per floor.
		UE_LOG(LogSpaceMMO, Log,
			TEXT("Standing on %s, %.1f cm %s the feet, normal %s."),
			*GetNameSafe(Hit.GetActor()),
			FMath::Abs(GapCentimetres),
			GapCentimetres >= 0.0 ? TEXT("below") : TEXT("above"),
			*Hit.ImpactNormal.ToCompactString());
	}
}

void ASpaceMMOCharacterPawn::StandOnGround(const FGroundContact& Ground)
{
	StoodOn = nullptr;

	if (!Ground.bOnGround)
	{
		return;
	}

	Navigation.SystemPosition = Ground.Position;
	WalkState.Velocity = Ground.Velocity;
}

void ASpaceMMOCharacterPawn::ResolveBlocking(const FSystemCoordinate& From)
{
	UWorld* const World = GetWorld();

	const USpaceMMORenderOriginSubsystem* const Origin =
		World != nullptr ? World->GetSubsystem<USpaceMMORenderOriginSubsystem>() : nullptr;

	if (World == nullptr || Origin == nullptr || CollisionRadiusCentimetres <= 0.0)
	{
		return;
	}

	// The sweep happens in Unreal world space, which is the render origin's frame -- and both ends
	// are converted in the same frame, so a rebase between frames cannot skew the sweep. That is
	// the whole reason this is a query and not a physics body: there is no state to carry across a
	// rebase.
	const FVector Start = Origin->ToWorldLocation(From);
	const FVector End = Origin->ToWorldLocation(Navigation.SystemPosition);

	if (FVector::DistSquared(Start, End) < UE_DOUBLE_SMALL_NUMBER)
	{
		// Standing still asks nothing of the world, so it neither begins a contact nor ends one. A
		// character resting against a hull is still resting against it, and counting those seconds
		// as time spent failing to walk is how a diagnostic learns to cry wolf.
		return;
	}

	// A capsule standing on the character's feet, which is where the pawn's origin is.
	const double HalfHeight =
		FMath::Max(CollisionRadiusCentimetres, CharacterHeightCentimetres * 0.5);

	const FVector Up = SurfaceNormal.GetSafeNormal().IsNearlyZero()
		? FVector::UpVector
		: SurfaceNormal.GetSafeNormal();

	const FVector Lift = Up * HalfHeight;

	const FQuat Orientation = FRotationMatrix::MakeFromZ(Up).ToQuat();

	const FCollisionShape Capsule = FCollisionShape::MakeCapsule(
		static_cast<float>(CollisionRadiusCentimetres), static_cast<float>(HalfHeight));

	// Simple collision only, which is what leaving bTraceComplex false selects -- the engine picks
	// exactly one of the two, never both. An imported mesh with no simple collision is invisible to
	// this sweep however solid it looks, which is why placement warns about one.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SpaceMMOCharacterBlocking), false, this);

	// Capsule centres rather than feet, because that is what a sweep moves.
	FVector Here = Start + Lift;
	FVector Want = End + Lift;

	const AActor* Touched = nullptr;
	FVector LastNormal = FVector::ZeroVector;

	// Three passes. The first stops the character against whatever it walked into; the second
	// spends the rest of the step along that surface, which is the difference between sliding and
	// being pinned; the third settles the corner where sliding runs into something else. Past that
	// the character is in a crevice and stopping is the honest answer.
	for (int32 Pass = 0; Pass < 3; ++Pass)
	{
		FHitResult Hit;

		const bool bHit = World->SweepSingleByChannel(
			Hit, Here, Want, Orientation, ECC_Pawn, Capsule, Params);

		if (!bHit || !Hit.bBlockingHit)
		{
			Here = Want;

			break;
		}

		Touched = Hit.GetActor();

		// A sweep that begins inside something has no surface to report crossing, so its impact
		// normal means nothing; what it does know is the shortest way out. Take that and try the
		// step again rather than resolving against a normal that was never measured.
		if (Hit.bStartPenetrating)
		{
			LastNormal = Hit.Normal.GetSafeNormal();

			const FVector Out =
				LastNormal * FCharacterWalkModel::SeparationCentimetres(Hit.PenetrationDepth);

			Here += Out;
			Want += Out;

			continue;
		}

		LastNormal = Hit.ImpactNormal;

		// Stop a hair clear of the contact, then spend what is left of the step along the surface.
		//
		// <strong>Keeping the remainder is the fix.</strong> Clamping to the contact and stopping
		// there leaves a character in continuous contact moving only by the separation push, which
		// measured at six centimetres a second against a walk of six hundred and read in the game as
		// the controls having died. The arithmetic is in the walk model, where it has tests.
		const FVector Contact =
			Hit.Location + LastNormal * FCharacterWalkModel::SeparationCentimetres(0.0);

		Want = Contact + FCharacterWalkModel::SlideDeltaCentimetres(Want - Hit.Location, LastNormal);
		Here = Contact;

		WalkState =
			FCharacterWalkModel::ResolveBlockingHit(WalkState, LastNormal, Hit.PenetrationDepth);
	}

	// Applied as a delta rather than by converting back through the render origin, which has no
	// reverse and would not want one: a difference between two points in the same frame means the
	// same thing whatever the origin happens to be, so this cannot be skewed by a rebase.
	const FVector CorrectionKilometres =
		((Here - Lift) - End) / SpaceMMO::Coordinates::CentimetresPerKilometre;

	Navigation.SystemPosition =
		FSystemCoordinate(Navigation.SystemPosition.Kilometres + CorrectionKilometres);

	ReportBlocking(Touched, LastNormal, (End - Start).Size());
}

void ASpaceMMOCharacterPawn::ReportBlocking(
	const AActor* const Touched, const FVector& Normal, const double WantedCentimetres)
{
	const UWorld* const World = GetWorld();

	if (World == nullptr)
	{
		return;
	}

	const bool bLogging = CVarLogCharacterDraw.GetValueOnGameThread() > 0;

	const AActor* const Was = BlockedBy.Get();

	if (Touched == Was)
	{
		// The same contact, still going. Keep adding up what is being asked of it.
		if (Touched != nullptr)
		{
			BlockedWantedCentimetres += WantedCentimetres;
		}

		return;
	}

	if (Was != nullptr)
	{
		const double Seconds = FMath::Max(0.0, World->GetTimeSeconds() - BlockedSinceSeconds);

		const double GotCentimetres =
			(Navigation.SystemPosition.Kilometres - BlockedFrom.Kilometres).Size()
			* SpaceMMO::Coordinates::CentimetresPerKilometre;

		if (bLogging)
		{
			UE_LOG(LogSpaceMMO, Log,
				TEXT("Cleared %s after %.1f s: asked for %.0f cm along it and got %.0f."),
				*GetNameSafe(Was), Seconds, BlockedWantedCentimetres, GotCentimetres);
		}

		// The line that would have found the pinning bug in one reading rather than in a script over
		// 1794 of them. Asked against got, because a contact going nowhere is only a fault if the
		// character was trying to go somewhere: standing beside a ship for ten seconds asks for
		// nothing and must not read as being stuck.
		//
		// A metre of asking is past any stumble, and a tenth of it arriving is far below anything
		// sliding produces -- a step almost parallel to a surface keeps nearly all of its length,
		// which SpaceMMO.Walk.SpendsTheRestOfTheStepAlongTheWall pins down.
		if (BlockedWantedCentimetres > 100.0 && GotCentimetres < BlockedWantedCentimetres * 0.1)
		{
			UE_LOG(LogSpaceMMO, Warning,
				TEXT("Pinned against %s for %.1f s: asked to move %.0f cm and moved %.0f. That is ")
				TEXT("stuck, not sliding."),
				*GetNameSafe(Was), Seconds, BlockedWantedCentimetres, GotCentimetres);
		}
	}

	BlockedBy = Touched;
	BlockedSinceSeconds = World->GetTimeSeconds();
	BlockedFrom = Navigation.SystemPosition;
	BlockedWantedCentimetres = Touched != nullptr ? WantedCentimetres : 0.0;

	if (Touched != nullptr && bLogging)
	{
		// Named, because a mesh with no collision on it is silently intangible and looks exactly
		// like broken movement code -- which ADR-0013 lists as an accepted cost of this approach.
		UE_LOG(LogSpaceMMO, Log,
			TEXT("Blocked by %s, normal %s. Watching how far it lets the character get."),
			*GetNameSafe(Touched), *Normal.ToCompactString());
	}
}

void ASpaceMMOCharacterPawn::SimulateStep(const double DeltaSeconds)
{
	// Ground first: the walk model needs to know which way is up and whether it has anything to
	// push against before it can decide what this step does.
	ResolveSurface();

	WalkState = FCharacterWalkModel::Step(
		WalkState, PendingInput, WalkConfig, SurfaceNormal, Gravity, bOnGround, DeltaSeconds);

	const FSystemCoordinate Before = Navigation.SystemPosition;

	Navigation.SystemPosition = FSystemCoordinate(
		Navigation.SystemPosition.Kilometres
		+ FCharacterWalkModel::PositionDeltaKilometres(WalkState, DeltaSeconds));

	// Anything solid in the way, before the ground gets a say. A character stopped by a wall has
	// not moved, so resolving the ground at the position they were prevented from reaching would
	// stand them on the wrong piece of terrain for a frame.
	ResolveBlocking(Before);

	// And again after moving, so the step that would have driven the character into a hill is
	// undone in the same frame rather than being visible for one.
	ResolveSurface();

	if (!Navigation.SystemPosition.IsWithinLocalSpaceOf(Navigation.RenderOrigin))
	{
		Navigation.RenderOrigin = Navigation.SystemPosition;
		++Navigation.RebaseCount;
	}
}

void ASpaceMMOCharacterPawn::PublishNetState()
{
	NetState.SystemPosition = Navigation.SystemPosition;
	NetState.Velocity = WalkState.Velocity;
	NetState.Rotation = WalkState.Rotation;
	NetState.bOnGround = bOnGround;

	const UWorld* World = GetWorld();

	NetState.ServerTimeSeconds = World != nullptr ? World->GetTimeSeconds() : 0.0;
}

bool ASpaceMMOCharacterPawn::ServerSendWalkInput_Validate(FWalkInput Input)
{
	// Nothing to reject; Sanitised clamps every axis, so no value here can be hostile.
	return true;
}

void ASpaceMMOCharacterPawn::ServerSendWalkInput_Implementation(FWalkInput Input)
{
	PendingInput = Input.Sanitised();
}

void ASpaceMMOCharacterPawn::ReconcileWithServer(const double DeltaSeconds)
{
	if (NetState.ServerTimeSeconds <= LastAppliedServerTime)
	{
		return;
	}

	LastAppliedServerTime = NetState.ServerTimeSeconds;

	Navigation.SystemPosition = FShipFlightModel::ReconcilePosition(
		Navigation.SystemPosition, NetState.SystemPosition, Reconciliation, DeltaSeconds);

	WalkState.Velocity = NetState.Velocity;
}

void ASpaceMMOCharacterPawn::FollowServerState(const double DeltaSeconds)
{
	const UWorld* World = GetWorld();
	const double Now = World != nullptr ? World->GetTimeSeconds() : 0.0;

	if (NetState.ServerTimeSeconds > LastAppliedServerTime)
	{
		LastAppliedServerTime = NetState.ServerTimeSeconds;
		LastNetStateReceivedAt = Now;
	}

	const FSystemCoordinate Target = FShipFlightModel::Extrapolate(
		NetState.SystemPosition, NetState.Velocity, Now - LastNetStateReceivedAt);

	Navigation.SystemPosition = FShipFlightModel::ReconcilePosition(
		Navigation.SystemPosition, Target, Reconciliation, DeltaSeconds);

	// Drawn, not simulated. Orientation comes from the server, which already aligned it to the
	// ground the character is actually standing on.
	WalkState.Rotation = NetState.Rotation;
	WalkState.Velocity = NetState.Velocity;
	bOnGround = NetState.bOnGround;
}

void ASpaceMMOCharacterPawn::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (HasAuthority())
	{
		SimulateStep(DeltaSeconds);
		PublishNetState();
	}
	else if (IsLocallyControlled())
	{
		SimulateStep(DeltaSeconds);
		ServerSendWalkInput(PendingInput);
		ReconcileWithServer(DeltaSeconds);
	}
	else
	{
		FollowServerState(DeltaSeconds);
	}

	if (IsLocallyControlled())
	{
		PublishRenderOrigin();

		// Drawn, never simulated. design-bible.md section 8: the camera is a client concern and must
		// never affect server-side validation, which is why this sits outside the step above.
		ApplyView(DeltaSeconds);
	}

	ApplyWorldTransform();

	// Cleared each frame because the legacy input path only calls the handlers while a key is held.
	PendingInput.Move = FVector2D::ZeroVector;
	PendingInput.Turn = 0.0;

	// Drawn, not simulated: this turns the model and touches nothing the server has an opinion on.
	UpdateMeshFacing(DeltaSeconds);

	DiagnosticSeconds += DeltaSeconds;

	DrawDiagnosticSeconds += DeltaSeconds;

	if (CVarLogCharacterDraw.GetValueOnGameThread() > 0)
	{
		// Sampled every frame, reported every second.
		//
		// The first version of this only measured on the second, and a once-a-second sample cannot
		// see a transient: a character that swings wide during a turn and comes back would report
		// the same steady number as one that never moved. What is being complained about is a
		// moment, so the diagnostic has to watch every frame and keep the worst one.
		TrackHowFarOffCentre();

		if (DrawDiagnosticSeconds >= 1.0)
		{
			DrawDiagnosticSeconds = 0.0;

			ReportHowItIsDrawn();

			WorstHorizontalDegrees = 0.0;
			WorstVerticalDegrees = 0.0;
			WorstDrawnFromActorCentimetres = 0.0;
		}
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("LogApproach")) && DiagnosticSeconds >= 1.0)
	{
		DiagnosticSeconds = 0.0;

		const ASpaceMMOShipPawn* Nearest = nullptr;
		double NearestKm = TNumericLimits<double>::Max();

		for (TActorIterator<ASpaceMMOShipPawn> It(GetWorld()); It; ++It)
		{
			const double Distance =
				(Navigation.SystemPosition.Kilometres - It->GetSystemPosition().Kilometres).Size();

			if (Distance < NearestKm)
			{
				NearestKm = Distance;
				Nearest = *It;
			}
		}

		UE_LOG(LogSpaceMMO, Log,
			TEXT("ONFOOT: sys %s | %s | ship %.1f m away, drawn %.1f m | speed %.1f m/s"),
			*Navigation.SystemPosition.ToString(),
			bOnGround ? TEXT("GROUNDED") : TEXT("AIRBORNE"),
			NearestKm * 1000.0,
			Nearest != nullptr
				? (Nearest->GetActorLocation() - GetActorLocation()).Size() / 100.0
				: -1.0,
			GetSpeedMetresPerSecond());
	}

	if (bShowWalkDebug && GEngine != nullptr && IsLocallyControlled())
	{
		GEngine->AddOnScreenDebugMessage(
			10, 0.0f, FColor::Green,
			FString::Printf(
				TEXT("On foot %s | %.1f m/s | %s"),
				*Navigation.SystemPosition.ToString(),
				GetSpeedMetresPerSecond(),
				bOnGround ? TEXT("GROUNDED") : TEXT("AIRBORNE")));

		GEngine->AddOnScreenDebugMessage(
			11, 0.0f, FColor::Emerald,
			FString::Printf(TEXT("Up %s"), *SurfaceNormal.ToCompactString()));
	}
}

void ASpaceMMOCharacterPawn::PublishRenderOrigin()
{
	const UWorld* World = GetWorld();

	if (!IsLocallyControlled())
	{
		return;
	}

	// Rebase from wherever the character actually is, before publishing.
	//
	// The rebase test used to live only in the locally-simulated movement path, so a position that
	// arrived any other way — a spawn, a teleport off a ship, replication from the server — left
	// the render origin wherever it had been. On disembarking, this pawn begins play at the system
	// origin, publishes that, and the whole world is then drawn relative to a point forty
	// kilometres from the player. Static meshes survive it; a kilometres-wide generated mesh does
	// not, which is what "there is no ground under me" turned out to be.
	//
	// Asking the question here means it is asked once per frame against the position that is
	// actually being rendered, whatever produced it.
	if (!Navigation.SystemPosition.IsWithinLocalSpaceOf(Navigation.RenderOrigin))
	{
		Navigation.RenderOrigin = Navigation.SystemPosition;
		++Navigation.RebaseCount;

		UE_LOG(LogSpaceMMO, Log,
			TEXT("REBASE %d: character sys %s"),
			Navigation.RebaseCount,
			*Navigation.SystemPosition.ToString());
	}

	if (USpaceMMORenderOriginSubsystem* Origin =
		World != nullptr ? World->GetSubsystem<USpaceMMORenderOriginSubsystem>() : nullptr)
	{
		Origin->SetRenderOrigin(Navigation.RenderOrigin);
	}
}

void ASpaceMMOCharacterPawn::ApplyWorldTransform()
{
	const UWorld* World = GetWorld();

	const USpaceMMORenderOriginSubsystem* Origin =
		World != nullptr ? World->GetSubsystem<USpaceMMORenderOriginSubsystem>() : nullptr;

	// Against the subsystem's origin, not this pawn's own, so remote characters are drawn in the
	// frame of reference this client is actually rendering in.
	const FVector Location = Origin != nullptr
		? Origin->ToWorldLocation(Navigation.SystemPosition)
		: Navigation.RenderLocationCentimetres();

	SetActorLocationAndRotation(Location, WalkState.Rotation);
}

void ASpaceMMOCharacterPawn::SetSystemPosition(const FSystemCoordinate& NewPosition)
{
	Navigation.SystemPosition = NewPosition;
	Navigation.RenderOrigin = NewPosition;
	++Navigation.RebaseCount;

	ResolveSurface();

	WalkState.Rotation = FCharacterWalkModel::AlignToSurface(WalkState.Rotation, SurfaceNormal);

	PublishRenderOrigin();
	ApplyWorldTransform();
}

void ASpaceMMOCharacterPawn::RequestEmbark()
{
	ServerEmbark();
}

void ASpaceMMOCharacterPawn::ServerEmbark_Implementation()
{
	AController* OwningController = GetController();
	UWorld* World = GetWorld();

	if (OwningController == nullptr || World == nullptr)
	{
		return;
	}

	// Nearest in range, chosen here rather than named by the client. Nearest rather than first,
	// so parking two ships side by side does not board whichever happened to spawn earlier.
	ASpaceMMOShipPawn* Best = nullptr;
	double BestDistance = TNumericLimits<double>::Max();

	for (TActorIterator<ASpaceMMOShipPawn> It(World); It; ++It)
	{
		ASpaceMMOShipPawn* Ship = *It;

		// Somebody else is flying it.
		if (Ship == nullptr || Ship->GetController() != nullptr)
		{
			continue;
		}

		const double Distance =
			(Navigation.SystemPosition.Kilometres - Ship->GetSystemPosition().Kilometres).Size();

		if (Distance < BestDistance
			&& FBoarding::CanEmbark(Navigation.SystemPosition, Ship->GetSystemPosition()))
		{
			Best = Ship;
			BestDistance = Distance;
		}
	}

	if (Best == nullptr)
	{
		UE_LOG(LogSpaceMMO, Log, TEXT("No ship within boarding range."));

		return;
	}

	UE_LOG(LogSpaceMMO, Log,
		TEXT("Boarded a ship from %.3f km away."), BestDistance);

	OwningController->Possess(Best);

	// Destroyed only after possession has moved on. Destroying first would leave the controller
	// briefly possessing nothing, and anything that runs in that window has no pawn to ask.
	Destroy();
}

void ASpaceMMOCharacterPawn::ToggleCameraView()
{
	bFirstPerson = !bFirstPerson;

	ApplyCameraView();
}

void ASpaceMMOCharacterPawn::ApplyCameraView()
{
	if (ThirdPersonCamera != nullptr)
	{
		ThirdPersonCamera->SetActive(!bFirstPerson);
	}

	if (FirstPersonCamera != nullptr)
	{
		FirstPersonCamera->SetActive(bFirstPerson);
	}

	// The body goes away in first person: the camera is inside the character's head, and a model
	// drawn there fills the screen with the underside of a jaw and the inside of a torso.
	//
	// Only ever this pawn's own body, and only on the machine looking through its eyes. bFirstPerson
	// is set by a key press, which only the locally controlled pawn receives, so another player's
	// character is never hidden by their choice of view.
	const bool bBodyVisible = !bFirstPerson;

	if (BodyMesh != nullptr && BodyMesh->GetSkeletalMeshAsset() != nullptr)
	{
		BodyMesh->SetVisibility(bBodyVisible);
	}
	else if (Body != nullptr)
	{
		Body->SetVisibility(bBodyVisible);
	}
}

void ASpaceMMOCharacterPawn::TrackHowFarOffCentre()
{
	const APlayerController* const Viewer = Cast<APlayerController>(GetController());

	if (Viewer == nullptr)
	{
		return;
	}

	FVector CameraLocation = FVector::ZeroVector;
	FRotator CameraRotation = FRotator::ZeroRotator;

	Viewer->GetPlayerViewPoint(CameraLocation, CameraRotation);

	// <strong>The bone, not the actor.</strong> The previous version of this measured
	// GetActorLocation, which is bolted to the camera boom and therefore reads zero degrees off
	// centre forever, whatever is on screen. It duly proved the actor was centred, which was never
	// the question: what a player looks at is the skinned mesh, and a pose can walk that anywhere
	// while the actor it hangs from does not move at all.
	const FVector Drawn = BodyMesh != nullptr
		? BodyMesh->GetBoneLocation(TEXT("pelvis"), EBoneSpaces::WorldSpace)
		: GetActorLocation();

	const FVector ToCharacter = Drawn - CameraLocation;

	if (ToCharacter.IsNearlyZero())
	{
		return;
	}

	// Split into the camera's own axes, because left-of-centre and low-in-frame are different
	// complaints and one unsigned angle cannot tell them apart -- which is what the first version
	// of this got wrong.
	const FMatrix CameraFrame = FRotationMatrix(CameraRotation);

	const double Forward = FVector::DotProduct(ToCharacter, CameraFrame.GetUnitAxis(EAxis::X));
	const double Right = FVector::DotProduct(ToCharacter, CameraFrame.GetUnitAxis(EAxis::Y));
	const double Up = FVector::DotProduct(ToCharacter, CameraFrame.GetUnitAxis(EAxis::Z));

	if (FMath::Abs(Forward) < UE_DOUBLE_SMALL_NUMBER)
	{
		return;
	}

	WorstHorizontalDegrees = FMath::Max(
		WorstHorizontalDegrees, FMath::Abs(FMath::RadiansToDegrees(FMath::Atan2(Right, Forward))));

	WorstVerticalDegrees = FMath::Max(
		WorstVerticalDegrees, FMath::Abs(FMath::RadiansToDegrees(FMath::Atan2(Up, Forward))));

	// And the same difference in plain centimetres, between where the character is and where it is
	// drawn. A sawtooth here -- climbing, then snapping back -- is a pose accumulating motion and
	// releasing it, and it is the shape the complaint describes.
	const double DrawnFromActor = (Drawn - GetActorLocation()).Size();

	WorstDrawnFromActorCentimetres = FMath::Max(WorstDrawnFromActorCentimetres, DrawnFromActor);

	LastDrawnFromActorCentimetres = DrawnFromActor;
}

void ASpaceMMOCharacterPawn::ReportHowItIsDrawn() const
{
	const FVector Actor = GetActorLocation();

	// Where the pose actually puts the body, in the mesh's own frame. This is the number that
	// separates "the mesh is drifting" from "the actor is": root motion left in a pose walks the
	// root bone away from the component while the component itself never moves, so a component
	// transform will happily report everything is fine.
	FVector RootBone = FVector::ZeroVector;
	FVector PelvisBone = FVector::ZeroVector;

	FString AnimReport = TEXT("<no anim instance>");

	if (BodyMesh != nullptr)
	{
		RootBone = BodyMesh->GetBoneLocation(TEXT("root"), EBoneSpaces::ComponentSpace);
		PelvisBone = BodyMesh->GetBoneLocation(TEXT("pelvis"), EBoneSpaces::ComponentSpace);

		if (const UAnimInstance* const Anim = BodyMesh->GetAnimInstance())
		{
			// The mode the running instance actually has, not the one the asset was saved with.
			// Whether a dropdown took effect is exactly the sort of thing worth reading off the
			// built object rather than believing.
			AnimReport = FString::Printf(
				TEXT("%s, root motion mode %d"),
				*GetNameSafe(Anim->GetClass()),
				static_cast<int32>(Anim->RootMotionMode.GetValue()));
		}
	}

	// What is looking at it, and from where. A view target that is not this pawn would put the
	// character anywhere on screen at all, and nothing else here would look wrong.
	FString ViewReport = TEXT("<no view target>");

	if (const APlayerController* const Viewer = Cast<APlayerController>(GetController()))
	{
		const AActor* const ViewTarget = Viewer->GetViewTarget();

		FVector CameraLocation = FVector::ZeroVector;
		FRotator CameraRotation = FRotator::ZeroRotator;

		Viewer->GetPlayerViewPoint(CameraLocation, CameraRotation);

		// How far off the middle of the screen the character is, as an angle. Zero is dead centre,
		// and it is the number the complaint is actually about.
		const FVector ToCharacter = Actor - CameraLocation;

		const double OffAxisDegrees = ToCharacter.IsNearlyZero()
			? 0.0
			: FMath::RadiansToDegrees(
				FMath::Acos(
					FMath::Clamp(
						FVector::DotProduct(
							ToCharacter.GetSafeNormal(), CameraRotation.Vector()),
						-1.0,
						1.0)));

		ViewReport = FString::Printf(
			TEXT("view target %s, camera %.0f cm away, feet %.1f deg off axis; "
				"worst this second: %.1f deg sideways, %.1f deg vertical; "
				"body drawn %.0f cm from the actor now, %.0f cm at worst"),
			*GetNameSafe(ViewTarget),
			ToCharacter.Size(),
			OffAxisDegrees,
			WorstHorizontalDegrees,
			WorstVerticalDegrees,
			LastDrawnFromActorCentimetres,
			WorstDrawnFromActorCentimetres);
	}

	// The four values the animation graph is actually handed, printed beside what it drew. Which
	// sample plays is entirely a function of these, so a wrong pose is either a wrong number here
	// or a wrong asset there -- and there is no way to tell which from a screenshot.
	UE_LOG(LogSpaceMMO, Log,
		TEXT("DRAW: speed %.2f m/s, direction %.1f deg (body facing %.1f, residual %.1f), "
			"vertical %.2f m/s, %s | "
			"actor %s | mesh relative %s | root bone (component) %s | pelvis %s | %s | %s"),
		GetGroundSpeedMetresPerSecond(),
		GetMoveDirectionDegrees(),
		MeshFacingDegrees,
		GetAnimationDirectionDegrees(),
		GetVerticalSpeedMetresPerSecond(),
		bOnGround ? TEXT("GROUNDED") : TEXT("AIRBORNE"),
		*Actor.ToCompactString(),
		BodyMesh != nullptr ? *BodyMesh->GetRelativeLocation().ToCompactString() : TEXT("<none>"),
		*RootBone.ToCompactString(),
		*PelvisBone.ToCompactString(),
		*AnimReport,
		*ViewReport);
}

double ASpaceMMOCharacterPawn::TurnTowards(
	const double CurrentDegrees, const double DesiredDegrees, const double MaxStepDegrees)
{
	// Normalised into -180..180 first, so the difference between them is the short way round by
	// construction rather than by luck. Turning from 170 to -170 is twenty degrees, and a version
	// of this that subtracted the raw values would spin the character the other three hundred and
	// forty every time somebody ran backwards.
	const double Delta =
		FRotator::NormalizeAxis(DesiredDegrees) - FRotator::NormalizeAxis(CurrentDegrees);

	const double Shortest = FRotator::NormalizeAxis(Delta);

	if (MaxStepDegrees <= 0.0 || FMath::Abs(Shortest) <= MaxStepDegrees)
	{
		// Close enough to land on it exactly. Stepping past and oscillating around the target is
		// the other way this goes wrong, and it reads as a body that jitters while running.
		return FRotator::NormalizeAxis(DesiredDegrees);
	}

	return FRotator::NormalizeAxis(
		FRotator::NormalizeAxis(CurrentDegrees) + (FMath::Sign(Shortest) * MaxStepDegrees));
}

void ASpaceMMOCharacterPawn::UpdateMeshFacing(const double DeltaSeconds)
{
	if (BodyMesh == nullptr)
	{
		return;
	}

	if (!bCharacterFacesTravel)
	{
		MeshFacingDegrees = 0.0;
		BodyMesh->SetRelativeRotation(CharacterMeshRotation);

		return;
	}

	// Only while actually going somewhere. Below the threshold there is no direction of travel to
	// face, and reading one out of a velocity that is basically noise would have the body twitching
	// round while the character stands still.
	if (GetGroundSpeedMetresPerSecond() >= CharacterFacingSpeedThresholdMetresPerSecond)
	{
		MeshFacingDegrees = TurnTowards(
			MeshFacingDegrees,
			GetMoveDirectionDegrees(),
			CharacterTurnRateDegreesPerSecond * DeltaSeconds);
	}

	// Added to the authored offset rather than replacing it: that value is how the model sits on
	// the pawn at all, and losing it would leave the character facing ninety degrees off whichever
	// way they walked.
	BodyMesh->SetRelativeRotation(
		FRotator(
			CharacterMeshRotation.Pitch,
			CharacterMeshRotation.Yaw + MeshFacingDegrees,
			CharacterMeshRotation.Roll));
}

double ASpaceMMOCharacterPawn::UniformScaleForHeight(
	const double AuthoredHeightCentimetres, const double TargetCentimetres)
{
	// Either being unusable leaves the model exactly as authored. Dividing by a zero height, or
	// scaling to a zero target, both end with a character that is not there at all -- which reads
	// as a failure to spawn rather than as a number nobody set.
	if (AuthoredHeightCentimetres <= UE_DOUBLE_SMALL_NUMBER || TargetCentimetres <= 0.0)
	{
		return 1.0;
	}

	return TargetCentimetres / AuthoredHeightCentimetres;
}

void ASpaceMMOCharacterPawn::ApplyCharacterMesh()
{
	if (BodyMesh == nullptr)
	{
		return;
	}

	// Said on every path, including the one that does nothing. An unset model and code that never
	// ran produce the same evidence -- a tube -- and only one of them is somebody's mistake.
	UE_LOG(LogSpaceMMO, Log,
		TEXT("Character model configured as '%s', animation as '%s'."),
		CharacterMesh.IsNull() ? TEXT("<unset>") : *CharacterMesh.ToString(),
		CharacterAnimClass.IsNull() ? TEXT("<unset>") : *CharacterAnimClass.ToString());

	if (CharacterMesh.IsNull())
	{
		return;
	}

	USkeletalMesh* const Mesh = Cast<USkeletalMesh>(CharacterMesh.TryLoad());

	// Named-but-wrong is worth saying out loud: from the outside a typo and an unset path look
	// identical, and one of them is a mistake somebody wants telling about.
	if (Mesh == nullptr)
	{
		UE_LOG(LogSpaceMMO, Warning,
			TEXT("Character model '%s' did not load; the placeholder stays."),
			*CharacterMesh.ToString());

		return;
	}

	// Measured off the model, not assumed. The bounds are the reference pose's, which is the only
	// thing that knows what scale somebody exported at.
	const double AuthoredHeight = Mesh->GetBounds().BoxExtent.Z * 2.0;
	const double Scale = UniformScaleForHeight(AuthoredHeight, CharacterHeightCentimetres);

	BodyMesh->SetSkeletalMeshAsset(Mesh);
	BodyMesh->SetRelativeRotation(CharacterMeshRotation);
	BodyMesh->SetRelativeLocation(CharacterMeshOffset);
	BodyMesh->SetRelativeScale3D(FVector(Scale));
	BodyMesh->SetVisibility(true);

	// The tube has done its job. Hidden rather than destroyed, so a model that fails to load on a
	// later run still has something to fall back to.
	if (Body != nullptr)
	{
		Body->SetVisibility(false);
	}

	if (!CharacterAnimClass.IsNull())
	{
		UClass* const AnimClass = CharacterAnimClass.TryLoadClass<UAnimInstance>();

		if (AnimClass == nullptr)
		{
			// A model in its bind pose is a T-posed statue sliding around the planet, which is a
			// working state on the way to an animated one -- but not one to arrive at silently.
			UE_LOG(LogSpaceMMO, Warning,
				TEXT("Animation blueprint '%s' did not load; '%s' stands in its bind pose."),
				*CharacterAnimClass.ToString(), *Mesh->GetName());
		}
		else
		{
			BodyMesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
			BodyMesh->SetAnimInstanceClass(AnimClass);
		}
	}

	// The authored height is named as well as the applied scale, because a model that needs a large
	// multiplier is a model exported at the wrong scale, and that is worth being told rather than
	// silently corrected forever.
	UE_LOG(LogSpaceMMO, Log,
		TEXT("Character drawing as '%s': authored %.1f cm, scaled %.3f to stand %.1f cm; "
			"rotated %s, offset %s."),
		*Mesh->GetName(),
		AuthoredHeight,
		Scale,
		AuthoredHeight * Scale,
		*CharacterMeshRotation.ToCompactString(),
		*CharacterMeshOffset.ToCompactString());
}

void ASpaceMMOCharacterPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (PlayerInputComponent == nullptr)
	{
		return;
	}

	PlayerInputComponent->BindAxis(
		TEXT("WalkForward"), this, &ASpaceMMOCharacterPawn::MoveForward);
	PlayerInputComponent->BindAxis(
		TEXT("WalkRight"), this, &ASpaceMMOCharacterPawn::MoveRight);
	PlayerInputComponent->BindAxis(
		TEXT("WalkTurn"), this, &ASpaceMMOCharacterPawn::TurnRight);
	PlayerInputComponent->BindAxis(
		TEXT("WalkLook"), this, &ASpaceMMOCharacterPawn::LookUp);

	PlayerInputComponent->BindAction(
		TEXT("WalkJump"), IE_Pressed, this, &ASpaceMMOCharacterPawn::StartJump);
	PlayerInputComponent->BindAction(
		TEXT("WalkJump"), IE_Released, this, &ASpaceMMOCharacterPawn::StopJump);
	PlayerInputComponent->BindAction(
		TEXT("ToggleCamera"), IE_Pressed, this, &ASpaceMMOCharacterPawn::ToggleCameraView);

	PlayerInputComponent->BindAction(
		TEXT("Board"), IE_Pressed, this, &ASpaceMMOCharacterPawn::RequestEmbark);

	PlayerInputComponent->BindAction(
		TEXT("WalkSprint"), IE_Pressed, this, &ASpaceMMOCharacterPawn::StartSprint);
	PlayerInputComponent->BindAction(
		TEXT("WalkSprint"), IE_Released, this, &ASpaceMMOCharacterPawn::StopSprint);

	PlayerInputComponent->BindAxis(TEXT("ViewZoom"), this, &ASpaceMMOCharacterPawn::ZoomView);

	PlayerInputComponent->BindAction(
		TEXT("OrbitView"), IE_Pressed, this, &ASpaceMMOCharacterPawn::StartOrbit);
	PlayerInputComponent->BindAction(
		TEXT("OrbitView"), IE_Released, this, &ASpaceMMOCharacterPawn::StopOrbit);
}

void ASpaceMMOCharacterPawn::StartSprint() { PendingInput.bSprint = true; }
void ASpaceMMOCharacterPawn::StopSprint() { PendingInput.bSprint = false; }

void ASpaceMMOCharacterPawn::StartOrbit() { View.bOrbiting = true; }
void ASpaceMMOCharacterPawn::StopOrbit() { View.bOrbiting = false; }

void ASpaceMMOCharacterPawn::ZoomView(const float Value)
{
	if (FMath::IsNearlyZero(Value))
	{
		return;
	}

	View.Wheel(Value, ZoomNearCentimetres, ZoomFarCentimetres, ZoomStepFraction);

	// Remembered off the pawn, because this pawn does not survive being boarded: stepping into a
	// ship destroys it and stepping out spawns another. A zoom kept here would reset on every trip.
	if (const UGameInstance* const Game = GetGameInstance())
	{
		if (USpaceMMOViewSubsystem* const Remembered =
			Game->GetSubsystem<USpaceMMOViewSubsystem>())
		{
			Remembered->CharacterArmCentimetres = View.ArmTargetCentimetres;
		}
	}
}

void ASpaceMMOCharacterPawn::ApplyView(const double DeltaSeconds)
{
	View.Advance(DeltaSeconds, OrbitReturnSeconds);

	if (CameraBoom == nullptr)
	{
		return;
	}

	// Eased rather than set, so a fast scroll glides out instead of jumping. The rate is the
	// reciprocal of the time asked for, which is what FInterpTo's "speed" means.
	CameraBoom->TargetArmLength = static_cast<float>(FMath::FInterpTo(
		static_cast<double>(CameraBoom->TargetArmLength),
		View.ArmTargetCentimetres,
		DeltaSeconds,
		ZoomSmoothingSeconds > 0.0 ? 1.0 / ZoomSmoothingSeconds : 0.0));

	// The orbit rides on top of the ordinary look pitch rather than replacing it, so letting go of
	// the key returns the swing and leaves the player looking where they were looking.
	CameraBoom->SetRelativeRotation(
		FRotator(ViewPitchDegrees + View.Orbit.Pitch, View.Orbit.Yaw, 0.0));

	// SocketOffset rather than TargetOffset: this one is applied in the arm's rotated frame, so the
	// shoulder stays over the shoulder through a pitch or a swing. TargetOffset is world space and
	// would slide around the character as the view turned.
	CameraBoom->SocketOffset = FThirdPersonView::ShoulderAt(
		ShoulderOffset,
		CameraBoom->TargetArmLength,
		ShoulderReferenceArmCentimetres);
}

void ASpaceMMOCharacterPawn::MoveForward(const float Value)
{
	PendingInput.Move.X = Value;
}

void ASpaceMMOCharacterPawn::MoveRight(const float Value)
{
	PendingInput.Move.Y = Value;
}

void ASpaceMMOCharacterPawn::TurnRight(const float Value)
{
	// While the orbit key is held the mouse swings the camera and the character stands its ground.
	// Turn is simulated by the server, so this has to stop the input reaching PendingInput rather
	// than be undone afterwards -- a turn sent and then cancelled is a turn the server performed.
	if (View.bOrbiting)
	{
		View.Swing(Value * OrbitSensitivityDegrees, 0.0, MaxLookPitchDegrees);

		PendingInput.Turn = 0.0;

		return;
	}

	PendingInput.Turn = Value;
}

void ASpaceMMOCharacterPawn::LookUp(const float Value)
{
	if (FMath::IsNearlyZero(Value))
	{
		return;
	}

	// While the orbit key is held the mouse swings the camera instead. Kept apart from the pitch
	// below so that releasing the key gives back the swing and not the look.
	if (View.bOrbiting)
	{
		View.Swing(0.0, Value * OrbitSensitivityDegrees, MaxLookPitchDegrees);

		return;
	}

	// Clamped rather than wrapped. Past vertical the view rolls over and every subsequent movement
	// reads inverted, which is indistinguishable from broken controls and has no recovery that is
	// not itself surprising.
	ViewPitchDegrees = FMath::Clamp(
		ViewPitchDegrees + (Value * LookSensitivityDegrees),
		-MaxLookPitchDegrees,
		MaxLookPitchDegrees);

	// On the boom, which already inherits the character's own orientation -- and the character's up
	// is the ground's normal, not the world's. Adding pitch to a controller rotation instead would
	// be pitching about an axis that means nothing on a sphere.
	if (CameraBoom != nullptr)
	{
		CameraBoom->SetRelativeRotation(FRotator(ViewPitchDegrees, 0.0, 0.0));
	}

	// The first-person camera is on the root rather than the boom, so it has to be tilted too or
	// looking up works in one view and silently does nothing in the other.
	if (FirstPersonCamera != nullptr)
	{
		FirstPersonCamera->SetRelativeRotation(FRotator(ViewPitchDegrees, 0.0, 0.0));
	}
}

void ASpaceMMOCharacterPawn::StartJump()
{
	PendingInput.bJump = true;
}

void ASpaceMMOCharacterPawn::StopJump()
{
	// Held rather than edge-triggered, so the jump survives until a frame actually consumes it —
	// pressing jump between two ticks would otherwise be silently dropped.
	PendingInput.bJump = false;
}
