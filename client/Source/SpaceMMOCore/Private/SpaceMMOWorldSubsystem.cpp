#include "SpaceMMOWorldSubsystem.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PostProcessVolume.h"
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
	float GKeyLightLux = 25.0f;
	float GFillLightLux = 6.0f;

	/**
	 * Manual exposure, in stops.
	 *
	 * Auto-exposure is off (DefaultEngine.ini) and nothing replaced it, so the renderer used a
	 * fixed default calibrated for nothing in particular. Everything brighter than it clamped to
	 * white and everything dimmer crushed to black, which is why the ground reads as two colours
	 * with no surface in between rather than as a lit shape.
	 *
	 * Negative darkens. This is the knob that decides whether 25 lux is a blown-out field or a lit
	 * hillside, and it can only be set by looking.
	 */
	float GExposureBias = 8.0f;

	/**
	 * A dim omnidirectional fill, so no surface is ever unlit.
	 *
	 * A sphere is the awkward case: two directional lights leave every normal facing away from both
	 * of them at exactly zero, and on a planet you are constantly walking around into those. A sky
	 * light is the right answer because it comes from everywhere, and the earlier attempt failed
	 * only because it was set to capture a scene that is empty black space. Given a colour to emit
	 * rather than a scene to photograph, it does the job it was always meant to.
	 */
	float GAmbientLux = 1.5f;

	FAutoConsoleVariableRef CVarExposure(
		TEXT("SpaceMMO.Exposure"),
		GExposureBias,
		TEXT("Manual exposure bias in stops. Negative darkens. Applies immediately."),
		ECVF_Default);

	FAutoConsoleVariableRef CVarAmbient(
		TEXT("SpaceMMO.Ambient"),
		GAmbientLux,
		TEXT("Omnidirectional fill so nothing is pure black. Applies immediately."),
		ECVF_Default);

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

	for (UDirectionalLightComponent* Ambient : AmbientLights)
	{
		if (Ambient != nullptr && !FMath::IsNearlyEqual(Ambient->Intensity, GAmbientLux))
		{
			Ambient->SetIntensity(GAmbientLux);
		}
	}

	if (Exposure != nullptr
		&& !FMath::IsNearlyEqual(Exposure->Settings.AutoExposureBias, GExposureBias))
	{
		Exposure->Settings.AutoExposureBias = GExposureBias;
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

	// Ambient, built out of directional lights pointing six ways.
	//
	// Two sky lights have now failed here for the same underlying reason: a sky light reports what
	// a sky is doing, and there is no sky. Captured mode photographed empty space and emitted
	// nothing; specified-cubemap mode with no cubemap also emits nothing, which is the version that
	// shipped and did nothing at any value.
	//
	// Six dim lights along the axes is an ambient cube by hand. Every surface normal faces towards
	// at least three of them, so nothing is ever unlit however far around the planet somebody has
	// walked — which is the property a sphere actually needs and the one two opposed suns cannot
	// give. Crude, but it is light that arrives, which beats a correct-looking configuration that
	// emits nothing.
	//
	// Shadows off on all six. They exist to lift black, and six shadow-casting lights would both
	// cost a great deal and reintroduce the darkness they were added to remove.
	static const FVector AmbientDirections[] =
	{
		FVector::ForwardVector, -FVector::ForwardVector,
		FVector::RightVector, -FVector::RightVector,
		FVector::UpVector, -FVector::UpVector,
	};

	for (const FVector& Direction : AmbientDirections)
	{
		if (ADirectionalLight* AmbientActor = World->SpawnActor<ADirectionalLight>(
			ADirectionalLight::StaticClass(),
			FTransform(Direction.Rotation()),
			SpawnParameters))
		{
			if (UDirectionalLightComponent* Component =
				Cast<UDirectionalLightComponent>(AmbientActor->GetLightComponent()))
			{
				Component->SetMobility(EComponentMobility::Movable);
				Component->SetIntensity(GAmbientLux);
				Component->SetLightColor(FLinearColor(0.35f, 0.42f, 0.6f));
				Component->SetCastShadows(false);

				AmbientLights.Add(Component);
			}
		}
	}

	// An unbound post-process volume, purely to own the exposure.
	//
	// Disabling auto-exposure did not set a manual one; it just stopped the renderer adapting, and
	// left whatever fixed value it defaults to. Nothing in the project has ever chosen the exposure
	// this scene is viewed at, which is the difference between a lit surface and a white one.
	if (APostProcessVolume* Volume = World->SpawnActor<APostProcessVolume>(
		APostProcessVolume::StaticClass(), FTransform::Identity, SpawnParameters))
	{
		Volume->bUnbound = true;
		Volume->BlendWeight = 1.0f;

		Volume->Settings.bOverride_AutoExposureMethod = true;
		Volume->Settings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;

		Volume->Settings.bOverride_AutoExposureBias = true;
		Volume->Settings.AutoExposureBias = GExposureBias;

		Exposure = Volume;
	}
#endif
}
