#include "SpaceMMOTerrainPaintSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "SpaceMMOBackendClient.h"
#include "SpaceMMOBackendLog.h"
#include "SpaceMMOPlanetActor.h"

void USpaceMMOTerrainPaintSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	const UGameInstance* const GameInstance = InWorld.GetGameInstance();

	USpaceMMOBackendClient* const Backend = GameInstance != nullptr
		? GameInstance->GetSubsystem<USpaceMMOBackendClient>()
		: nullptr;

	if (Backend == nullptr)
	{
		return;
	}

	Backend->OnBodiesLoaded.AddDynamic(this, &USpaceMMOTerrainPaintSubsystem::HandleBodiesLoaded);

	// And once now, in case they already arrived -- a level transition keeps the subsystem's client
	// and its body list, so waiting for a fetch that has already happened would wait forever.
	PaintPlanets();
}

void USpaceMMOTerrainPaintSubsystem::HandleBodiesLoaded()
{
	PaintPlanets();
}

void USpaceMMOTerrainPaintSubsystem::PaintPlanets()
{
	UWorld* const World = GetWorld();

	const UGameInstance* const GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;

	const USpaceMMOBackendClient* const Backend = GameInstance != nullptr
		? GameInstance->GetSubsystem<USpaceMMOBackendClient>()
		: nullptr;

	if (World == nullptr || Backend == nullptr || Backend->GetBodies().Num() == 0)
	{
		// Nothing has arrived yet. Deliberately not counted as settled: whatever is waiting on
		// that signal would then place itself against the compiled-in default terrain, which is
		// the race this exists to close.
		return;
	}

	for (TActorIterator<ASpaceMMOPlanetActor> It(World); It; ++It)
	{
		ASpaceMMOPlanetActor* const Planet = *It;

		if (Planet == nullptr || Planet->BodyKey.IsEmpty())
		{
			continue;
		}

		const FBackendBody* const Body = Backend->GetBodies().FindByPredicate(
			[Planet](const FBackendBody& Candidate) { return Candidate.Key == Planet->BodyKey; });

		if (Body == nullptr)
		{
			// Named, because a mistyped key and an unpainted body both leave the planet grey and
			// only one of them is a mistake somebody wants telling about.
			UE_LOG(LogSpaceMMOBackend, Warning,
				TEXT("No body '%s' to paint %s from; it keeps its configured material."),
				*Planet->BodyKey, *Planet->GetName());

			continue;
		}

		// Shape first, then colour. Setting terrain rebuilds the globe and drops the patch, and
		// doing that after painting would leave the new meshes carrying the old material instance
		// for a frame -- which is a flicker nobody would be able to account for.
		if (Body->bHasTerrain)
		{
			FPlanetTerrainConfig Shape = Planet->GetTerrainConfig();
			Shape.Seed = Body->TerrainSeed;
			Shape.MaxElevationKilometres = Body->MaxElevationKilometres;
			Shape.BaseFrequency = Body->BaseFrequency;

			// Only when something actually differs. This runs on every bodies-loaded broadcast, and
			// rebuilding a hundred thousand triangles to arrive at the shape already on screen is
			// a stutter with nothing to show for it.
			const FPlanetTerrainConfig Current = Planet->GetTerrainConfig();

			const bool bChanged =
				Current.Seed != Shape.Seed
				|| !FMath::IsNearlyEqual(Current.MaxElevationKilometres, Shape.MaxElevationKilometres)
				|| !FMath::IsNearlyEqual(Current.BaseFrequency, Shape.BaseFrequency);

			if (bChanged)
			{
				UE_LOG(LogSpaceMMOBackend, Log,
					TEXT("Shaping %s from body '%s': seed %lld, relief %.2f km, frequency %.1f."),
					*Planet->GetName(),
					*Body->Key,
					Shape.Seed,
					Shape.MaxElevationKilometres,
					Shape.BaseFrequency);

				Planet->SetTerrainConfig(Shape);
			}
		}

		if (!Body->bHasAppearance)
		{
			UE_LOG(LogSpaceMMOBackend, Log,
				TEXT("Body '%s' has no authored palette; %s keeps its configured material."),
				*Body->Key, *Planet->GetName());

			continue;
		}

		Planet->SetTerrainPalette(
			Body->LowColour,
			Body->HighColour,
			Body->RockColour,
			FVector4(Body->HeightFrom, Body->HeightTo, Body->SlopeFrom, Body->SlopeTo));
	}

	// Said once, and said even when nothing needed shaping. A body with no authored terrain is a
	// working state, and a gate waiting for a signal that only fires on the interesting path would
	// wait forever -- which is a world with no stations in it and nothing in the log about why.
	if (!bPlanetsSettled)
	{
		bPlanetsSettled = true;

		UE_LOG(LogSpaceMMOBackend, Log,
			TEXT("Planets have the shape they will keep; anything placed on the ground may go "
				"down now."));

		OnPlanetsPainted.Broadcast();
	}
}
