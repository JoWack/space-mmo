#include "SpaceMMOWorldSubsystem.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/TextureCube.h"
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
	float GAmbientLux = 2.0f;

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

	/**
	 * Whether the key light casts shadows at all.
	 *
	 * A toggle rather than a decision, because "this ground is dark" has two completely different
	 * causes — no light reaching it, or light reaching it and being shadowed out — and they are
	 * indistinguishable by looking. One keypress separates them.
	 */
	float GKeyLightShadows = 1.0f;

	FAutoConsoleVariableRef CVarKeyLight(
		TEXT("SpaceMMO.KeyLight"),
		GKeyLightLux,
		TEXT("Key light intensity in lux. Applies immediately."),
		ECVF_Default);

	FAutoConsoleVariableRef CVarKeyLightShadows(
		TEXT("SpaceMMO.KeyShadows"),
		GKeyLightShadows,
		TEXT("1 for key light shadows, 0 for none. Dark ground that lights up at 0 was shadowed, "
			"not unlit. Applies immediately."),
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

FPlanetConfig USpaceMMOWorldSubsystem::StartingPlanet()
{
	// 20 km radius at 60 km.
	//
	// It was 200 km, which was unflyable: at the ship's acceleration that is around two and a half
	// minutes of unbroken thrust, during which a distant sphere barely changes apparent size while
	// the marker lattice streams past three kilometres apart. The planet looked stationary and
	// everything else looked fast, which reads as the planet running away. Sixty kilometres is far
	// enough to be a real approach and close enough to be worth making.
	FPlanetConfig Config;
	Config.Centre = FSystemCoordinate(60.0, 0.0, 0.0);
	Config.RadiusKilometres = 20.0;
	Config.SurfaceGravity = 981.0;
	Config.AtmosphereHeightKilometres = 12.0;

	return Config;
}

FPlanetTerrainConfig USpaceMMOWorldSubsystem::StartingPlanetTerrain()
{
	// Half a kilometre of relief on a 20 km world is 2.5% of the radius. Earth's tallest mountain
	// is 0.14% of Earth's, so this planet is roughly eighteen times as rugged, and now that the
	// whole globe is drawn from these numbers rather than approximated by a ball that is something
	// you can see from orbit rather than a detail of the landing zone.
	//
	// Left as it is on purpose: the lighting was tuned against this terrain, and a peak several
	// times the height of the horizon is what makes the ground read as ground on a planet this
	// small. Lowering it toward 0.15 would make the planet rounder from space at the cost of
	// flattening what a player walks on.
	FPlanetTerrainConfig Terrain;
	Terrain.Seed = 20260801;
	Terrain.MaxElevationKilometres = 0.5;

	// Twelve features per radius rather than two, which is what gives the ground slopes at all.
	//
	// Relief was always half a kilometre and that is not what was wrong: spread over two features
	// per radius it made broad swells, and the steepest ground anywhere on the planet was 5.9
	// degrees. Measured, by sweeping the parameter and reading the result rather than looking at
	// it -- 12 gives 31.8 degrees, and lifts the height range inside a single 1.4 km patch from
	// 0.31..0.37 to 0.37..0.78. Both matter: a material that bands on height or steepness has
	// nothing to work with when neither varies across everything a player can see.
	//
	// 24 gives 47 degrees and 48 gives 70 if this reads too gentle. Going the other way makes the
	// planet smooth again and silently stops any slope-based material from doing anything, which
	// is what TerrainHasSlopesToShade exists to catch.
	Terrain.BaseFrequency = 12.0;

	return Terrain;
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

	if (KeyLight != nullptr)
	{
		const bool bWanted = GKeyLightShadows > 0.5f;

		if (KeyLight->CastShadows != bWanted)
		{
			KeyLight->SetCastShadows(bWanted);
		}
	}

	if (FillLight != nullptr && !FMath::IsNearlyEqual(FillLight->Intensity, GFillLightLux))
	{
		FillLight->SetIntensity(GFillLightLux);
	}

	if (SkyLight != nullptr && !FMath::IsNearlyEqual(SkyLight->Intensity, GAmbientLux))
	{
		SkyLight->SetIntensity(GAmbientLux);
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
		PlanetActor->SetPlanetConfig(StartingPlanet());

		// Half a kilometre of relief on a 20 km world is 2.5% of the radius. Earth's tallest
		// mountain is 0.14% of Earth's, so this planet is roughly eighteen times as rugged, and
		// now that the whole globe is drawn from these numbers rather than approximated by a ball
		// that is something you can see from orbit rather than a detail of the landing zone.
		//
		// Left as it is on purpose: the lighting was tuned against this terrain, and a peak
		// several times the height of the horizon is what makes the ground read as ground on a
		// planet this small. Lowering it toward 0.15 would make the planet rounder from space at
		// the cost of flattening what a player walks on.
		PlanetActor->SetTerrainConfig(StartingPlanetTerrain());
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

			// Shadow settings for a planet rather than for a room.
			//
			// The defaults assume a scene a few hundred metres across: cascades stop at 200 m, and
			// the depth bias is scaled for surfaces a few metres apart. Here a single terrain mesh
			// spans kilometres, so within the cascade range it shadows itself — the ground goes
			// black out to the last cascade and then snaps to fully lit beyond it, which reads as a
			// bright band at a fixed distance from the camera rather than as light at all.
			//
			// Further cascades and a much larger bias trade shadow crispness, which nothing here
			// needs, for ground that is lit where the sun is above it.
			Component->DynamicShadowDistanceMovableLight = 400000.0f;
			Component->DynamicShadowCascades = 4;
			Component->ShadowBias = 3.0f;
			Component->ShadowSlopeBias = 3.0f;

			// Names this as the sun. With more than one directional light the renderer has to pick
			// one for forward shading, translucency and fog, and it warns on screen that it is
			// guessing by brightness — which means the choice could change when a light is dimmed.
			Component->ForwardShadingPriority = 1;

			Component->MarkRenderStateDirty();

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

	// Ambient, from a sky light that has been given something to emit.
	//
	// Three attempts got this wrong in the same way. A sky light in captured mode photographs its
	// surroundings, and out here the surroundings are empty black space, so it captured black.
	// Specified-cubemap mode with no cubemap assigned emits nothing either. Six dim directional
	// lights along the axes replaced it and looked reasonable in code, but the renderer does not
	// treat a crowd of directional lights as an ambient cube — the engine says so on screen, and
	// the proof was a frame with blown-out white ore sitting on black ground, which cannot happen
	// if every normal is receiving light from three directions at once.
	//
	// The missing piece was never the mechanism, it was the cubemap. The engine ships one.
	if (ASkyLight* SkyLightActor = World->SpawnActor<ASkyLight>(
		ASkyLight::StaticClass(), FTransform::Identity, SpawnParameters))
	{
		if (USkyLightComponent* Component = SkyLightActor->GetLightComponent())
		{
			Component->SetMobility(EComponentMobility::Movable);
			Component->SourceType = ESkyLightSourceType::SLS_SpecifiedCubemap;

			// LoadObject rather than a ConstructorHelpers finder: this runs when the world starts,
			// and a finder outside a constructor asserts.
			UTextureCube* AmbientCubemap = LoadObject<UTextureCube>(
				nullptr,
				TEXT("/Engine/MapTemplates/Sky/DaylightAmbientCubemap.DaylightAmbientCubemap"));

			if (AmbientCubemap != nullptr)
			{
				Component->Cubemap = AmbientCubemap;
			}
			else
			{
				UE_LOG(LogSpaceMMO, Warning,
					TEXT("No ambient cubemap found; the sky light will emit nothing, as before."));
			}

			// The lower hemisphere is not black, because on a sphere there is no such thing as a
			// surface that only faces up. Walk far enough and what was the underside is the ground.
			Component->bLowerHemisphereIsBlack = false;

			Component->SetIntensity(GAmbientLux);
			Component->SetLightColor(FLinearColor(0.55f, 0.62f, 0.85f));

			// A specified cubemap still has to be processed before it lights anything.
			Component->RecaptureSky();

			SkyLight = Component;
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
