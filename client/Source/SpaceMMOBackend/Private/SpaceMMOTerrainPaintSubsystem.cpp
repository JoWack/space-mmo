#include "SpaceMMOTerrainPaintSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "SpaceMMOBackendClient.h"
#include "SpaceMMOBackendLog.h"
#include "SpaceMMOPlanetActor.h"
#include "SpaceMMOWorldSubsystem.h"

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

void USpaceMMOTerrainPaintSubsystem::BuildPlanetsForPlacedBodies(
	UWorld& World, const USpaceMMOBackendClient& Backend)
{
	USpaceMMOWorldSubsystem* const Scenery = World.GetSubsystem<USpaceMMOWorldSubsystem>();

	if (Scenery == nullptr)
	{
		return;
	}

	// Every body is drawn at the radius the starting planet carries, whatever its authored
	// radiusKm says.
	//
	// <strong>Asked of the thing that draws it, not written down again here.</strong> The same
	// accessor the authoring preview scales its markers against, for the same reason: the radius is
	// compiled in rather than authored (task 123), and the day it becomes content this is one of
	// the two places that has to notice. A second copy of "20" here would agree with it right up
	// until one was edited.
	const FPlanetConfig Drawn = USpaceMMOWorldSubsystem::StartingPlanet();

	int32 Built = 0;
	int32 Unplaced = 0;

	for (const FBackendBody& Body : Backend.GetBodies())
	{
		// A body nobody has placed is a working state: it exists in the database and is drawn by
		// nobody. Counted rather than warned about, because until 8 September that was every body
		// in the pack and a warning per world would be noise rather than news.
		if (!Body.bHasSystemPosition || Body.Key.IsEmpty())
		{
			++Unplaced;

			continue;
		}

		FPlanetConfig Config = Drawn;
		Config.Centre = FSystemCoordinate(Body.SystemPositionKilometres);

		FPlanetTerrainConfig Terrain = USpaceMMOWorldSubsystem::StartingPlanetTerrain();

		// Shaped here as well as in the paint pass below, so a planet is never built wearing the
		// starting world's terrain and then reshaped a frame later. The paint pass still applies
		// it -- it is what handles a body reshaped after the planets already exist -- and it skips
		// a planet that already has the shape content asked for, so this costs nothing twice.
		if (Body.bHasTerrain)
		{
			Terrain.Seed = Body.TerrainSeed;
			Terrain.MaxElevationKilometres = Body.MaxElevationKilometres;
			Terrain.BaseFrequency = Body.BaseFrequency;
		}

		if (Scenery->EnsurePlanet(Body.Key, Config, Terrain) != nullptr)
		{
			++Built;
		}
	}

	// Counted off the world rather than off the loop above.
	//
	// <strong>Built is what was asked for; this is what is standing there.</strong> They differ if a
	// spawn fails, and -- the case worth catching -- if something else has already built a planet
	// this pass did not account for. Two subsystems build planets now, so "5 of 5" from a loop that
	// called EnsurePlanet five times would say exactly that while six planets stood in the world.
	// Measuring the built thing rather than the thing that configures it is the rule this project
	// keeps relearning.
	int32 InTheWorld = 0;

	for (TActorIterator<ASpaceMMOPlanetActor> It(&World); It; ++It)
	{
		InTheWorld += *It != nullptr ? 1 : 0;
	}

	UE_LOG(LogSpaceMMOBackend, Log,
		TEXT("%d of %d body(ies) have a planet; %d are authored nowhere; %d planet(s) in the "
			"world."),
		Built, Backend.GetBodies().Num(), Unplaced, InTheWorld);
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

	BuildPlanetsForPlacedBodies(*World, *Backend);

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
