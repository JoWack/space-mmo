#include "SpaceMMOGameMode.h"

#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "SpaceMMOLog.h"
#include "SpaceMMOCharacterPawn.h"
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

		// Terrain the planet streams in once a viewer is close enough to see it. Half a kilometre
		// of relief on a 20 km world is proportionally close to Everest on Earth — dramatic on
		// foot, and almost invisible from orbit, which is how a planet should read.
		FPlanetTerrainConfig Terrain;
		Terrain.Seed = 20260801;
		Terrain.MaxElevationKilometres = 0.5;

		PlanetActor->SetTerrainConfig(Terrain);
		PlanetActor->FinishSpawning(FTransform::Identity);
	}

	// A character standing on the planet, when asked for. Spawned on the far side from the ship's
	// approach so the two are visibly on different parts of the same sphere — which is the point
	// worth being able to see.
	if (FParse::Param(FCommandLine::Get(), TEXT("SpawnCharacter")))
	{
		double Radius = 20.0;
		double CharacterX = 0.0;
		double CharacterY = 0.0;
		double CharacterZ = 1.0;

		FParse::Value(FCommandLine::Get(), TEXT("CharacterDirX="), CharacterX);
		FParse::Value(FCommandLine::Get(), TEXT("CharacterDirY="), CharacterY);
		FParse::Value(FCommandLine::Get(), TEXT("CharacterDirZ="), CharacterZ);

		const FVector Direction = FVector(CharacterX, CharacterY, CharacterZ).GetSafeNormal();

		// Placed above the surface and left to fall the last few metres, so the log shows ground
		// contact actually catching it rather than it being positioned onto the ground by hand.
		const FVector Position =
			FVector(200.0, 0.0, 0.0) + (Direction * (Radius + 0.05));

		if (ASpaceMMOCharacterPawn* Character = World->SpawnActorDeferred<ASpaceMMOCharacterPawn>(
			ASpaceMMOCharacterPawn::StaticClass(),
			FTransform::Identity,
			nullptr,
			nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
		{
			// Before FinishSpawning, not after. BeginPlay resolves the ground and aligns the
			// character to it, so a position set afterwards arrives too late — exactly the
			// ordering that made the planet report the wrong centre.
			Character->SetStartingSystemPosition(Position);
			Character->FinishSpawning(FTransform::Identity);

			UE_LOG(LogSpaceMMO, Log, TEXT("Spawned character on the planet."));
		}
	}

	// ── Lighting ─────────────────────────────────────────────────────────────
	//
	// Compiled out of the dedicated server entirely, not merely skipped at runtime. A server has
	// no renderer, so parts of the light API do not exist in that build at all —
	// ADirectionalLight::GetComponent among them, which is what broke the first server compile.
	//
	// A runtime IsRunningDedicatedServer() check would not have helped: the code still has to
	// compile before it can decide not to run. This is the class of drift the server target was
	// added on day one to catch, and it caught it.
#if !UE_SERVER
	// A key light at an angle, so the marker cubes read as solid objects rather than flat
	// silhouettes and it is possible to tell which way one is facing.
	if (ADirectionalLight* KeyLight = World->SpawnActor<ADirectionalLight>(
		ADirectionalLight::StaticClass(),
		FTransform(FRotator(-45.0, 45.0, 0.0)),
		SpawnParameters))
	{
		// GetLightComponent rather than GetComponent: it is declared on ALight, so it survives
		// every target configuration, and the cast is checked.
		if (UDirectionalLightComponent* Component =
			Cast<UDirectionalLightComponent>(KeyLight->GetLightComponent()))
		{
			Component->SetMobility(EComponentMobility::Movable);
			Component->SetIntensity(4.0f);
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
#endif
}
