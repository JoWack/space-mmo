#include "SpaceMMOLootSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "SpaceMMOBackendClient.h"
#include "SpaceMMOBackendLog.h"
#include "SpaceMMOCharacterPawn.h"
#include "SpaceMMOCombatComponent.h"
#include "SpaceMMODeathWire.h"
#include "SpaceMMOLootComponent.h"
#include "SpaceMMOLootCrateActor.h"
#include "SpaceMMOPlanetActor.h"
#include "SpaceMMOPlanetTerrain.h"
#include "TimerManager.h"

namespace
{
	USpaceMMOBackendClient* BackendOf(const UWorld* World)
	{
		const UGameInstance* GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;

		return GameInstance != nullptr ? GameInstance->GetSubsystem<USpaceMMOBackendClient>() : nullptr;
	}
}

bool USpaceMMOLootSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}

	// Played worlds only, as the deposits' subsystem: an editor preview has no deaths to cost.
	const UWorld* World = Cast<UWorld>(Outer);

	return World != nullptr && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE);
}

void USpaceMMOLootSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// Pawns already here as well as those to come, as the deposits' subsystem does: in a standalone game the player's
	// pawn is spawned before the world begins play, and a handler alone never sees it -- which left the first look at
	// this with no prompt over the crate and nothing on the death panel.
	for (TActorIterator<ASpaceMMOCharacterPawn> It(&InWorld); It; ++It)
	{
		AttachLoot(*It);
	}

	ActorSpawnedHandle = InWorld.AddOnActorSpawnedHandler(
		FOnActorSpawned::FDelegate::CreateUObject(this, &USpaceMMOLootSubsystem::AttachLoot));

	// Crates already lying about, once the world has its planets: a server restarted within ten minutes of a death
	// still has the crate to show. Only a machine that can speak for the server asks; a client has none to ask with.
	const USpaceMMOBackendClient* Backend = BackendOf(&InWorld);

	if (InWorld.GetNetMode() != NM_Client && Backend != nullptr && Backend->HasServiceSecret())
	{
		InWorld.GetTimerManager().SetTimer(SyncTimer, FTimerDelegate::CreateUObject(this, &USpaceMMOLootSubsystem::SyncCrates),
			3.0f, false);
	}
}

void USpaceMMOLootSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->RemoveOnActorSpawnedHandler(ActorSpawnedHandle);
		World->GetTimerManager().ClearTimer(SyncTimer);
	}

	Super::Deinitialize();
}

void USpaceMMOLootSubsystem::AttachLoot(AActor* Actor)
{
	ASpaceMMOCharacterPawn* Pawn = Cast<ASpaceMMOCharacterPawn>(Actor);

	// Authority only: the component replicates, and a client that made its own would hold a second copy that
	// speaks for nobody (the gathering component met this first).
	if (Pawn == nullptr || !Pawn->HasAuthority() || Pawn->FindComponentByClass<USpaceMMOLootComponent>() != nullptr)
	{
		return;
	}

	USpaceMMOLootComponent* Loot = NewObject<USpaceMMOLootComponent>(Pawn, TEXT("Loot"));
	Loot->RegisterComponent();

	UE_LOG(LogSpaceMMOBackend, Log, TEXT("Loot: attached to %s."), *GetNameSafe(Pawn));
}

double USpaceMMOLootSubsystem::ServerNowSeconds() const
{
	const UWorld* World = GetWorld();
	const AGameStateBase* GameState = World != nullptr ? World->GetGameState() : nullptr;

	return GameState != nullptr ? GameState->GetServerWorldTimeSeconds() : (World != nullptr ? World->GetTimeSeconds() : 0.0);
}

FVector USpaceMMOLootSubsystem::UpAt(const FSystemCoordinate& Where) const
{
	const ASpaceMMOPlanetActor* Planet = ASpaceMMOPlanetActor::NearestTo(GetWorld(), Where);

	if (Planet == nullptr)
	{
		return FVector::UpVector;
	}

	return FPlanetTerrain::SurfaceNormal(
		Planet->GetPlanetConfig(), Planet->GetTerrainConfig(), Where.Kilometres - Planet->GetPlanetConfig().Centre.Kilometres);
}

