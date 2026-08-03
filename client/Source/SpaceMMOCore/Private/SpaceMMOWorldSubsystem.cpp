#include "SpaceMMOWorldSubsystem.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "SpaceMMOLog.h"
#include "SpaceMMOPlanetActor.h"
#include "SpaceMMOTestScene.h"

namespace
{
	// Directional light intensity is in lux, and lux is unforgiving: three is roughly twilight,
	// which is why the scene went dark the moment auto-exposure stopped compensating for it.
	// Daylight is orders of magnitude higher; these are a starting point, not a physical claim.
	float GKeyLightLux = 14.0f;
	float GFillLightLux = 3.0f;

	FAutoConsoleVariableRef CVarKeyLight(
		TEXT("SpaceMMO.KeyLight"),
		GKeyLightLux,
		TEXT("Key light intensity in lux. Applies immediately."),
		ECVF_Default);

	FAutoConsoleVariableRef CVarFillLight(
		TEXT("SpaceMMO.FillLight"),
		GFillLightLux,
		TEXT("Fill light intensity in lux, lighting the side facing away from the key. Applies immediately."),
		ECVF_Default);
}

bool USpaceMMOWorldSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}

	// Only for worlds that are actually played. Editor preview and inactive worlds would otherwise
	// each get their own planet, which is confusing at best.
	const UWorld* World = Cast<UWorld>(Outer);

	return World != nullptr
		&& (World->WorldType == EWorldType::Game
			|| World->WorldType == EWorldType::PIE);
}

void USpaceMMOWorldSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	BuildScenery();
}

void USpaceMMOWorldSubsystem::Tick(const float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Only on change, so this is a comparison per frame rather than a light update per frame.
	if (KeyLight != nullptr && !FMath::IsNearlyEqual(KeyLight->Intensity, GKeyLightLux))
	{
		KeyLight->SetIntensity(GKeyLightLux);
	}

	if (FillLight != nullptr && !FMath::IsNearlyEqual(FillLight->Intensity, GFillLightLux))
	{
		FillLight->SetIntensity(GFillLightLux);
	}
}

void USpaceMMOWorldSubsystem::BuildScenery()
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

	// A planet to fly to. 20 km radius at 60 km.
	//
	// It was 200 km, which was unflyable: at the ship's acceleration that is around two and a half
	// minutes of unbroken thrust, during which a distant sphere barely changes apparent size while
	// the marker lattice streams past three kilometres apart. The planet looked stationary and
	// everything else looked fast, which reads as the planet running away. Sixty kilometres is far
	// enough to be a real approach and close enough to be worth making.
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
		Config.Centre = FSystemCoordinate(60.0, 0.0, 0.0);
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
	if (ADirectionalLight* KeyLightActor = World->SpawnActor<ADirectionalLight>(
		ADirectionalLight::StaticClass(),
		FTransform(FRotator(-45.0, 45.0, 0.0)),
		SpawnParameters))
	{
		// GetLightComponent rather than GetComponent: it is declared on ALight, so it survives
		// every target configuration, and the cast is checked.
		if (UDirectionalLightComponent* Component =
			Cast<UDirectionalLightComponent>(KeyLightActor->GetLightComponent()))
		{
			Component->SetMobility(EComponentMobility::Movable);
			Component->SetIntensity(GKeyLightLux);

			KeyLight = Component;
		}
	}

	// Fill, from roughly the opposite side, so the half of a planet facing away from the key light
	// is dim rather than absent.
	//
	// A second directional light rather than a sky light, which is what this was and why it did
	// nothing: a sky light captures its surroundings to produce ambient, and out here there is no
	// sky, no atmosphere and no horizon to capture. It faithfully captured black and scaled it,
	// leaving every unlit surface at zero — so a planet read as one white hemisphere and one black
	// one, with nothing in between.
	//
	// Dim and cool, so it reads as bounced starlight rather than a second sun.
	if (ADirectionalLight* FillLightActor = World->SpawnActor<ADirectionalLight>(
		ADirectionalLight::StaticClass(),
		FTransform(FRotator(-15.0, 215.0, 0.0)),
		SpawnParameters))
	{
		if (UDirectionalLightComponent* Component =
			Cast<UDirectionalLightComponent>(FillLightActor->GetLightComponent()))
		{
			Component->SetMobility(EComponentMobility::Movable);
			Component->SetIntensity(GFillLightLux);
			Component->SetLightColor(FLinearColor(0.45f, 0.52f, 0.7f));

			FillLight = Component;

			// No shadows from the fill. Two shadow-casting suns on a sphere produce crossing
			// terminators that read as a rendering fault rather than as lighting.
			Component->SetCastShadows(false);
		}
	}
#endif
}
