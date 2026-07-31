#include "SpaceMMOGameMode.h"

#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "SpaceMMOLog.h"
#include "SpaceMMOPlanetActor.h"
#include "SpaceMMOShipPawn.h"
#include "SpaceMMOTestScene.h"

ASpaceMMOGameMode::ASpaceMMOGameMode()
{
	DefaultPawnClass = ASpaceMMOShipPawn::StaticClass();
}

void ASpaceMMOGameMode::StartPlay()
{
	Super::StartPlay();

	if (bSpawnTestScene)
	{
		SpawnTestScene();
	}
}

void ASpaceMMOGameMode::SpawnTestScene()
{
	UWorld* World = GetWorld();

	if (World == nullptr)
	{
		return;
	}

	// Everything is spawned from code rather than placed in a map, because a .umap is a binary
	// asset and this project has none yet. It also means the scene cannot drift out of step with
	// the code it exists to demonstrate.
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	const ASpaceMMOTestScene* Scene = World->SpawnActor<ASpaceMMOTestScene>(
		ASpaceMMOTestScene::StaticClass(), FTransform::Identity, SpawnParameters);

	UE_LOG(LogSpaceMMO, Log, TEXT("Spawned test scene: %s"),
		Scene != nullptr ? TEXT("ok") : TEXT("FAILED"));

	// A planet to fly to. 20 km radius at 200 km, so it starts as a small sphere ahead and the
	// whole orbital-to-atmospheric-to-surface transition is reachable in a couple of minutes.
	// Spawned deferred so the configuration is in place before BeginPlay runs. A plain SpawnActor
	// begins play immediately, so anything set afterwards arrives too late — the planet would
	// briefly exist at the system origin with default settings, and BeginPlay would report them.
	if (ASpaceMMOPlanetActor* PlanetActor = World->SpawnActorDeferred<ASpaceMMOPlanetActor>(
		ASpaceMMOPlanetActor::StaticClass(),
		FTransform::Identity,
		nullptr,
		nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
	{
		FPlanetConfig Config;
		Config.Centre = FSystemCoordinate(200.0, 0.0, 0.0);
		Config.RadiusKilometres = 20.0;
		Config.SurfaceGravity = 981.0;
		Config.AtmosphereHeightKilometres = 12.0;

		PlanetActor->SetPlanetConfig(Config);
		PlanetActor->FinishSpawning(FTransform::Identity);
	}

	// A key light at an angle, so the marker cubes read as solid objects rather than flat
	// silhouettes and it is possible to tell which way one is facing.
	if (ADirectionalLight* KeyLight = World->SpawnActor<ADirectionalLight>(
		ADirectionalLight::StaticClass(),
		FTransform(FRotator(-45.0, 45.0, 0.0)),
		SpawnParameters))
	{
		if (UDirectionalLightComponent* Component = KeyLight->GetComponent())
		{
			Component->SetIntensity(4.0f);
			Component->SetMobility(EComponentMobility::Movable);
		}
	}

	// Ambient fill, or everything not facing the key light is pure black — which in an empty scene
	// means half of every marker simply disappears.
	if (ASkyLight* Fill = World->SpawnActor<ASkyLight>(
		ASkyLight::StaticClass(), FTransform::Identity, SpawnParameters))
	{
		if (USkyLightComponent* Component = Fill->GetLightComponent())
		{
			Component->SetMobility(EComponentMobility::Movable);
			Component->SetIntensity(1.5f);
			Component->SetLightColor(FLinearColor(0.35f, 0.4f, 0.55f));
		}
	}
}
