#include "SpaceMMODockingComponent.h"

#include "Components/InputComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "SpaceMMOBackendClient.h"
#include "SpaceMMOBackendLog.h"
#include "SpaceMMORenderOrigin.h"
#include "SpaceMMOStationActor.h"

USpaceMMODockingComponent::USpaceMMODockingComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

void USpaceMMODockingComponent::BeginPlay()
{
	Super::BeginPlay();

	if (const APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		BindInput(Pawn->InputComponent);
	}
}

void USpaceMMODockingComponent::BindInput(UInputComponent* InputComponent)
{
	if (bInputBound || InputComponent == nullptr)
	{
		return;
	}

	InputComponent->BindAction(
		TEXT("Dock"), IE_Pressed, this, &USpaceMMODockingComponent::RequestToggleDock);

	bInputBound = true;

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
		// Loud, because the alternative is a key that appears to do nothing and a player who
		// concludes the station is broken.
		UE_LOG(LogSpaceMMOBackend, Warning,
			TEXT("Dock: no character bound to this pawn; nothing to dock."));

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

void USpaceMMODockingComponent::TickComponent(
	const float DeltaTime,
	const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const AActor* Owner = GetOwner();

	if (Owner == nullptr || !Owner->HasAuthority() || DockedStationId == 0)
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
