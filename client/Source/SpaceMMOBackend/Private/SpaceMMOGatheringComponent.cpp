#include "SpaceMMOGatheringComponent.h"

#include "Components/InputComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "SpaceMMOBackendClient.h"
#include "SpaceMMOBackendLog.h"
#include "SpaceMMOCharacterPawn.h"
#include "SpaceMMODepositActor.h"

namespace
{
	/**
	 * Triggers a gather from the console: SpaceMMO.Gather
	 *
	 * The same path the key takes, so it is a real test rather than a parallel one. Exists because
	 * a key binding is the one part of this that cannot be checked without a human at a keyboard —
	 * and the first version of this feature shipped with the binding silently doing nothing, which
	 * no automated test noticed and no log recorded.
	 */
	FAutoConsoleCommandWithWorld GGatherCommand(
		TEXT("SpaceMMO.Gather"),
		TEXT("Attempts to gather from the nearest deposit, as if the gather key were pressed."),
		FConsoleCommandWithWorldDelegate::CreateLambda(
			[](UWorld* World)
			{
				const APlayerController* Controller =
					World != nullptr ? World->GetFirstPlayerController() : nullptr;

				const APawn* Pawn = Controller != nullptr ? Controller->GetPawn() : nullptr;

				USpaceMMOGatheringComponent* Gathering =
					Pawn != nullptr
						? Pawn->FindComponentByClass<USpaceMMOGatheringComponent>()
						: nullptr;

				if (Gathering == nullptr)
				{
					UE_LOG(LogSpaceMMOBackend, Warning,
						TEXT("SpaceMMO.Gather: the possessed pawn cannot gather. On foot?"));

					return;
				}

				Gathering->RequestGather();
			}));
}

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

	APawn* Pawn = Cast<APawn>(GetOwner());

	if (Pawn == nullptr)
	{
		return;
	}

	// Covers the pawn that is already possessed by the time this runs.
	BindInput(Pawn->InputComponent);

	// And the far more common case: a pawn spawned unpossessed, whose input component does not
	// exist yet. Restart fires after possession has set input up, which is the only moment binding
	// can actually succeed. Missing this was why the key did nothing at all.
	Pawn->ReceiveRestartedDelegate.AddDynamic(
		this, &USpaceMMOGatheringComponent::HandlePawnRestarted);
}

void USpaceMMOGatheringComponent::HandlePawnRestarted(APawn* Pawn)
{
	if (Pawn != nullptr)
	{
		BindInput(Pawn->InputComponent);
	}
}

void USpaceMMOGatheringComponent::BindInput(UInputComponent* InputComponent)
{
	if (InputComponent == nullptr || bInputBound)
	{
		return;
	}

	InputComponent->BindAction(
		TEXT("Gather"), IE_Pressed, this, &USpaceMMOGatheringComponent::RequestGather);

	bInputBound = true;

	UE_LOG(LogSpaceMMOBackend, Log, TEXT("Gather key bound on %s."), *GetNameSafe(GetOwner()));
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
		// Not an error — pressing the key in an empty field is ordinary — but logged at Log level
		// anyway. A key press is a deliberate act, so the one thing it must never do is produce
		// no evidence at all that it was received.
		UE_LOG(LogSpaceMMOBackend, Log, TEXT("Gather: nothing within %.0f m."), RangeMetres);

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
