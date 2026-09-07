#include "SpaceMMODockingComponent.h"

#include "Components/InputComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "SpaceMMOBackendClient.h"
#include "SpaceMMOBackendLog.h"
#include "SpaceMMOBoarding.h"
#include "SpaceMMOCharacterPawn.h"
#include "SpaceMMOPlayerController.h"
#include "SpaceMMORenderOrigin.h"
#include "SpaceMMOShipPawn.h"
#include "SpaceMMOStationActor.h"

USpaceMMODockingComponent::USpaceMMODockingComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

void USpaceMMODockingComponent::BeginPlay()
{
	Super::BeginPlay();

	if (APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		BindInput(Pawn->InputComponent);

		// And again whenever the pawn is possessed. A ship is boarded, left and boarded again, and
		// each possession builds a fresh input component -- so binding once at BeginPlay leaves the
		// key attached to a dead one. That is why G worked twice and then did nothing at all, with
		// no message, because no handler ran to produce one.
		Pawn->ReceiveRestartedDelegate.AddDynamic(
			this, &USpaceMMODockingComponent::HandlePawnRestarted);
	}
}

void USpaceMMODockingComponent::HandlePawnRestarted(APawn* Pawn)
{
	if (Pawn != nullptr)
	{
		BindInput(Pawn->InputComponent);
	}
}

void USpaceMMODockingComponent::BindInput(UInputComponent* InputComponent)
{
	// Compared against the component actually bound, not a flag. Possession replaces the input
	// component, and a flag cannot tell "already bound" from "bound to something that is gone".
	if (InputComponent == nullptr || BoundInput.Get() == InputComponent)
	{
		return;
	}

	const bool bRebind = BoundInput.IsValid() || bHasBoundOnce;

	InputComponent->BindAction(
		TEXT("Dock"), IE_Pressed, this, &USpaceMMODockingComponent::RequestToggleDock);

	BoundInput = InputComponent;
	bHasBoundOnce = true;

	// The input component is named, because "bound" and "bound to the one the player is driving"
	// are different facts and the log could not tell them apart. Two possessions of one ship pawn
	// produced one line, and the second boarding's dead key looked exactly like a working one.
	UE_LOG(LogSpaceMMOBackend, Log, TEXT("Dock key %s on %s (input %s)."),
		bRebind ? TEXT("re-bound") : TEXT("bound"),
		*GetNameSafe(GetOwner()),
		*GetNameSafe(InputComponent));
}

void USpaceMMODockingComponent::RequestToggleDock()
{
	// <strong>Says the key ran, before anything can decide it did nothing.</strong> Every refusal
	// below this reached the player as an on-screen message and nothing else, so "G does nothing"
	// covered a dead binding, a station out of range and an unidentified character equally -- and
	// the log could not separate them. This line is what makes a silent key a fact rather than an
	// absence (task 156).
	UE_LOG(LogSpaceMMOBackend, Log, TEXT("Dock key pressed on %s."), *GetNameSafe(GetOwner()));

	// Carries nothing. Which station, and whether we are near it, are the server's to decide.
	ServerToggleDock();
}

