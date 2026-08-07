#include "SpaceMMODepositSubsystem.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "SpaceMMOBackendClient.h"
#include "SpaceMMOBackendLog.h"
#include "SpaceMMOCharacterPawn.h"
#include "SpaceMMODepositActor.h"
#include "SpaceMMODockingComponent.h"
#include "SpaceMMOGatheringComponent.h"
#include "SpaceMMOPlayerController.h"
#include "SpaceMMOPlanetActor.h"
#include "SpaceMMOStationActor.h"

bool USpaceMMODepositSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}

	// Played worlds only, matching the scenery subsystem. An editor preview world would otherwise
	// issue its own HTTP requests and spawn its own copy of every deposit.
	const UWorld* World = Cast<UWorld>(Outer);

	return World != nullptr
		&& (World->WorldType == EWorldType::Game
			|| World->WorldType == EWorldType::PIE);
}

void USpaceMMODepositSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	const UGameInstance* GameInstance = InWorld.GetGameInstance();

	USpaceMMOBackendClient* Backend =
		GameInstance != nullptr
			? GameInstance->GetSubsystem<USpaceMMOBackendClient>()
			: nullptr;

	if (Backend == nullptr)
	{
		UE_LOG(LogSpaceMMOBackend, Warning,
			TEXT("No backend client; the world will have no deposits in it."));

		return;
	}

	Backend->OnBodiesLoaded.AddDynamic(this, &USpaceMMODepositSubsystem::HandleBodiesLoaded);
	Backend->OnDepositsLoaded.AddDynamic(this, &USpaceMMODepositSubsystem::HandleDepositsLoaded);
	Backend->OnStationsLoaded.AddDynamic(this, &USpaceMMODepositSubsystem::HandleStationsLoaded);

	// Bodies first, so the deposit request can name a body by an id resolved from its content key.
	Backend->FetchBodies();

	// Stations need no body resolved first — they are asked for all at once and carry whichever
	// position they have — so this can go out immediately rather than waiting behind the bodies.
	Backend->FetchStations();

	// Character pawns are spawned on demand rather than placed, so both cases have to be covered:
	// any that already exist, and any that appear later.
	// Every pawn, not only characters. Docking attaches to ships as well, and a ship already in
	// the world when this ran would otherwise never get the key.
	for (TActorIterator<APawn> It(&InWorld); It; ++It)
	{
		AttachGathering(*It);
	}

	ActorSpawnedHandle = InWorld.AddOnActorSpawnedHandler(
		FOnActorSpawned::FDelegate::CreateUObject(this, &USpaceMMODepositSubsystem::AttachGathering));
}

void USpaceMMODepositSubsystem::AttachGathering(AActor* Actor)
{
	AttachDocking(Actor);

	ASpaceMMOCharacterPawn* Pawn = Cast<ASpaceMMOCharacterPawn>(Actor);

	if (Pawn == nullptr || Pawn->FindComponentByClass<USpaceMMOGatheringComponent>() != nullptr)
	{
		return;
	}

	// Authority only. The component is replicated, so the server's copy arrives on each client by
	// itself — and a client that also made its own ended up with two, both binding the gather key,
	// so every press sent two requests and drew two rate-limited answers. The duplicate is invisible
	// in the world and only shows up as doubled traffic, or as the second copy quietly holding
	// character id zero because only one of them was ever told who the player is.
	if (!Pawn->HasAuthority())
	{
		return;
	}

	USpaceMMOGatheringComponent* Gathering =
		NewObject<USpaceMMOGatheringComponent>(Pawn, TEXT("Gathering"));

	if (Gathering == nullptr)
	{
		return;
	}

	UE_LOG(LogSpaceMMOBackend, Log,
		TEXT("Attaching gathering to %s (subsystem %s, world %s)."),
		*GetNameSafe(Pawn), *GetName(), *GetNameSafe(GetWorld()));

	Gathering->RegisterComponent();

	// Identity comes from the controller, which had to prove it to the backend — no longer from
	// the command line, which was a single-player convenience that would have credited every
	// player on a server to the same character.
	//
	// Read here as well as pushed from the controller because the two race: the backend round trip
	// can finish before or after a pawn is possessed, and only one of the two orders is covered by
	// each.
	if (const ASpaceMMOPlayerController* Controller =
		Cast<ASpaceMMOPlayerController>(Pawn->GetController()))
	{
		Gathering->CharacterId = Controller->GetCharacterId();
		Gathering->StationId = Controller->StationId;

		if (Gathering->CharacterId != 0)
		{
			UE_LOG(LogSpaceMMOBackend, Log, TEXT("%s will gather as character %d (%s)."),
				*GetNameSafe(Pawn), Gathering->CharacterId, *Controller->GetCharacterName());
		}
	}

	// Registration can happen before possession, in which case the pawn has no input component
	// yet and the component's own BeginPlay binding found nothing. Binding again here is harmless
	// when it already worked.
	Gathering->BindInput(Pawn->InputComponent);
}

