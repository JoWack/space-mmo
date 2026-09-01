#include "SpaceMMOGatheringComponent.h"

#include "Components/InputComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "SpaceMMOBackendClient.h"
#include "SpaceMMOBackendLog.h"
#include "SpaceMMOCharacterPawn.h"
#include "SpaceMMODepositActor.h"
#include "SpaceMMOPlayerController.h"

namespace
{
	/**
	 * One key for every gather message, so pressing E repeatedly replaces the line rather than
	 * stacking a column up the screen. Shared by the yield and the refusal deliberately: they are
	 * answers to the same press and only one of them can be current.
	 */
	constexpr uint64 GatherMessageKey = 0x5A17;

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
	// Compared against the component actually bound, not a flag: possession replaces it.
	if (InputComponent == nullptr || BoundInput.Get() == InputComponent)
	{
		return;
	}

	InputComponent->BindAction(
		TEXT("Gather"), IE_Pressed, this, &USpaceMMOGatheringComponent::RequestGather);

	BoundInput = InputComponent;

	UE_LOG(LogSpaceMMOBackend, Log, TEXT("Gather key bound on %s by component %s (input %s)."),
		*GetNameSafe(GetOwner()), *GetName(), *GetNameSafe(InputComponent));
}

void USpaceMMOGatheringComponent::RequestGather()
{
	const UWorld* World = GetWorld();

	if (World != nullptr)
	{
		const double Now = World->GetRealTimeSeconds();

		// Dropped locally rather than sent and refused. Suppressing the request is the whole point
		// — a refusal still costs a round trip and a database transaction, and it was those, not
		// the gathering, that made two players hammering a key wait eight seconds for an answer.
		if (Now - LastRequestSeconds < MinimumRequestSeconds)
		{
			return;
		}

		LastRequestSeconds = Now;
	}

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
	// all. The server asks; the backend rules on it; the player is told the answer.
	const FString ItemName = Deposit->GetNode().ItemName;

	TWeakObjectPtr<USpaceMMOGatheringComponent> WeakThis(this);

	Backend->GatherAsServer(
		CharacterId,
		Deposit->GetNode().Id,
		USpaceMMOBackendClient::FOnGatherComplete::CreateLambda(
			[WeakThis, ItemName](const FBackendGatherResult& Result)
			{
				// Weak, because an HTTP response can outlive the pawn that asked — a player who
				// boards their ship mid-request destroys this component before the reply lands.
				if (USpaceMMOGatheringComponent* Component = WeakThis.Get())
				{
					Component->ClientGatherResult(
						Result.Quantity,
						Result.XpAwarded,
						Result.NodeRemaining,
						ItemName,
						Result.bNoRoom);
				}
			}),
		USpaceMMOBackendClient::FOnGatherFailed::CreateLambda(
			[WeakThis](const FBackendFailure& Failure)
			{
				// The refusal is the whole answer here: a missing tool, too low a skill, or a
				// deposit somebody else just emptied. Told to the player rather than logged where
				// only the host can read it.
				if (USpaceMMOGatheringComponent* Component = WeakThis.Get())
				{
					Component->ClientGatherRefused(Failure.Message);
				}
			}));
}

ASpaceMMOPlayerController* USpaceMMOGatheringComponent::OwningController() const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());

	return Pawn != nullptr ? Cast<ASpaceMMOPlayerController>(Pawn->GetController()) : nullptr;
}

void USpaceMMOGatheringComponent::ClientGatherRefused_Implementation(const FString& Reason)
{
	const FString Message = Reason.IsEmpty() ? FString(TEXT("That was refused")) : Reason;

	UE_LOG(LogSpaceMMOBackend, Log, TEXT("Gather refused: %s"), *Message);

	if (ASpaceMMOPlayerController* Controller = OwningController())
	{
		// Always a warning: this path only exists because the server said no.
		Controller->ShowTransientMessage(Message, ESpaceMMOMessageTone::Warning);
	}
}

FString USpaceMMOGatheringComponent::FormatGatherMessage(
	const int32 Quantity,
	const int64 XpAwarded,
	const int32 NodeRemaining,
	const FString& ItemName,
	const bool bNoRoom)
{
	if (Quantity > 0)
	{
		return FString::Printf(
			TEXT("+%d %s   (+%lld xp)   %d left"),
			Quantity,
			ItemName.IsEmpty() ? TEXT("ore") : *ItemName,
			XpAwarded,
			NodeRemaining);
	}

	// Nothing yielded, and the reason matters. Two of the three are worth waiting out and one never
	// is -- a player told to give it a moment while standing at a full pack will stand there giving
	// it a moment, which is exactly what happened the first time capacity bound.
	//
	// Checked before the node, because a full pack is the player's problem wherever they are
	// standing: being told a deposit is worked out while the real answer is in your own pockets
	// sends somebody looking for another rock.
	if (bNoRoom)
	{
		return FString(TEXT("You are carrying all you can - nothing more will fit"));
	}

	return NodeRemaining > 0
		? FString(TEXT("Nothing yet - give it a moment"))
		: FString(TEXT("This deposit is worked out"));
}

ESpaceMMOMessageTone USpaceMMOGatheringComponent::GatherTone(const int32 Quantity)
{
	return Quantity > 0 ? ESpaceMMOMessageTone::Positive : ESpaceMMOMessageTone::Warning;
}

void USpaceMMOGatheringComponent::ClientGatherResult_Implementation(
	const int32 Quantity,
	const int64 XpAwarded,
	const int32 NodeRemaining,
	const FString& ItemName,
	const bool bNoRoom)
{
	const FString Message =
		FormatGatherMessage(Quantity, XpAwarded, NodeRemaining, ItemName, bNoRoom);

	UE_LOG(LogSpaceMMOBackend, Log, TEXT("%s"), *Message);

	// Only when something was actually credited. A refused press changes nothing server-side, and
	// re-fetching on every hammered key would put the panel's traffic back where the request
	// throttle just took it from.
	if (Quantity > 0)
	{
		const APawn* Pawn = Cast<APawn>(GetOwner());

		if (ASpaceMMOPlayerController* Controller =
			Pawn != nullptr ? Cast<ASpaceMMOPlayerController>(Pawn->GetController()) : nullptr)
		{
			// Asked for, not applied locally. The client could add the quantity to its own copy and
			// be right nearly always -- but "nearly" is how a display starts disagreeing with the
			// database, and the panel exists to be believed.
			Controller->RefreshCharacterState();
		}
	}

	// Floats above the pawn now, which is what the panel detour was a workaround for: a separate
	// on-screen entry competed with the panel for slots, the panel is redrawn every frame at zero
	// display time, and a three-second message landed wherever the free list put it -- usually below
	// dozens of panel lines and off the bottom of the screen. The widget also gets the colour back
	// that folding it into the panel had to give up.
	if (ASpaceMMOPlayerController* Controller = OwningController())
	{
		Controller->ShowTransientMessage(Message, GatherTone(Quantity));
	}
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
