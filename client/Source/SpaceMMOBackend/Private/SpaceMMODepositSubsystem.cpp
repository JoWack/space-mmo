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
	// Stations and deposits are positioned by asking the terrain function where the ground is, so
	// they cannot go down until the planet is wearing the terrain it will keep. That arrives from content on the
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

	Backend->FetchBodies();

	// Stations and deposits need no body resolved first — both are asked for all at once and
	// carry the body they belong to — so these go out immediately rather than waiting behind the
	// bodies. Deposits used to wait, because the request had to name one body by an id resolved
	// from a configured key; that is the one-body fetch task 158 removed.
	Backend->FetchStations();
	Backend->FetchDeposits();

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

void USpaceMMODepositSubsystem::HandleBodiesLoaded()
{
	bBodiesLoaded = true;

	// Loud, because the alternative is a world that silently has nothing standing on it. With no
	// bodies the paint subsystem never declares the ground settled, so neither stations nor
	// deposits are placed, and the only other trace is a count of zero in a Log line.
	const UWorld* const World = GetWorld();

	const UGameInstance* const GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;

	const USpaceMMOBackendClient* const Backend =
		GameInstance != nullptr
			? GameInstance->GetSubsystem<USpaceMMOBackendClient>()
			: nullptr;

	if (Backend != nullptr && Backend->GetBodies().Num() == 0)
	{
		UE_LOG(LogSpaceMMOBackend, Warning,
			TEXT("No bodies loaded; no stations or deposits will be placed. Has content been "
				"seeded?"));
	}

	PlaceStationsWhenReady();
	PlaceDepositsWhenReady();
}

void USpaceMMODepositSubsystem::HandleDepositsLoaded()
{
	bDepositsLoaded = true;

	PlaceDepositsWhenReady();
}

void USpaceMMODepositSubsystem::RunGatherSelfTest() const
{
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
	PlaceDepositsWhenReady();
}

bool USpaceMMODepositSubsystem::IsGroundReady() const
{
	// Two of the three answers everything on the ground waits for, and they arrive in any order
	// with the thing being placed: bodies take a round trip, and the planets are then built and
	// reshaped from each body's authored terrain. Placing on whichever lands first would mean that
	// on the ordering where the placed things win, every body-relative one is compared against no
	// planet at all, matches nothing, and is silently skipped — a world with one deep-space
	// station in it, no ore, and no error anywhere.
	//
	// The terrain was the one nobody added. Placement asks the terrain function where the ground
	// is, so anything placed before the planet is reshaped is measured against the compiled-in
	// default and then left floating when the real ground arrives — and nothing about it looks
	// wrong, because the actor and its own copy of the terrain config agree with each other
	// perfectly. It was found from a playtest where the same build put the station on the ground
	// one run and a hundred metres above it the next.
	const USpaceMMOTerrainPaintSubsystem* const Paint =
		GetWorld() != nullptr
			? GetWorld()->GetSubsystem<USpaceMMOTerrainPaintSubsystem>()
			: nullptr;

	const bool bGroundSettled = Paint == nullptr || Paint->HavePlanetsSettled();

	return bBodiesLoaded && bGroundSettled;
}

void USpaceMMODepositSubsystem::PlaceStationsWhenReady()
{
	if (bStationsPlaced || !bStationsLoaded || !IsGroundReady())
	{
		return;
	}

	bStationsPlaced = true;

	PlaceStations();
}

void USpaceMMODepositSubsystem::PlaceDepositsWhenReady()
{
	// The same gate as stations, and new for deposits. They never needed one while they were
	// fetched behind the bodies -- a second round trip cannot land before the first, and the
	// planets are shaped inside the first's broadcast -- but asked for up front they can be the
	// first thing to arrive, and a deposit placed then stands on a planet that is not there yet.
	if (bDepositsPlaced || !bDepositsLoaded || !IsGroundReady())
	{
		return;
	}

	bDepositsPlaced = true;

	PlaceDeposits();

	RunGatherSelfTest();
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
	const TMap<int32, const ASpaceMMOPlanetActor*> PlanetsByBodyId = PlanetsByBody(*Backend);

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

TMap<int32, const ASpaceMMOPlanetActor*> USpaceMMODepositSubsystem::PlanetsByBody(
	const USpaceMMOBackendClient& Backend) const
{
	TMap<int32, const ASpaceMMOPlanetActor*> PlanetsByBodyId;

	UWorld* const World = GetWorld();

	if (World == nullptr)
	{
		return PlanetsByBodyId;
	}

	TMap<FString, int32> BodyIdsByKey;

	for (const FBackendBody& Body : Backend.GetBodies())
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

	return PlanetsByBodyId;
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

	// Each deposit is measured against the planet standing in for its own body, exactly as a
	// station is. The planet's configuration is read off the planet itself rather than copied
	// here: two hard-coded copies of a radius and a terrain seed would agree right up until one
	// was edited, and then deposits would sit at the altitude of a planet that no longer exists.
	//
	// <strong>This used to find one planet, by the configured scene body's key, and place every
	// deposit on it.</strong> Every deposit was on that body, because only that body's had been
	// fetched, so it was never wrong -- it was simply the whole of the world's ore standing on one
	// of five worlds (task 158).
	const TMap<int32, const ASpaceMMOPlanetActor*> PlanetsByBodyId = PlanetsByBody(*Backend);

	TSet<int32> BodiesWithOre;

	int32 Skipped = 0;

	for (const FBackendResourceNode& Node : Backend->GetDeposits())
	{
		const ASpaceMMOPlanetActor* const Standing = PlanetsByBodyId.FindRef(Node.BodyId);

		if (Standing == nullptr)
		{
			// Named rather than counted silently, for the reason the station loop gives: a
			// deposit on a body content has not placed is served, real, and drawn by nothing, and
			// the fix is specific enough to act on.
			UE_LOG(LogSpaceMMOBackend, Warning,
				TEXT("Deposit %s is on body %d, which has no planet in the world; "
					"has that body been given a systemPosition?"),
				*Node.Key, Node.BodyId);

			++Skipped;

			continue;
		}

		const FPlanetConfig Planet = Standing->GetPlanetConfig();
		const FPlanetTerrainConfig Terrain = Standing->GetTerrainConfig();

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

		// The shape it was placed against, named per deposit, for the reason the station line
		// gives: a rock measured against the wrong terrain looks exactly like one measured against
		// the right terrain, and the seed is the one value that tells the two apart. Five different
		// seeds across the log is what says each world got its own ore; one seed ten times is
		// this task back again.
		UE_LOG(LogSpaceMMOBackend, Log,
			TEXT("Deposit %s placed on '%s' against terrain seed %lld, relief %.2f km, "
				"frequency %.1f."),
			*Node.Key,
			*Standing->BodyKey,
			Terrain.Seed,
			Terrain.MaxElevationKilometres,
			Terrain.BaseFrequency);

		PlacedDeposits.Add(Deposit);

		BodiesWithOre.Add(Node.BodyId);
	}

	UE_LOG(LogSpaceMMOBackend, Log,
		TEXT("Placed %d deposit(s) across %d body/bodies; skipped %d on bodies content has not "
			"placed."),
		PlacedDeposits.Num(), BodiesWithOre.Num(), Skipped);
}