void USpaceMMODepositSubsystem::HandleBodiesLoaded()
{
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

	FBackendBody Body;

	if (!Backend->FindBodyByKey(BodyKey, Body))
	{
		// Loud, because the alternative is a world that silently has no ore in it and a player
		// wondering where the mining content went.
		UE_LOG(LogSpaceMMOBackend, Warning,
			TEXT("No body with key '%s'; no deposits will be placed. Has content been seeded?"),
			*BodyKey);

		return;
	}

	// Remembered, because it is also the answer to "which stations belong on the planet this scene
	// actually has". Every other body is in the database with nowhere to stand.
	SceneBodyId = Body.Id;

	Backend->FetchDeposits(Body.Id);

	PlaceStationsWhenReady();
}

void USpaceMMODepositSubsystem::HandleDepositsLoaded(const int32 BodyId)
{
	PlaceDeposits();

	// Dev affordance: -GatherSelfTest fires one gather against the first deposit as soon as the
	// world is built, skipping the pawn, the key and the range check entirely. That isolates the
	// HTTP and credential half of the path, which is otherwise only reachable with a human at a
	// keyboard standing in the right spot. Same spirit as -BackendSmokeTest.
	if (!FParse::Param(FCommandLine::Get(), TEXT("GatherSelfTest")) || PlacedDeposits.Num() == 0)
	{
		return;
	}

	const UWorld* World = GetWorld();

	const UGameInstance* GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;

	USpaceMMOBackendClient* Backend =
		GameInstance != nullptr
			? GameInstance->GetSubsystem<USpaceMMOBackendClient>()
			: nullptr;

	if (Backend == nullptr || PlacedDeposits[0] == nullptr)
	{
		return;
	}

	int32 SelfTestCharacterId = 0;
	int32 SelfTestStationId = 0;

	FParse::Value(FCommandLine::Get(), TEXT("GatherCharacterId="), SelfTestCharacterId);
	FParse::Value(FCommandLine::Get(), TEXT("GatherStationId="), SelfTestStationId);

	UE_LOG(LogSpaceMMOBackend, Log, TEXT("SELFTEST: gathering %s as character %d."),
		*PlacedDeposits[0]->GetNode().Key, SelfTestCharacterId);

	Backend->GatherAsServer(
		SelfTestCharacterId, PlacedDeposits[0]->GetNode().Id, SelfTestStationId);
}

void USpaceMMODepositSubsystem::AttachDocking(AActor* Actor)
{
	// Any pawn a player can be in, not only the character. You dock a ship, and the docked state
	// then belongs to the character rather than to whichever body they are wearing — which is why
	// disembarking at a station leaves you docked.
	APawn* Pawn = Cast<APawn>(Actor);

	if (Pawn == nullptr || Pawn->FindComponentByClass<USpaceMMODockingComponent>() != nullptr)
	{
		return;
	}

	// Authority only, for the reason spelled out in AttachGathering: the component replicates, so
	// a client that made its own would end up with two, both bound to the key.
	if (!Pawn->HasAuthority())
	{
		return;
	}

	USpaceMMODockingComponent* Docking =
		NewObject<USpaceMMODockingComponent>(Pawn, TEXT("Docking"));

	if (Docking == nullptr)
	{
		return;
	}

	Docking->RegisterComponent();

	if (const ASpaceMMOPlayerController* Controller =
		Cast<ASpaceMMOPlayerController>(Pawn->GetController()))
	{
		Docking->CharacterId = Controller->GetCharacterId();
	}

	Docking->BindInput(Pawn->InputComponent);
}

void USpaceMMODepositSubsystem::HandleStationsLoaded()
{
	bStationsLoaded = true;

	PlaceStationsWhenReady();
}

void USpaceMMODepositSubsystem::PlaceStationsWhenReady()
{
	// Both answers are needed and they arrive in either order: stations are asked for immediately,
	// bodies take a round trip to resolve a content key. Placing on whichever lands first would
	// mean that on the ordering where stations win, every body-relative station is compared
	// against a scene body of zero, matches nothing, and is silently skipped — a world with one
	// deep-space station in it and no error anywhere.
	if (bStationsPlaced || !bStationsLoaded || SceneBodyId == 0)
	{
		return;
	}

	bStationsPlaced = true;

	PlaceStations();
}

