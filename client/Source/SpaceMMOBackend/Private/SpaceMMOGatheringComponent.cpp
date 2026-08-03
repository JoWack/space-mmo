#include "SpaceMMOGatheringComponent.h"

#include "Components/InputComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "SpaceMMOBackendClient.h"
#include "SpaceMMOBackendLog.h"
#include "SpaceMMOCharacterPawn.h"
#include "SpaceMMODepositActor.h"

USpaceMMOGatheringComponent::USpaceMMOGatheringComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	// Replicated so its Server RPC has a route. An RPC on an unreplicated component is silently
	// dropped, which looks exactly like the key not being bound.
	SetIsReplicatedByDefault(true);
}

void USpaceMMOGatheringComponent::BeginPlay()
{
	Super::BeginPlay();

	// The pawn may not be possessed yet, in which case it has no input component and BindInput is
	// called again by whoever possesses it. Binding here covers the already-possessed case.
	if (const APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		BindInput(Pawn->InputComponent);
	}
}

void USpaceMMOGatheringComponent::BindInput(UInputComponent* InputComponent)
{
	if (InputComponent == nullptr)
	{
		return;
	}

	InputComponent->BindAction(
		TEXT("Gather"), IE_Pressed, this, &USpaceMMOGatheringComponent::RequestGather);
}

void USpaceMMOGatheringComponent::RequestGather()
{
	// Deliberately carries nothing. See the class comment: everything the server needs, it already
	// knows better than the client does.
	ServerGather();
}

void USpaceMMOGatheringComponent::ServerGather_Implementation()
{
	const AActor* Owner = GetOwner();

	if (Owner == nullptr || !Owner->HasAuthority())
	{
		return;
	}

	const ASpaceMMODepositActor* Deposit = FindDepositInRange();

	if (Deposit == nullptr)
	{
		// Not an error. Pressing the key in an empty field is an ordinary thing to do.
		UE_LOG(LogSpaceMMOBackend, Verbose, TEXT("Gather: nothing in range."));

		return;
	}

	const UWorld* World = GetWorld();

	const UGameInstance* GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;

	USpaceMMOBackendClient* Backend =
		GameInstance != nullptr
			? GameInstance->GetSubsystem<USpaceMMOBackendClient>()
			: nullptr;

	if (Backend == nullptr)
	{
		return;
	}

	if (CharacterId == 0)
	{
		// Loud, because the alternative is a key that appears to do nothing and a player who
		// concludes the deposit is broken.
		UE_LOG(LogSpaceMMOBackend, Warning,
			TEXT("Gather: no character bound to this pawn; nothing to credit."));

		return;
	}

	UE_LOG(LogSpaceMMOBackend, Log, TEXT("Gather: character %d works %s."),
		CharacterId, *Deposit->GetNode().Key);

	// The quantity is not ours to decide, and neither is whether this attempt yields anything at
	// all. The server asks; the backend rules on it.
	Backend->GatherAsServer(CharacterId, Deposit->GetNode().Id, StationId);
}

ASpaceMMODepositActor* USpaceMMOGatheringComponent::FindDepositInRange() const
{
	UWorld* World = GetWorld();

	const ASpaceMMOCharacterPawn* Pawn = Cast<ASpaceMMOCharacterPawn>(GetOwner());

	if (World == nullptr || Pawn == nullptr)
	{
		return nullptr;
	}

	// Compared in system space, not in Unreal world space. World space is relative to the render
	// origin, which is a per-client notion — the dedicated server has no viewer and no meaningful
	// origin of its own, so a distance measured there would be measured against nothing.
	const FVector Position = Pawn->GetSystemPosition().Kilometres;

	const double RangeKilometres = RangeMetres / 1000.0;

	ASpaceMMODepositActor* Best = nullptr;
	double BestDistance = TNumericLimits<double>::Max();

	for (TActorIterator<ASpaceMMODepositActor> It(World); It; ++It)
	{
		ASpaceMMODepositActor* Deposit = *It;

		if (Deposit == nullptr)
		{
			continue;
		}

		const double Distance =
			FVector::Distance(Deposit->GetSurfacePosition().Kilometres, Position);

		// Nearest rather than first, so two deposits close together do not resolve to whichever
		// happened to be spawned earlier.
		if (Distance <= RangeKilometres && Distance < BestDistance)
		{
			Best = Deposit;
			BestDistance = Distance;
		}
	}

	return Best;
}