void USpaceMMOLootSubsystem::CostDeath(USpaceMMOCombatComponent* Dead)
{
	const ASpaceMMOCharacterPawn* Body = Dead != nullptr ? Cast<ASpaceMMOCharacterPawn>(Dead->GetOwner()) : nullptr;
	USpaceMMOBackendClient* Backend = BackendOf(GetWorld());

	if (Body == nullptr || Backend == nullptr || !Body->HasAuthority() || Dead->GetCharacterId() == 0)
	{
		return;
	}

	const int32 CharacterId = Dead->GetCharacterId();

	// Beside the body on the ground, on the planet it fell on. With no planet -- which nobody on foot is without --
	// at the feet.
	FSystemCoordinate Where = Body->GetSystemPosition();

	if (const ASpaceMMOPlanetActor* Planet = ASpaceMMOPlanetActor::NearestTo(GetWorld(), Where))
	{
		Where = ASpaceMMOLootCrateActor::BesideTheBody(
			Planet->GetPlanetConfig(), Planet->GetTerrainConfig(), Where, Body->GetActorRightVector());
	}

	Costing.FindOrAdd(CharacterId);

	UE_LOG(LogSpaceMMOBackend, Log, TEXT("Loot: costing the death of character %d, the crate at %s km."),
		CharacterId, *Where.Kilometres.ToString());

	TWeakObjectPtr<USpaceMMOLootSubsystem> WeakThis(this);
	TWeakObjectPtr<USpaceMMOCombatComponent> WeakDead(Dead);

	auto Finish = [WeakThis, CharacterId]()
	{
		USpaceMMOLootSubsystem* Self = WeakThis.Get();
		TArray<TFunction<void()>> Waiting;

		if (Self != nullptr && Self->Costing.RemoveAndCopyValue(CharacterId, Waiting))
		{
			for (TFunction<void()>& Then : Waiting)
			{
				Then();
			}
		}
	};

	Backend->CostDeathAsServer(
		CharacterId,
		Where.Kilometres,
		[WeakThis, WeakDead, CharacterId, Finish](const FBackendDeathCosts& Costs)
		{
			USpaceMMOLootSubsystem* Self = WeakThis.Get();

			UE_LOG(LogSpaceMMOBackend, Log,
				TEXT("Loot: the death of character %d cost %d materials and wore %d items%s%s."),
				CharacterId, Costs.Lost.Num(), Costs.Worn.Num(), Costs.bAtStation ? TEXT(" (at a station: nothing)") : TEXT(""),
				Costs.bApplied ? TEXT("") : TEXT(" -- nothing was owed"));

			if (Self != nullptr && Costs.bHasContainer)
			{
				Self->SpawnCrate(Costs.Container);
			}

			if (USpaceMMOCombatComponent* Dead = WeakDead.Get())
			{
				if (USpaceMMOLootComponent* Loot = Dead->GetOwner()->FindComponentByClass<USpaceMMOLootComponent>())
				{
					Loot->ReportDeathCosts(Costs);
				}

				// What they hold has worn, and may have broken: read Weapon 1 again.
				Dead->RequestLoadoutRefresh();
			}

			Finish();
		},
		[CharacterId, Finish](const FBackendFailure& Failure)
		{
			// The respawn will cost it instead, at the last whereabouts.
			UE_LOG(LogSpaceMMOBackend, Warning, TEXT("Loot: the death of character %d could not be costed (%d): %s"),
				CharacterId, Failure.HttpStatus, *Failure.Message);

			Finish();
		});
}

void USpaceMMOLootSubsystem::WhenCosted(const int32 CharacterId, TFunction<void()> Then)
{
	if (TArray<TFunction<void()>>* Waiting = Costing.Find(CharacterId))
	{
		Waiting->Add(MoveTemp(Then));

		return;
	}

	Then();
}

ASpaceMMOLootCrateActor* USpaceMMOLootSubsystem::SpawnCrate(const FBackendLootContainer& Container)
{
	UWorld* World = GetWorld();

	if (World == nullptr || Container.Id == 0)
	{
		return nullptr;
	}

	const FSystemCoordinate Where(Container.PositionKilometres);

	for (TActorIterator<ASpaceMMOLootCrateActor> It(World); It; ++It)
	{
		if ((*It)->GetContainerId() == Container.Id)
		{
			(*It)->SetItems(Container.Items);

			return *It;
		}
	}

	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Spawn.bDeferConstruction = true;

	ASpaceMMOLootCrateActor* Crate = World->SpawnActor<ASpaceMMOLootCrateActor>(Spawn);

	if (Crate != nullptr)
	{
		Crate->SetContainer(Container, UpAt(Where), ServerNowSeconds());
		Crate->FinishSpawning(FTransform::Identity);
	}

	return Crate;
}

void USpaceMMOLootSubsystem::SyncCrates()
{
	USpaceMMOBackendClient* Backend = BackendOf(GetWorld());

	if (Backend == nullptr)
	{
		return;
	}

	TWeakObjectPtr<USpaceMMOLootSubsystem> WeakThis(this);

	Backend->FetchLootContainersAsServer([WeakThis](const TArray<FBackendLootContainer>& Live)
	{
		USpaceMMOLootSubsystem* Self = WeakThis.Get();
		UWorld* World = Self != nullptr ? Self->GetWorld() : nullptr;

		if (World == nullptr)
		{
			return;
		}

		TSet<int64> Ids;

		for (const FBackendLootContainer& Container : Live)
		{
			Ids.Add(Container.Id);
			Self->SpawnCrate(Container);
		}

		for (TActorIterator<ASpaceMMOLootCrateActor> It(World); It; ++It)
		{
			if (!Ids.Contains((*It)->GetContainerId()))
			{
				(*It)->Destroy();
			}
		}

		UE_LOG(LogSpaceMMOBackend, Log, TEXT("Loot: %d crates lying in the world."), Live.Num());
	});
}

ASpaceMMOLootCrateActor* USpaceMMOLootSubsystem::NearestCrate(
	const FSystemCoordinate& Where, const double RangeMetres, double* OutDistanceMetres) const
{
	UWorld* World = GetWorld();

	if (World == nullptr)
	{
		return nullptr;
	}

	ASpaceMMOLootCrateActor* Best = nullptr;
	double BestMetres = RangeMetres;

	// Measured in system space, as the deposits are: world space is each machine's own render origin.
	for (TActorIterator<ASpaceMMOLootCrateActor> It(World); It; ++It)
	{
		ASpaceMMOLootCrateActor* Crate = *It;

		if (Crate == nullptr || Crate->GetContainerId() == 0 || Crate->GetItems().IsEmpty())
		{
			continue;
		}

		const double Metres = FVector::Distance(Crate->GetSystemPosition().Kilometres, Where.Kilometres) * 1000.0;

		if (Metres <= BestMetres)
		{
			Best = Crate;
			BestMetres = Metres;
		}
	}

	if (OutDistanceMetres != nullptr)
	{
		*OutDistanceMetres = BestMetres;
	}

	return Best;
}