void USpaceMMODockingComponent::ServerToggleDock_Implementation()
{
	const AActor* Owner = GetOwner();

	if (Owner == nullptr || !Owner->HasAuthority())
	{
		return;
	}

	const UWorld* World = GetWorld();

	const UGameInstance* GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;

	USpaceMMOBackendClient* Backend =
		GameInstance != nullptr
			? GameInstance->GetSubsystem<USpaceMMOBackendClient>()
			: nullptr;

	if (Backend == nullptr || CharacterId == 0)
	{
		UE_LOG(LogSpaceMMOBackend, Warning,
			TEXT("Dock: no character bound to %s; nothing to dock."), *GetNameSafe(Owner));

		// On screen as well as in the log. This branch is the one that actually fired, and
		// logging alone made a pressed key indistinguishable from an unbound one — which is
		// exactly how it was reported: "it just does nothing".
		ClientDockResult(TEXT("Not identified yet; cannot dock."), false);

		return;
	}

	if (DockedStationId != 0)
	{
		Backend->UndockAsServer(CharacterId);
		DockedStationId = 0;

		// So a pawn possessed after this one is not handed a station this player has just left.
		if (const APawn* Pawn = Cast<APawn>(GetOwner()))
		{
			if (ASpaceMMOPlayerController* Controller =
				Cast<ASpaceMMOPlayerController>(Pawn->GetController()))
			{
				Controller->NoteDockedStation(0);
			}
		}

		ClientDockResult(TEXT("Undocked."), true);

		return;
	}

	const ASpaceMMOStationActor* Station = FindStationInRange();

	if (Station == nullptr)
	{
		// Logged as well as shown. An on-screen message is the first thing lost behind a panel, and
		// a refusal nobody sees is indistinguishable from a key that did nothing -- which is exactly
		// how the dead binding above was reported. The nearest station and the distance say whether
		// the answer was right.
		double NearestKilometres = 0.0;

		const ASpaceMMOStationActor* Nearest = NearestStation(NearestKilometres);

		if (Nearest != nullptr)
		{
			UE_LOG(LogSpaceMMOBackend, Log,
				TEXT("Nothing in docking range: nearest is %s at %.0f m, and it docks within %.0f m."),
				*Nearest->GetStation().Name,
				NearestKilometres * 1000.0,
				Nearest->GetStation().DockingRangeKilometres * 1000.0);
		}
		else
		{
			UE_LOG(LogSpaceMMOBackend, Log,
				TEXT("Nothing in docking range, and no station exists in this world to be near."));
		}

		ClientDockResult(TEXT("Nothing in docking range."), false);

		return;
	}

	DockedStationId = Station->GetStation().Id;

	Backend->DockAsServer(CharacterId, DockedStationId);

	// Read before anything is destroyed. StowShipAt possesses a new pawn and destroys this
	// component's owner, so every field of this object -- and this pointer -- is gone afterwards.
	const int32 StationId = DockedStationId;
	const int32 DockingCharacterId = CharacterId;
	const FString StationName = Station->GetStation().Name;

	const ASpaceMMOShipPawn* Ship = Cast<ASpaceMMOShipPawn>(GetOwner());

	if (Ship == nullptr)
	{
		ClientDockResult(FString::Printf(TEXT("Docked at %s."), *StationName), true);

		return;
	}

	const int64 HullItemInstanceId = Ship->HullItemInstanceId;

	FSystemCoordinate Ashore;

	const bool bCanStepAshore =
		Station->GroundPositionBeside(DockArrivalOffsetKilometres, 0.0, Ashore);

	// Sent before the swap, deliberately: this is an RPC on a component that is about to be
	// destroyed with its owner, and one sent afterwards has nothing to send it from.
	ClientDockResult(
		bCanStepAshore
			? FString::Printf(TEXT("Docked at %s. Your ship is in the hangar."), *StationName)
			: FString::Printf(
				TEXT("Docked at %s. There is nowhere to step out, so your ship stays alongside."),
				*StationName),
		true);

	if (bCanStepAshore && StowShipAt(*Station, Ashore))
	{
		// Only once the pawn has actually gone, and off locals rather than off this component:
		// StowShipAt destroyed the owner, and every field of this object went with it.
		//
		// The record follows the world rather than leading it, which is the whole point of the
		// task -- a hull recorded in a hangar it is standing outside of is what is being removed.
		Backend->StowAsServer(DockingCharacterId, HullItemInstanceId, StationId);
	}
}

void USpaceMMODockingComponent::AdoptDocking(const int32 StationId)
{
	DockedStationId = StationId;

	// Reset with it. The new pawn has never been range-checked, and inheriting a partly elapsed
	// interval from the pawn that was destroyed would check it at an arbitrary moment.
	SecondsSinceRangeCheck = 0.0;
	ResumeStationId = 0;
}

