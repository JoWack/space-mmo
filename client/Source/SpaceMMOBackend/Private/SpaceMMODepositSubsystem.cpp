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
#include "SpaceMMOShipPawn.h"
#include "SpaceMMOStationActor.h"
#include "SpaceMMOTerrainPaintSubsystem.h"

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

	// The third thing placement waits for, and the one that was missing.
	//
	// Stations are positioned by asking the terrain function where the ground is, so they cannot go
	// down until the planet is wearing the terrain it will keep. That arrives from content on the
	// same OnBodiesLoaded broadcast this subsystem listens to, which means the two handlers race:
	// win it and the station sits on the ground, lose it and the station is placed against the
	// compiled-in default and the ground is reshaped out from under it. About a hundred metres, on
	// the capital, and intermittent across restarts because it turns on which HTTP response landed
	// first.
	if (USpaceMMOTerrainPaintSubsystem* const Paint =
		InWorld.GetSubsystem<USpaceMMOTerrainPaintSubsystem>())
	{
		Paint->OnPlanetsPainted.AddDynamic(
			this, &USpaceMMODepositSubsystem::HandlePlanetsPainted);
	}

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

FString USpaceMMODepositSubsystem::SceneBodyKey() const
{
	// The body this client starts on, which is the one DefaultGame.ini names.
	//
	// <strong>This used to return the first planet in the world carrying a key</strong>, and the
	// comment defended it: what is drawn is what content belongs on, and asking the built thing
	// beats asking what configured it. That was true while the scene held exactly one planet and
	// became false the moment content could place five (task 157) -- "the first planet the actor
	// iterator returns" is now an arbitrary world, and this decides which body's deposits are
	// fetched. Ore would have appeared on whichever planet happened to spawn first.
	//
	// The configured value is not a guess at what the planet will be. It is what the starting
	// planet wears: BuildScenery does not set a key, so that actor takes the class default, which
	// is this.
	const FString Configured = GetDefault<ASpaceMMOPlanetActor>()->BodyKey;

	if (!Configured.IsEmpty())
	{
		return Configured;
	}

	// Nothing configured at all. Fall back to any planet that names a body, so a scene assembled
	// some other way still has an answer rather than none.
	if (UWorld* const World = GetWorld())
	{
		for (TActorIterator<ASpaceMMOPlanetActor> It(World); It; ++It)
		{
			const ASpaceMMOPlanetActor* const Planet = *It;

			if (Planet != nullptr && !Planet->BodyKey.IsEmpty())
			{
				return Planet->BodyKey;
			}
		}
	}

	return Configured;
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

	const FString BodyKey = SceneBodyKey();

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

	// Said on the way through, not only on failure. Which body a scene is populated from used to
	// be answerable only by reading two files, and it was wrong for months without a symptom.
	UE_LOG(LogSpaceMMOBackend, Log,
		TEXT("Scene is body '%s' (id %d); its deposits and stations are the ones that belong."),
		*BodyKey, Body.Id);

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

	UE_LOG(LogSpaceMMOBackend, Log, TEXT("SELFTEST: gathering %s as character %d."),
		*PlacedDeposits[0]->GetNode().Key, SelfTestCharacterId);

	Backend->GatherAsServer(SelfTestCharacterId, PlacedDeposits[0]->GetNode().Id);
}

