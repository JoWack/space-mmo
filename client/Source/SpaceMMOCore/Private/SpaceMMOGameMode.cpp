#include "SpaceMMOGameMode.h"

#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "SpaceMMOLog.h"
#include "SpaceMMOCharacterPawn.h"
#include "SpaceMMOPlanetActor.h"
#include "SpaceMMOShipPawn.h"
#include "SpaceMMOTestScene.h"
#include "SpaceMMOWorldSubsystem.h"

ASpaceMMOGameMode::ASpaceMMOGameMode()
{
	// On foot, per ADR-0012: nobody starts with a ship. This used to be the ship pawn, so every
	// connection spawned flying.
	DefaultPawnClass = ASpaceMMOCharacterPawn::StaticClass();
}

void ASpaceMMOGameMode::InitGame(
	const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);

	// Resolved by path rather than by including the header, so Core keeps knowing nothing about
	// the backend module — the same boundary that put deposits and gathering over there. A path is
	// a name, not a link: this compiles and runs with SpaceMMOBackend absent, falling back to a
	// plain controller and connections with no identity.
	if (UClass* Identified = LoadClass<APlayerController>(
		nullptr, TEXT("/Script/SpaceMMOBackend.SpaceMMOPlayerController")))
	{
		PlayerControllerClass = Identified;
	}
	else
	{
		UE_LOG(LogSpaceMMO, Warning,
			TEXT("SpaceMMOPlayerController not found; connections will have no character."));
	}
}

void ASpaceMMOGameMode::StartPlay()
{
	Super::StartPlay();

	if (bSpawnTestScene)
	{
		SpawnTestScene();
	}
}

double ASpaceMMOGameMode::MaxTerrainRise()
{
	return USpaceMMOWorldSubsystem::StartingPlanetTerrain().MaxElevationKilometres;
}

APawn* ASpaceMMOGameMode::SpawnDefaultPawnAtTransform_Implementation(
	AController* NewPlayer, const FTransform& SpawnTransform)
{
	UWorld* World = GetWorld();

	if (World == nullptr)
	{
		return Super::SpawnDefaultPawnAtTransform_Implementation(NewPlayer, SpawnTransform);
	}

	const FVector Direction = StartingDirection.GetSafeNormal();

	if (Direction.IsNearlyZero())
	{
		// A zero vector names no point on a sphere at all, so there is no sensible default to
		// substitute -- the same reasoning that makes a deposit with no direction get dropped
		// rather than placed at the planet's core.
		UE_LOG(LogSpaceMMO, Warning,
			TEXT("StartingDirection is zero; spawning at the placed transform instead."));

		return Super::SpawnDefaultPawnAtTransform_Implementation(NewPlayer, SpawnTransform);
	}

	// Read as a constant rather than found in the world. A connection is given its pawn before
	// USpaceMMOWorldSubsystem::OnWorldBeginPlay has spawned the planet -- 323 ms apart, measured --
	// so looking for the actor found nothing and dropped the character near the system origin, 59 km
	// from a planet it then fell towards.
	const FPlanetConfig Planet = USpaceMMOWorldSubsystem::StartingPlanet();

	const FVector Centre = Planet.Centre.Kilometres;
	const double Radius = Planet.RadiusKilometres;

	// Placed a little above the surface and left to fall the last few metres, so ground contact
	// catches the character rather than the spawn positioning it onto the ground by hand -- which
	// is what proves the height function and the mesh agree about where the ground is.
	//
	// Clear of the hills as well as the sphere: terrain rises up to MaxElevationKilometres above the
	// nominal radius, so a drop measured from the radius alone starts inside whatever is there.
	const FVector Position =
		Centre + (Direction * (Radius + MaxTerrainRise() + StartingDropKilometres));

	ASpaceMMOCharacterPawn* Character = World->SpawnActorDeferred<ASpaceMMOCharacterPawn>(
		ASpaceMMOCharacterPawn::StaticClass(),
		FTransform::Identity,
		nullptr,
		nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

	if (Character == nullptr)
	{
		return Super::SpawnDefaultPawnAtTransform_Implementation(NewPlayer, SpawnTransform);
	}

	// Before FinishSpawning, not after. BeginPlay resolves the ground and aligns to it, so a
	// position set afterwards arrives too late and the first frame is spent somewhere else.
	Character->SetStartingSystemPosition(Position);
	Character->FinishSpawning(FTransform::Identity);

	// Said out loud because a spawn that lands somewhere unintended looks exactly like terrain
	// being in the wrong place, and the two are diagnosed in completely different files.
	// Names the planet it was placed against, not just a distance. The previous version of this line
	// printed a distance from a centre it had been handed, so it read as correct while being
	// measured from the wrong place -- a diagnostic that agreed with the bug.
	UE_LOG(LogSpaceMMO, Log,
		TEXT("Spawned a character on foot at %s km: %.2f km from the planet at %s (radius %.1f km)."),
		*Position.ToString(),
		(Position - Centre).Size(),
		*Centre.ToString(),
		Radius);

	return Character;
}

void ASpaceMMOGameMode::SpawnTestScene()
{
	UWorld* World = GetWorld();

	if (World == nullptr)
	{
		return;
	}

	// Scenery is built by USpaceMMOWorldSubsystem on every machine, not here. A game mode only
	// exists on the server, so anything it spawned would be invisible to every client.

	// A ship on the ground beside where the player appears, unpossessed, for them to walk over and
	// board.
	//
	// Scaffolding, and it says so: ADR-0012 has nobody starting with a ship, and 115 will replace
	// this with a hull that is crafted and summoned. Until then flight -- the most-tested thing in
	// this project -- would otherwise be unreachable from a standing start, which is a poor trade
	// for a change about where a character stands.
	//
	// Turning bSpawnStarterShip off is how "nobody starts with a ship" gets tested before the
	// questline that grants one exists.
	if (bSpawnStarterShip)
	{
		const FVector Direction = StartingDirection.GetSafeNormal();

		if (Direction.IsNearlyZero())
		{
			return;
		}

		const FPlanetConfig Planet = USpaceMMOWorldSubsystem::StartingPlanet();

		const FVector Centre = Planet.Centre.Kilometres;
		const double Radius = Planet.RadiusKilometres;

		// Any direction perpendicular to "up" works, and this one is stable: the character pawn
		// builds its own frame the same way, so the ship lands beside the player rather than at
		// some angle that changes with the starting direction.
		const FVector East =
			FVector::CrossProduct(Direction, FVector::UpVector).GetSafeNormal().IsNearlyZero()
				? FVector::CrossProduct(Direction, FVector::ForwardVector).GetSafeNormal()
				: FVector::CrossProduct(Direction, FVector::UpVector).GetSafeNormal();

		// Thirty metres away: close enough to see on a 283 m horizon, far enough that it is a walk
		// rather than a thing the player spawns inside.
		const FVector Position =
			Centre
			+ (Direction * (Radius + MaxTerrainRise() + StartingDropKilometres))
			+ (East * 0.03);

		if (ASpaceMMOShipPawn* Ship = World->SpawnActorDeferred<ASpaceMMOShipPawn>(
			ASpaceMMOShipPawn::StaticClass(),
			FTransform::Identity,
			nullptr,
			nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
		{
			Ship->SetStartingSystemPosition(Position);
			Ship->FinishSpawning(FTransform::Identity);

			UE_LOG(LogSpaceMMO, Log,
				TEXT("Spawned a starter ship 30 m from the player's start, for boarding."));
		}
	}
}