bool USpaceMMODockingComponent::StowShipAt(
	const ASpaceMMOStationActor& Station, const FSystemCoordinate& Ashore)
{
	ASpaceMMOShipPawn* Ship = Cast<ASpaceMMOShipPawn>(GetOwner());

	if (Ship == nullptr)
	{
		return false;
	}

	const int32 StationId = DockedStationId;

	// Facing the station, because you have just walked off a ship into it and the alternative is
	// arriving with your back to the only thing there is to do here.
	const FVector Up = Ship->SurfaceUpHere();

	const FQuat Facing = FBoarding::StepOutRotation(
		Up, (Station.GetSystemPosition().Kilometres - Ashore.Kilometres).GetSafeNormal());

	// Held before the swap: GetController() answers null the moment possession moves on, and the
	// station has to be handed to the controller as well as to the pawn.
	ASpaceMMOPlayerController* Controller =
		Cast<ASpaceMMOPlayerController>(Ship->GetController());

	// The same swap stepping out performs, and the same code performing it (task 153 point 1). The
	// pawn being destroyed is the one the player possesses, so the possession has to move first.
	ASpaceMMOCharacterPawn* AshorePilot = Ship->StepPilotOut(Ashore, Facing);

	if (AshorePilot == nullptr)
	{
		UE_LOG(LogSpaceMMOBackend, Warning,
			TEXT("Docked at %s but no pilot could be put ashore; the ship stays alongside."),
			*Station.GetStation().Name);

		return false;
	}

	// Onto the pawn the player is now in, before the old one goes. A freshly spawned component
	// holds zero, which is "not docked" -- so without this the key would offer to dock again and
	// the range check that keeps a docking honest would never run at all.
	if (USpaceMMODockingComponent* Arrived =
		AshorePilot->FindComponentByClass<USpaceMMODockingComponent>())
	{
		Arrived->CharacterId = CharacterId;
		Arrived->AdoptDocking(StationId);
	}

	// And on the controller, which pushes it onto every pawn possessed after this one.
	if (Controller != nullptr)
	{
		Controller->NoteDockedStation(StationId);
	}

	UE_LOG(LogSpaceMMOBackend, Log,
		TEXT("%s is in station %d's hangar; its pawn has left the world, and its pilot is "
			"standing at %s."),
		*Station.GetStation().Name, StationId, *Ashore.ToString());

	// Last, and nothing touches this component afterwards: destroying the owner destroys this.
	Ship->Destroy();

	return true;
}

void USpaceMMODockingComponent::ClientDockResult_Implementation(
	const FString& Message, const bool bSucceeded)
{
	if (GEngine != nullptr)
	{
		GEngine->AddOnScreenDebugMessage(
			41, 4.0f, bSucceeded ? FColor::Green : FColor::Orange, Message);
	}
}

void USpaceMMODockingComponent::ResumeDockedAt(const int32 StationId)
{
	const AActor* Owner = GetOwner();

	// Server-side, like everything else here. The client's copy of this component has no authority
	// over where the ship is, and moving it there would be corrected by replication a frame later.
	if (Owner == nullptr || !Owner->HasAuthority() || StationId == 0)
	{
		return;
	}

	// Already there, which is the ordinary case for anyone who docked during this session.
	if (DockedStationId == StationId)
	{
		return;
	}

	ResumeStationId = StationId;
	SecondsWaitingToResume = 0.0;

	UE_LOG(LogSpaceMMOBackend, Log,
		TEXT("Character %d is docked at station %d; waiting for it to exist before saying so."),
		CharacterId, StationId);
}

