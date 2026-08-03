#include "SpaceMMODepositSubsystem.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "SpaceMMOBackendClient.h"
#include "SpaceMMOBackendLog.h"
#include "SpaceMMOCharacterPawn.h"
#include "SpaceMMODepositActor.h"
#include "SpaceMMOGatheringComponent.h"
#include "SpaceMMOPlayerController.h"
#include "SpaceMMOPlanetActor.h"

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

	// Bodies first, so the deposit request can name a body by an id resolved from its content key.
	Backend->FetchBodies();

	// Character pawns are spawned on demand rather than placed, so both cases have to be covered:
	// any that already exist, and any that appear later.
	for (TActorIterator<ASpaceMMOCharacterPawn> It(&InWorld); It; ++It)
	{
		AttachGathering(*It);
	}

	ActorSpawnedHandle = InWorld.AddOnActorSpawnedHandler(
		FOnActorSpawned::FDelegate::CreateUObject(this, &USpaceMMODepositSubsystem::AttachGathering));
}

void USpaceMMODepositSubsystem::AttachGathering(AActor* Actor)
{
	ASpaceMMOCharacterPawn* Pawn = Cast<ASpaceMMOCharacterPawn>(Actor);

	if (Pawn == nullptr || Pawn->FindComponentByClass<USpaceMMOGatheringComponent>() != nullptr)
	{
		return;
	}

	USpaceMMOGatheringComponent* Gathering =
		NewObject<USpaceMMOGatheringComponent>(Pawn, TEXT("Gathering"));

	if (Gathering == nullptr)
	{
		return;
	}

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

	Backend->FetchDeposits(Body.Id);
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