void USpaceMMODepositSubsystem::AttachDocking(AActor* Actor)
{
	// Ships and characters, and nothing else. Both, because you dock a ship and the docked state
	// then belongs to the character rather than to whichever body they are wearing — which is why
	// disembarking at a station leaves you docked.
	//
	// "Any pawn" was too broad: the spectator pawn the client holds before possession got one too,
	// bound the key to it, and pressing G did nothing because a spectator has no character. The
	// only evidence was one log line reading "Dock key bound on SpectatorPawn_0".
	const bool bCanDock =
		Actor != nullptr
		&& (Actor->IsA<ASpaceMMOShipPawn>() || Actor->IsA<ASpaceMMOCharacterPawn>());

	APawn* Pawn = bCanDock ? Cast<APawn>(Actor) : nullptr;

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

void USpaceMMODepositSubsystem::HandlePlanetsPainted()
{
	PlaceStationsWhenReady();
}

void USpaceMMODepositSubsystem::PlaceStationsWhenReady()
{
	// Three answers are needed and they arrive in any order: stations are asked for immediately,
	// bodies take a round trip to resolve a content key, and the planet is reshaped from that
	// body's authored terrain. Placing on whichever lands first would mean that on the ordering
	// where stations win, every body-relative station is compared against a scene body of zero,
	// matches nothing, and is silently skipped — a world with one deep-space station in it and no
	// error anywhere.
	//
	// The terrain was the one nobody added. Placement asks the terrain function where the ground
	// is, so a station placed before the planet is reshaped is measured against the compiled-in
	// default and then left floating when the real ground arrives — and nothing about it looks
	// wrong, because the station and its own copy of the terrain config agree with each other
	// perfectly. It was found from a playtest where the same build put the station on the ground
	// one run and a hundred metres above it the next.
	const USpaceMMOTerrainPaintSubsystem* const Paint =
		GetWorld() != nullptr
			? GetWorld()->GetSubsystem<USpaceMMOTerrainPaintSubsystem>()
			: nullptr;

	const bool bGroundSettled = Paint == nullptr || Paint->HavePlanetsSettled();

	if (bStationsPlaced || !bStationsLoaded || SceneBodyId == 0 || !bGroundSettled)
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

	// Every station is measured against the planet standing in for its own body.
	//
	// <strong>It used to be measured against the first planet the iterator returned.</strong> That
	// was correct only because the scene held exactly one, and it is why the whole of this loop's
	// old story was about skipping: four of the six seeded stations belonged to bodies nothing
	// drew, so they were counted and dropped. Now that content places bodies (task 157) the same
	// question has an answer per station, and the fault it prevents is worse than the one it
	// replaces -- an outpost measured against another world's terrain is buried or floating, and
	// looks exactly like a station measured correctly.
	TMap<int32, const ASpaceMMOPlanetActor*> PlanetsByBodyId;

	{
		TMap<FString, int32> BodyIdsByKey;

		for (const FBackendBody& Body : Backend->GetBodies())
		{
			BodyIdsByKey.Add(Body.Key, Body.Id);
		}

		for (TActorIterator<ASpaceMMOPlanetActor> It(World); It; ++It)
		{
			const ASpaceMMOPlanetActor* const Planet = *It;

			if (Planet == nullptr || Planet->BodyKey.IsEmpty())
			{
				continue;
			}

			if (const int32* const BodyId = BodyIdsByKey.Find(Planet->BodyKey))
			{
				PlanetsByBodyId.Add(*BodyId, Planet);
			}
		}
	}

	int32 Drawn = 0;

	int32 Skipped = 0;

	for (const FBackendStation& Station : Backend->GetStations())
	{
		// A station on a body needs that body's planet to stand on. Deep-space ones do not, which
		// is why a missing planet is only fatal for the first kind -- and why this loop continues
		// rather than returning.
		const ASpaceMMOPlanetActor* const Standing =
			Station.bPlaced && Station.bOnBody
				? PlanetsByBodyId.FindRef(Station.BodyId)
				: nullptr;

		if (Station.bPlaced && Station.bOnBody && Standing == nullptr)
		{
			// Named rather than counted silently. A station whose body content has not placed is
			// the one thing that still cannot be drawn, and the reason is now specific enough to
			// act on: put a systemPosition on that body.
			UE_LOG(LogSpaceMMOBackend, Warning,
				TEXT("Station %s stands on body %d, which has no planet in the world; "
					"has that body been given a systemPosition?"),
				*Station.Key, Station.BodyId);

			++Skipped;

			continue;
		}

		const FPlanetConfig Planet =
			Standing != nullptr ? Standing->GetPlanetConfig() : FPlanetConfig();

		const FPlanetTerrainConfig Terrain =
			Standing != nullptr ? Standing->GetTerrainConfig() : FPlanetTerrainConfig();

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

		// <strong>Identity, which is what it was spawned at -- not the transform Configure just
		// set (task 163).</strong> AActor::FinishSpawning compares the transform it is handed with
		// the one SpawnActorDeferred was given, and when they differ it assumes the caller wants
		// both and composes them: "TemplateTransform * UserTransform" (Actor.cpp:4403). Handing it
		// the actor's own transform, scale 25 already on it, made a 625 m cube of every placeholder
		// station and a 1.2 km one of Deepdock, while the log said "25 m" on every line. The
		// Capital escaped because its Hull is hidden under a Blueprint with absolute scale, so the
		// one station anybody looked at was the one that could not show it. Joe had to walk three
		// hundred metres out of Terra Outpost to see it from the outside.
		//
		// The same word the planet actor and the ship pawn already use, for the same reason.
		Placed->FinishSpawning(FTransform::Identity);

		// The scale the actor actually ended up with, read after FinishSpawning rather than
		// assumed from what Configure asked for (task 163). The two disagreed by a factor of
		// twenty-five for as long as stations have existed, and the log said "25 m" throughout.
		UE_LOG(LogSpaceMMOBackend, Log,
			TEXT("Station %s finished spawning at actor scale %s."),
			*Station.Key, *Placed->GetActorScale3D().ToCompactString());

		PlacedStations.Add(Placed);

		// The shape it was placed against, named per station. A station measured against the wrong
		// terrain looks exactly like one measured against the right terrain -- it is only wrong
		// relative to the ground everybody else can see -- so the seed is the one value that tells
		// the two apart, and with five worlds in the scene one line for all of them would name at
		// most one of the five seeds actually used.
		if (Standing != nullptr)
		{
			UE_LOG(LogSpaceMMOBackend, Log,
				TEXT("Station %s placed on '%s' against terrain seed %lld, relief %.2f km, "
					"frequency %.1f."),
				*Station.Key,
				*Standing->BodyKey,
				Terrain.Seed,
				Terrain.MaxElevationKilometres,
				Terrain.BaseFrequency);
		}

		Drawn += Station.bPlaced ? 1 : 0;
	}

	UE_LOG(LogSpaceMMOBackend, Log,
		TEXT("Placed %d station(s), %d drawable; skipped %d on bodies content has not placed."),
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
	//
	// The scene's own planet specifically, matched by body key rather than taken as the first the
	// iterator returns. Deposits are fetched for exactly one body -- SceneBodyId -- so placing them
	// against another world's terrain would bury or float every rock on the planet, and each one
	// would look correctly placed from every direction except standing next to it.
	const FString BodyKey = SceneBodyKey();

	const ASpaceMMOPlanetActor* PlanetActor = nullptr;

	for (TActorIterator<ASpaceMMOPlanetActor> It(World); It; ++It)
	{
		if (*It != nullptr && It->BodyKey == BodyKey)
		{
			PlanetActor = *It;

			break;
		}
	}

	if (PlanetActor == nullptr)
	{
		UE_LOG(LogSpaceMMOBackend, Warning,
			TEXT("No planet for body '%s' in the world; its deposits have nothing to stand on."),
			*BodyKey);

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

		// Identity, for the reason the station spawn above gives at length -- and here it is
		// closing a trap rather than fixing a fault, which was measured rather than assumed.
		//
		// Deposits looked like the same bug: the same deferred spawn, the same scaled root, the
		// same transform handed back to FinishSpawning. They read 2.04 before this change and 2.04
		// after it, because a deposit scales itself in ApplyRenderTransform from BeginPlay -- after
		// FinishSpawning -- so its root was still at Identity when the engine compared, and nothing
		// was composed. The station scales in Configure, before, and was. One line of timing was
		// the whole difference, and it would have become the same 625 m fault the day somebody
		// moved that scale into Configure for a good reason.
		Deposit->FinishSpawning(FTransform::Identity);

		UE_LOG(LogSpaceMMOBackend, Log,
			TEXT("Deposit %s finished spawning at actor scale %s."),
			*Node.Key, *Deposit->GetActorScale3D().ToCompactString());

		PlacedDeposits.Add(Deposit);
	}

	UE_LOG(LogSpaceMMOBackend, Log, TEXT("Placed %d deposit(s) on %s."),
		PlacedDeposits.Num(), *SceneBodyKey());
}
