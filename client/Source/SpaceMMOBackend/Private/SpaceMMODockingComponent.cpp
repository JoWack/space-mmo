#include "SpaceMMODockingComponent.h"

#include "Components/InputComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "SpaceMMOBackendClient.h"
#include "SpaceMMOBackendLog.h"
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

	InputComponent->BindAction(
		TEXT("Dock"), IE_Pressed, this, &USpaceMMODockingComponent::RequestToggleDock);

	BoundInput = InputComponent;

	UE_LOG(LogSpaceMMOBackend, Log, TEXT("Dock key bound on %s."), *GetNameSafe(GetOwner()));
}

void USpaceMMODockingComponent::RequestToggleDock()
{
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

		ClientDockResult(TEXT("Undocked."), true);

		return;
	}

	const ASpaceMMOStationActor* Station = FindStationInRange();

	if (Station == nullptr)
	{
		ClientDockResult(TEXT("Nothing in docking range."), false);

		return;
	}

	DockedStationId = Station->GetStation().Id;

	Backend->DockAsServer(CharacterId, DockedStationId);

	ClientDockResult(
		FString::Printf(TEXT("Docked at %s."), *Station->GetStation().Name), true);
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
		TEXT("Character %d was left docked at station %d; waiting for it to exist to put the ship "
			"back there."),
		CharacterId, StationId);
}

bool USpaceMMODockingComponent::TryResume()
{
	APawn* Ship = Cast<APawn>(GetOwner());

	UWorld* World = GetWorld();

	if (Ship == nullptr || World == nullptr)
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

		// At the station's own position rather than beside it. Docked means at the station, and it
		// is the only placement guaranteed to be inside its own docking range -- an offset guessed
		// here would be outside the range of any station whose range is smaller than the guess, and
		// the range check below would undock them again on the next pass.
		if (ASpaceMMOShipPawn* ShipPawn = Cast<ASpaceMMOShipPawn>(Ship))
		{
			ShipPawn->SetSystemPosition(Station->GetSystemPosition());
		}
		else
		{
			// Not a ship. Nothing to move, but the record is still true and the range check needs
			// to know about it, so this is not a failure.
			UE_LOG(LogSpaceMMOBackend, Log,
				TEXT("Character %d resumed docked at %s while not in a ship; left where they are."),
				CharacterId, *Station->GetStation().Name);
		}

		DockedStationId = ResumeStationId;
		ResumeStationId = 0;

		// Said out loud, because this is a thing that moves a player's ship without being asked to
		// and the alternative to saying so is a teleport nobody can account for.
		UE_LOG(LogSpaceMMOBackend, Log,
			TEXT("Put character %d back at %s (station %d), where they were left docked."),
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

ASpaceMMOStationActor* USpaceMMODockingComponent::FindStationInRange() const
{
	UWorld* World = GetWorld();

	FSystemCoordinate Position;

	if (World == nullptr || !TryGetSystemPosition(Position))
	{
		return nullptr;
	}

	ASpaceMMOStationActor* Nearest = nullptr;
	double NearestDistance = TNumericLimits<double>::Max();

	for (TActorIterator<ASpaceMMOStationActor> It(World); It; ++It)
	{
		ASpaceMMOStationActor* Station = *It;

		if (Station == nullptr)
		{
			continue;
		}

		// The same rule the client draws with, so a prompt that says "dock available" is never
		// followed by a refusal.
		if (!ASpaceMMOStationActor::IsWithinDockingRange(
			Station->GetStation(), Station->GetSystemPosition(), Position))
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

	return Nearest;
}
