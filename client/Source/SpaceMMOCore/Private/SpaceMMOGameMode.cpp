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

	// Scenery is built by USpaceMMOWorldSubsystem on every machine, not here. A game mode only
	// exists on the server, so anything it spawned would be invisible to every client.

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

}
