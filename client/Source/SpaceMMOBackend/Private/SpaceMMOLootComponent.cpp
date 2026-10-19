#include "SpaceMMOLootComponent.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "SpaceMMOBackendClient.h"
#include "SpaceMMOBackendLog.h"
#include "SpaceMMOCharacterPawn.h"
#include "SpaceMMOCombatComponent.h"
#include "SpaceMMOGatheringComponent.h"
#include "SpaceMMOLootCrateActor.h"
#include "SpaceMMOLootSubsystem.h"
#include "SpaceMMOPlayerController.h"
#include "SpaceMMODepositActor.h"

USpaceMMOLootComponent::USpaceMMOLootComponent()
{
	// Ticks to drop a dead player's report once they are up again.
	PrimaryComponentTick.bCanEverTick = true;

	SetIsReplicatedByDefault(true);
}

bool USpaceMMOLootComponent::CrateTakesE(
	const bool bCrate, const double CrateMetres, const bool bDeposit, const double DepositMetres)
{
	return bCrate && (!bDeposit || CrateMetres <= DepositMetres);
}

ASpaceMMOPlayerController* USpaceMMOLootComponent::OwningController() const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());

	return Pawn != nullptr ? Cast<ASpaceMMOPlayerController>(Pawn->GetController()) : nullptr;
}

ASpaceMMOLootCrateActor* USpaceMMOLootComponent::FindCrateInRange(double* OutMetres) const
{
	const ASpaceMMOCharacterPawn* Pawn = Cast<ASpaceMMOCharacterPawn>(GetOwner());
	const UWorld* World = GetWorld();
	const USpaceMMOLootSubsystem* Loot = World != nullptr ? World->GetSubsystem<USpaceMMOLootSubsystem>() : nullptr;

	return Pawn != nullptr && Loot != nullptr ? Loot->NearestCrate(Pawn->GetSystemPosition(), RangeMetres, OutMetres) : nullptr;
}

ASpaceMMOLootCrateActor* USpaceMMOLootComponent::CrateForE() const
{
	double CrateMetres = 0.0;
	ASpaceMMOLootCrateActor* Crate = FindCrateInRange(&CrateMetres);

	const ASpaceMMOCharacterPawn* Pawn = Cast<ASpaceMMOCharacterPawn>(GetOwner());
	const USpaceMMOGatheringComponent* Gathering =
		GetOwner() != nullptr ? GetOwner()->FindComponentByClass<USpaceMMOGatheringComponent>() : nullptr;
	const ASpaceMMODepositActor* Deposit = Gathering != nullptr ? Gathering->FindDepositInRange() : nullptr;

	const double DepositMetres = Deposit != nullptr && Pawn != nullptr
		? FVector::Distance(Deposit->GetSurfacePosition().Kilometres, Pawn->GetSystemPosition().Kilometres) * 1000.0
		: 0.0;

	return CrateTakesE(Crate != nullptr, CrateMetres, Deposit != nullptr, DepositMetres) ? Crate : nullptr;
}

void USpaceMMOLootComponent::RequestLoot(ASpaceMMOLootCrateActor* Crate, const TArray<FBackendLootItem>& Wanted)
{
	if (Crate == nullptr || bLootPending)
	{
		return;
	}

	bLootPending = true;

	ServerLoot(Crate, Wanted);
}

void USpaceMMOLootComponent::ServerLoot_Implementation(
	ASpaceMMOLootCrateActor* Crate, const TArray<FBackendLootItem>& Wanted)
{
	const ASpaceMMOCharacterPawn* Pawn = Cast<ASpaceMMOCharacterPawn>(GetOwner());
	const ASpaceMMOPlayerController* Controller = OwningController();
	const UWorld* World = GetWorld();
	const UGameInstance* GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;
	USpaceMMOBackendClient* Backend = GameInstance != nullptr ? GameInstance->GetSubsystem<USpaceMMOBackendClient>() : nullptr;

	if (Pawn == nullptr || !Pawn->HasAuthority() || Controller == nullptr || Backend == nullptr)
	{
		ClientLootResult({}, TEXT("gone"));

		return;
	}

	// Where the server has them, not where the client says: a crate on another planet is not in reach.
	const double Metres = Crate != nullptr
		? FVector::Distance(Crate->GetSystemPosition().Kilometres, Pawn->GetSystemPosition().Kilometres) * 1000.0
		: TNumericLimits<double>::Max();

	const USpaceMMOCombatComponent* Combat = Pawn->FindComponentByClass<USpaceMMOCombatComponent>();

	if (Crate == nullptr || Metres > RangeMetres + 1.0)
	{
		UE_LOG(LogSpaceMMOBackend, Log, TEXT("Loot: character %d asked for a crate %.1f m away; refused."),
			Controller->GetCharacterId(), Metres);

		ClientLootResult({}, TEXT("out_of_reach"));

		return;
	}

	if (Combat != nullptr && Combat->IsDead())
	{
		ClientLootResult({}, TEXT("dead"));

		return;
	}

	const int32 CharacterId = Controller->GetCharacterId();
	const int64 ContainerId = Crate->GetContainerId();

	UE_LOG(LogSpaceMMOBackend, Log, TEXT("Loot: character %d takes %s from crate %lld, %.1f m away."),
		CharacterId, Wanted.IsEmpty() ? TEXT("everything") : *FString::Printf(TEXT("%d stacks"), Wanted.Num()), ContainerId,
		Metres);

	TWeakObjectPtr<USpaceMMOLootComponent> WeakThis(this);
	TWeakObjectPtr<ASpaceMMOLootCrateActor> WeakCrate(Crate);

	Backend->LootAsServer(
		ContainerId,
		CharacterId,
		Wanted,
		[WeakThis, WeakCrate, ContainerId](const FBackendLootResult& Result)
		{
			// What is left, mirrored onto the crate for everyone; gone with nothing left, or when the API says so.
			if (ASpaceMMOLootCrateActor* Crate = WeakCrate.Get())
			{
				if (Result.bHasLeft)
				{
					Crate->SetItems(Result.Left.Items);
				}
				else if (Result.bTook || Result.Refusal == TEXT("gone"))
				{
					UE_LOG(LogSpaceMMOBackend, Log, TEXT("Loot: crate %lld is empty."), ContainerId);

					Crate->Destroy();
				}
			}

			if (USpaceMMOLootComponent* Self = WeakThis.Get())
			{
				Self->ClientLootResult(Result.Taken, Result.Refusal);
			}
		},
		[WeakThis](const FBackendFailure& Failure)
		{
			UE_LOG(LogSpaceMMOBackend, Warning, TEXT("Loot: the crate could not be looted (%d): %s"),
				Failure.HttpStatus, *Failure.Message);

			if (USpaceMMOLootComponent* Self = WeakThis.Get())
			{
				Self->ClientLootResult({}, TEXT("failed"));
			}
		});
}