void USpaceMMODepositSubsystem::PlaceStations()
{
	UWorld* World = GetWorld();

	if (World == nullptr)
	{
		return;
	}

	const UGameInstance* GameInstance = World->GetGameInstance();

	const USpaceMMOBackendClient* Backend =
		GameInstance != nullptr
			? GameInstance->GetSubsystem<USpaceMMOBackendClient>()
			: nullptr;

	if (Backend == nullptr)
	{
		return;
	}

	// Read off the planet rather than copied, for the same reason the deposits do it: two
	// hard-coded radii agree right up until one is edited, and then a station stands at the
	// altitude of a planet that no longer exists.
	const ASpaceMMOPlanetActor* PlanetActor = nullptr;

	for (TActorIterator<ASpaceMMOPlanetActor> It(World); It; ++It)
	{
		PlanetActor = *It;

		break;
	}

	const FPlanetConfig Planet =
		PlanetActor != nullptr ? PlanetActor->GetPlanetConfig() : FPlanetConfig();

	const FPlanetTerrainConfig Terrain =
		PlanetActor != nullptr ? PlanetActor->GetTerrainConfig() : FPlanetTerrainConfig();

	int32 Drawn = 0;

	int32 Skipped = 0;

	for (const FBackendStation& Station : Backend->GetStations())
	{
		// A station on a body needs a planet to stand on. Deep-space ones do not, which is why
		// the missing planet is only fatal for the first kind — and why this loop continues
		// rather than returning.
		if (Station.bPlaced && Station.bOnBody && PlanetActor == nullptr)
		{
			UE_LOG(LogSpaceMMOBackend, Warning,
				TEXT("No planet in the world; station %s has nothing to stand on."), *Station.Key);

			continue;
		}

		// And it needs to be *this* planet's station.
		//
		// The scene has one planet; the system has five. Placing every body-relative station
		// against the only planet present put all five outposts on the capital, a few hundred
		// metres apart, which read as one enormous sprawling structure rather than as four
		// stations that should not have been there at all. Skipping them is honest: those worlds
		// exist in the database and have nowhere to stand yet.
		if (Station.bPlaced && Station.bOnBody && Station.BodyId != SceneBodyId)
		{
			++Skipped;

			continue;
		}

		// Deferred, so Configure runs before BeginPlay. A plain SpawnActor begins play
		// immediately and the station would briefly occupy the system origin — the same ordering
		// mistake the deposits carry a comment about having made three times.
		ASpaceMMOStationActor* Placed = World->SpawnActorDeferred<ASpaceMMOStationActor>(
			ASpaceMMOStationActor::StaticClass(),
			FTransform::Identity,
			nullptr,
			nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

		if (Placed == nullptr)
		{
			continue;
		}

		Placed->Configure(Station, Planet, Terrain);
		Placed->FinishSpawning(Placed->GetActorTransform());

		PlacedStations.Add(Placed);

		Drawn += Station.bPlaced ? 1 : 0;
	}

	UE_LOG(LogSpaceMMOBackend, Log,
		TEXT("Placed %d station(s), %d drawable; skipped %d on bodies this scene does not have."),
		PlacedStations.Num(), Drawn, Skipped);
}

void USpaceMMODepositSubsystem::PlaceDeposits()
{
	UWorld* World = GetWorld();

	if (World == nullptr)
	{
		return;
	}

	const UGameInstance* GameInstance = World->GetGameInstance();

	const USpaceMMOBackendClient* Backend =
		GameInstance != nullptr
			? GameInstance->GetSubsystem<USpaceMMOBackendClient>()
			: nullptr;

	if (Backend == nullptr)
	{
		return;
	}

	// The planet's configuration is read off the planet itself rather than copied here. Two
	// hard-coded copies of a radius and a terrain seed would agree right up until one was edited,
	// and then deposits would sit at the altitude of a planet that no longer exists.
	const ASpaceMMOPlanetActor* PlanetActor = nullptr;

	for (TActorIterator<ASpaceMMOPlanetActor> It(World); It; ++It)
	{
		PlanetActor = *It;

		break;
	}

	if (PlanetActor == nullptr)
	{
		UE_LOG(LogSpaceMMOBackend, Warning,
			TEXT("No planet in the world; deposits have nothing to stand on."));

		return;
	}

	const FPlanetConfig Planet = PlanetActor->GetPlanetConfig();
	const FPlanetTerrainConfig Terrain = PlanetActor->GetTerrainConfig();

	for (const FBackendResourceNode& Node : Backend->GetDeposits())
	{
		// Deferred, so Configure runs before BeginPlay. A plain SpawnActor begins play
		// immediately and the deposit would log — and briefly occupy — the system origin. This
		// exact ordering mistake has been made three times in this project.
		ASpaceMMODepositActor* Deposit = World->SpawnActorDeferred<ASpaceMMODepositActor>(
			ASpaceMMODepositActor::StaticClass(),
			FTransform::Identity,
			nullptr,
			nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

		if (Deposit == nullptr)
		{
			continue;
		}

		Deposit->Configure(Node, Planet, Terrain);
		Deposit->FinishSpawning(Deposit->GetActorTransform());

		PlacedDeposits.Add(Deposit);
	}

	UE_LOG(LogSpaceMMOBackend, Log, TEXT("Placed %d deposit(s) on %s."),
		PlacedDeposits.Num(), *BodyKey);
}
