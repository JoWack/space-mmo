#include "SpaceMMOGameMode.h"

#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "SpaceMMOLog.h"
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