FString USpaceMMOLootComponent::LootMessage(const TArray<FBackendLootItem>& Taken, const FString& Refusal)
{
	if (!Taken.IsEmpty())
	{
		TArray<FString> Parts;

		for (const FBackendLootItem& Item : Taken)
		{
			Parts.Add(FString::Printf(TEXT("%s %s"), *ASpaceMMOPlayerController::GroupDigits(Item.Quantity), *Item.Name));
		}

		return FString::Printf(TEXT("Took %s"), *FString::Join(Parts, TEXT(", ")));
	}

	// In words a player can act on; anything unforeseen as it came, rather than nothing.
	if (Refusal == TEXT("no_room"))
	{
		return TEXT("No room in your pockets");
	}

	if (Refusal == TEXT("gone") || Refusal == TEXT("not_there"))
	{
		return TEXT("It's gone");
	}

	if (Refusal == TEXT("out_of_reach"))
	{
		return TEXT("Too far away to take from");
	}

	if (Refusal == TEXT("dead"))
	{
		return TEXT("The dead take nothing");
	}

	return Refusal.IsEmpty() ? FString(TEXT("Nothing was taken")) : Refusal;
}

void USpaceMMOLootComponent::ClientLootResult_Implementation(const TArray<FBackendLootItem>& Taken, const FString& Refusal)
{
	bLootPending = false;

	UE_LOG(LogSpaceMMOBackend, Log, TEXT("Loot: %s."), *LootMessage(Taken, Refusal));

	if (ASpaceMMOPlayerController* Controller = OwningController())
	{
		Controller->ShowTransientMessage(
			LootMessage(Taken, Refusal), Taken.IsEmpty() ? ESpaceMMOMessageTone::Warning : ESpaceMMOMessageTone::Positive);

		// The pockets changed on the server: the inventory beside the window has to say so.
		if (!Taken.IsEmpty())
		{
			Controller->RefreshCharacterState();
		}
	}
}

void USpaceMMOLootComponent::ReportDeathCosts(const FBackendDeathCosts& Costs)
{
	ClientDeathCosts(Costs);
}

void USpaceMMOLootComponent::ClientDeathCosts_Implementation(const FBackendDeathCosts& Costs)
{
	DeathReport = Costs;
	bHasDeathReport = true;

	UE_LOG(LogSpaceMMOBackend, Log, TEXT("Loot: this death cost %d materials and wore %d items%s."),
		Costs.Lost.Num(), Costs.Worn.Num(), Costs.bAtStation ? TEXT(" (at a station, nothing)") : TEXT(""));

	// Pockets emptied and gear worn, on the server: the inventory and equipment screens must not show them as they were.
	if (ASpaceMMOPlayerController* Controller = OwningController())
	{
		Controller->RefreshCharacterState();
	}
}

void USpaceMMOLootComponent::TickComponent(
	const float DeltaTime, const ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Up again: the report was this death's, and must not stand in for the next one's before its answer arrives.
	const USpaceMMOCombatComponent* Combat =
		GetOwner() != nullptr ? GetOwner()->FindComponentByClass<USpaceMMOCombatComponent>() : nullptr;

	if (bHasDeathReport && !bLookSample && Combat != nullptr && !Combat->IsDead())
	{
		bHasDeathReport = false;
	}
}

#if !UE_BUILD_SHIPPING
void USpaceMMOLootComponent::ShowSampleDeathReportForLook()
{
	FBackendDeathCosts Sample;
	Sample.bApplied = true;

	FBackendDeathLoss Ore;
	Ore.Name = TEXT("Ferrite Ore");
	Ore.Style = EBackendItemStyle::Terran;
	Ore.Dropped = 5;
	Ore.Destroyed = 5;

	FBackendDeathLoss Plate;
	Plate.Name = TEXT("Structural Plate");
	Plate.Style = EBackendItemStyle::Martian;
	Plate.Dropped = 3;
	Plate.Destroyed = 2;

	FBackendWornLoss Rifle;
	Rifle.Name = TEXT("Laser Rifle");
	Rifle.Before = 100;
	Rifle.After = 90;

	FBackendWornLoss Laser;
	Laser.Name = TEXT("Crude Mining Laser");
	Laser.Before = 70;
	Laser.After = 60;

	Sample.Lost = {Ore, Plate};
	Sample.Worn = {Rifle, Laser};

	DeathReport = Sample;
	bHasDeathReport = true;
	bLookSample = true;
}
#endif