bool USpaceMMODockingComponent::TryResume()
{
	UWorld* World = GetWorld();

	if (World == nullptr)
	{
		return false;
	}

	for (TActorIterator<ASpaceMMOStationActor> It(World); It; ++It)
	{
		const ASpaceMMOStationActor* Station = *It;

		if (Station == nullptr || Station->GetStation().Id != ResumeStationId)
		{
			continue;
		}

		// <strong>Nothing is moved, and that is the fix rather than an omission.</strong> This used
		// to put the ship at the station's own position, which was task 114's answer to a ship that
		// did not survive a restart. Task 147 answers it better: a player comes back at the exact
		// position they left, docked or not, so there is nothing left for this to correct.
		//
		// Worse than redundant, it fired on every possession. ResumeAtStationId is handed to
		// whichever docking component is on the current pawn, and a freshly boarded ship's is zero,
		// so boarding a ship you had just summoned teleported it into the station you were standing
		// in -- reported from a playtest on 7 September, at the first moment anybody had a ship of
		// their own to board.
		//
		// What remains is the record, which every new pawn does need: the range check below does
		// nothing while DockedStationId is zero, and the station has to exist before it is set or
		// the first check undocks somebody standing right next to it.
		DockedStationId = ResumeStationId;
		ResumeStationId = 0;

		UE_LOG(LogSpaceMMOBackend, Log,
			TEXT("Character %d is docked at %s (station %d), and stays where they are."),
			CharacterId, *Station->GetStation().Name, DockedStationId);

		return true;
	}

	return false;
}

void USpaceMMODockingComponent::TickComponent(
	const float DeltaTime,
	const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// <strong>Before the authority guard, because binding is a local concern.</strong> On a
	// dedicated server the machine that needs the key bound is the one with no authority, and a
	// rebind that lived below this line would never run there at all.
	//
	// <strong>Checked every tick rather than driven by an event.</strong> APawn::UnPossessed
	// destroys the pawn's input component outright (Pawn.cpp:727, DestroyPlayerInputComponent), so
	// every re-boarding needs a fresh binding -- and the two events that were supposed to deliver
	// one, ReceiveRestartedDelegate and the controller's possession pass, both raced the moment the
	// new component exists. A ship boarded, left and boarded again kept flying and kept stepping
	// out, because SetupPlayerInputComponent runs on the new component, and only the dock key was
	// dead. Comparing against the component actually bound cannot double-bind and cannot go stale,
	// so this is self-healing rather than ordered (task 156).
	if (const APawn* OwningPawn = Cast<APawn>(GetOwner()))
	{
		BindInput(OwningPawn->InputComponent);
	}

	const AActor* Owner = GetOwner();

	if (Owner == nullptr || !Owner->HasAuthority())
	{
		return;
	}

	// Before the range check, and separate from it. The range check does nothing while
	// DockedStationId is zero, which is exactly the state a freshly spawned ship is in -- so a
	// resume that waited for it would wait forever.
	if (ResumeStationId != 0)
	{
		SecondsWaitingToResume += DeltaTime;

		if (!TryResume() && SecondsWaitingToResume >= ResumeTimeoutSeconds)
		{
			// Loud, and it gives up rather than retrying forever. A station that never appears
			// means the world and the backend disagree about what exists, which is worth knowing
			// about on its own -- and a silent wait leaves the player exactly where task 114 found
			// them, with no way to tell that from the bug.
			UE_LOG(LogSpaceMMOBackend, Warning,
				TEXT("Gave up putting character %d back at station %d after %.0f seconds: no such "
					"station exists in this world."),
				CharacterId, ResumeStationId, ResumeTimeoutSeconds);

			ResumeStationId = 0;
		}
	}

	if (DockedStationId == 0)
	{
		return;
	}

	SecondsSinceRangeCheck += DeltaTime;

	if (SecondsSinceRangeCheck < RangeCheckSeconds)
	{
		return;
	}

	SecondsSinceRangeCheck = 0.0;

	// Flying away undocks you. Nobody sends a message for leaving, and without this "docked" is a
	// state you enter once and never exit — which would let a player dock at the capital, fly to
	// Grimhold, and keep trading on the capital's book from there.
	const ASpaceMMOStationActor* Station = FindStationInRange();

	if (Station != nullptr && Station->GetStation().Id == DockedStationId)
	{
		return;
	}

	const UWorld* World = GetWorld();

	const UGameInstance* GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;

	if (USpaceMMOBackendClient* Backend =
		GameInstance != nullptr
			? GameInstance->GetSubsystem<USpaceMMOBackendClient>()
			: nullptr)
	{
		Backend->UndockAsServer(CharacterId);
	}

	DockedStationId = 0;

	// The same clearing the key performs, for the same reason: the held station is pushed onto the
	// next pawn, and one this player has flown away from would undock them again on arrival.
	if (const APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		if (ASpaceMMOPlayerController* Controller =
			Cast<ASpaceMMOPlayerController>(Pawn->GetController()))
		{
			Controller->NoteDockedStation(0);
		}
	}

	ClientDockResult(TEXT("Left docking range."), false);
}

bool USpaceMMODockingComponent::TryGetSystemPosition(FSystemCoordinate& OutPosition) const
{
	const AActor* Owner = GetOwner();

	const UWorld* World = GetWorld();

	const USpaceMMORenderOriginSubsystem* Origin =
		World != nullptr ? World->GetSubsystem<USpaceMMORenderOriginSubsystem>() : nullptr;

	if (Owner == nullptr || Origin == nullptr)
	{
		return false;
	}

	// Reconstructed from the render origin rather than read off a pawn's own navigation, so this
	// works for a ship and for a character on foot without knowing which it is attached to.
	OutPosition = FSystemCoordinate(
		Origin->GetRenderOrigin().Kilometres
		+ (Owner->GetActorLocation() / SpaceMMO::Coordinates::CentimetresPerKilometre));

	return true;
}

ASpaceMMOStationActor* USpaceMMODockingComponent::NearestStation(
	double& OutKilometres) const
{
	UWorld* World = GetWorld();

	FSystemCoordinate Position;

	OutKilometres = 0.0;

	if (World == nullptr || !TryGetSystemPosition(Position))
	{
		return nullptr;
	}

	ASpaceMMOStationActor* Nearest = nullptr;
	double NearestDistance = TNumericLimits<double>::Max();

	for (TActorIterator<ASpaceMMOStationActor> It(World); It; ++It)
	{
		ASpaceMMOStationActor* Station = *It;

		// An unplaced station is nowhere rather than at the origin, and measuring to it would
		// report a distance to a place that does not exist.
		if (Station == nullptr || !Station->GetStation().bPlaced)
		{
			continue;
		}

		const double Distance =
			(Position.Kilometres - Station->GetSystemPosition().Kilometres).Size();

		if (Distance < NearestDistance)
		{
			NearestDistance = Distance;
			Nearest = Station;
		}
	}

	if (Nearest != nullptr)
	{
		OutKilometres = NearestDistance;
	}

	return Nearest;
}

ASpaceMMOStationActor* USpaceMMODockingComponent::FindStationInRange() const
{
	FSystemCoordinate Position;

	if (!TryGetSystemPosition(Position))
	{
		return nullptr;
	}

	double Kilometres = 0.0;

	ASpaceMMOStationActor* Nearest = NearestStation(Kilometres);

	if (Nearest == nullptr)
	{
		return nullptr;
	}

	// The same rule the client draws with, so a prompt that says "dock available" is never followed
	// by a refusal. Asked of the nearest one only: a station further away that happens to have a
	// wider ring is not the one you are standing at.
	return ASpaceMMOStationActor::IsWithinDockingRange(
		Nearest->GetStation(), Nearest->GetSystemPosition(), Position)
		? Nearest
		: nullptr;
}
