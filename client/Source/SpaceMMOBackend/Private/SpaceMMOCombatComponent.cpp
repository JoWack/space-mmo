#include "SpaceMMOCombatComponent.h"

#include "Components/InputComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"
#include "SpaceMMOBackendClient.h"
#include "SpaceMMOBackendLog.h"
#include "SpaceMMOBoltActor.h"
#include "SpaceMMOCharacterPawn.h"
#include "SpaceMMOPlanetActor.h"
#include "SpaceMMOPlanetTerrain.h"
#include "SpaceMMOPlayerController.h"
#include "SpaceMMORenderOrigin.h"
#include "SpaceMMOShot.h"
#include "SpaceMMOStationActor.h"

namespace
{
	/** How many of your recent hits to keep for numbers and bars: a burst, not a history. */
	constexpr int32 RecentHitLimit = 12;

	/** How far behind the server's clock a fire request may be and still count, as a fraction of the cooldown. */
	constexpr double CooldownTolerance = 0.8;

	/** Steps along a shot when asking whether the ground is in the way. */
	constexpr double TerrainStepMetres = 2.0;

	/**
	 * Fires from the console: SpaceMMO.Fire. The same path the key takes, so a binding that silently does
	 * nothing (it has happened to gathering and docking) can be told apart from a shot that was refused.
	 */
	FAutoConsoleCommandWithWorld GFireCommand(
		TEXT("SpaceMMO.Fire"),
		TEXT("Fires Weapon 1 at whatever the crosshair is on, as if the fire key were pressed."),
		FConsoleCommandWithWorldDelegate::CreateLambda(
			[](UWorld* World)
			{
				const APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
				const APawn* Pawn = Controller != nullptr ? Controller->GetPawn() : nullptr;

				if (USpaceMMOCombatComponent* Combat =
					Pawn != nullptr ? Pawn->FindComponentByClass<USpaceMMOCombatComponent>() : nullptr)
				{
					Combat->RequestFire();

					return;
				}

				UE_LOG(LogSpaceMMOBackend, Warning, TEXT("SpaceMMO.Fire: the possessed pawn cannot fire. On foot?"));
			}));

	FString RefusalMessage(const FString& Refusal)
	{
		// The API's reasons, in the words a player can act on. Unknown ones are shown as they came, rather than as
		// nothing: a refused shot that says nothing reads as a broken key.
		if (Refusal == TEXT("not_allowed"))
		{
			return TEXT("You can't attack them here");
		}

		if (Refusal == TEXT("no_weapon"))
		{
			return TEXT("Nothing in Weapon 1 fires");
		}

		if (Refusal == TEXT("target_dead") || Refusal == TEXT("attacker_dead"))
		{
			return FString();
		}

		return Refusal.IsEmpty() ? FString(TEXT("That shot was refused")) : Refusal;
	}
}

USpaceMMOCombatComponent::USpaceMMOCombatComponent()
{
	// Ticks for the key binding, as gathering's does: possession replaces the pawn's input component.
	PrimaryComponentTick.bCanEverTick = true;

	// Replicated, or its RPCs have no route and its health never arrives.
	SetIsReplicatedByDefault(true);
}

void USpaceMMOCombatComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Everybody's health, to everybody: bars over heads.
	DOREPLIFETIME(USpaceMMOCombatComponent, StoredHealth);
	DOREPLIFETIME(USpaceMMOCombatComponent, MaxHealth);
	DOREPLIFETIME(USpaceMMOCombatComponent, LastCombatServerSeconds);
	DOREPLIFETIME(USpaceMMOCombatComponent, bDead);
	DOREPLIFETIME(USpaceMMOCombatComponent, DeathFall);
	DOREPLIFETIME(USpaceMMOCombatComponent, DeathVariant);
	DOREPLIFETIME(USpaceMMOCombatComponent, DisplayName);

	// What they hold is theirs to know.
	DOREPLIFETIME_CONDITION(USpaceMMOCombatComponent, WeaponName, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(USpaceMMOCombatComponent, WeaponCooldownMs, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(USpaceMMOCombatComponent, WeaponRangeMetres, COND_OwnerOnly);
}

int32 USpaceMMOCombatComponent::HealthAt(
	const int32 Stored, const int32 Max, const double LastCombatSeconds, const double NowSeconds)
{
	if (Max <= 0 || Stored <= 0)
	{
		return 0;
	}

	const int32 Was = FMath::Min(Stored, Max);

	if (LastCombatSeconds < 0.0)
	{
		return Was;
	}

	const double Regenerating = NowSeconds - LastCombatSeconds - RegenDelaySeconds;

	if (Regenerating <= 0.0)
	{
		return Was;
	}

	// Whole milliseconds, as the API counts them, so the two agree to the point.
	const int64 Milliseconds = static_cast<int64>(Regenerating * 1000.0);
	const int64 Gained = static_cast<int64>(Max) * RegenPercentPerSecond * Milliseconds / 100000;

	return static_cast<int32>(FMath::Min<int64>(Max, Was + Gained));
}

int32 USpaceMMOCombatComponent::GetHealth() const
{
	return bDead ? 0 : HealthAt(StoredHealth, MaxHealth, LastCombatServerSeconds, ServerNowSeconds());
}

double USpaceMMOCombatComponent::ServerNowSeconds() const
{
	const UWorld* World = GetWorld();

	if (World == nullptr)
	{
		return 0.0;
	}

	const AGameStateBase* GameState = World->GetGameState();

	return GameState != nullptr ? GameState->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
}

ASpaceMMOPlayerController* USpaceMMOCombatComponent::OwningController() const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());

	return Pawn != nullptr ? Cast<ASpaceMMOPlayerController>(Pawn->GetController()) : nullptr;
}

USpaceMMOBackendClient* USpaceMMOCombatComponent::Backend() const
{
	const UWorld* World = GetWorld();
	const UGameInstance* GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;

	return GameInstance != nullptr ? GameInstance->GetSubsystem<USpaceMMOBackendClient>() : nullptr;
}

void USpaceMMOCombatComponent::BeginPlay()
{
	Super::BeginPlay();

	if (APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		BindInput(Pawn->InputComponent);
		Pawn->ReceiveRestartedDelegate.AddDynamic(this, &USpaceMMOCombatComponent::HandlePawnRestarted);
	}
}

void USpaceMMOCombatComponent::HandlePawnRestarted(APawn* Pawn)
{
	if (Pawn != nullptr)
	{
		BindInput(Pawn->InputComponent);
	}
}

void USpaceMMOCombatComponent::TickComponent(
	const float DeltaTime, const ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Compared against the component actually bound, as gathering's is (task 156): possession replaces it.
	const APawn* Pawn = Cast<APawn>(GetOwner());

	if (Pawn != nullptr)
	{
		BindInput(Pawn->InputComponent);
	}

#if !UE_BUILD_SHIPPING
	if (bLookSample && !RecentHits.IsEmpty())
	{
		const double Now = GetWorld()->GetRealTimeSeconds();

		RecentHits.Last().ArrivedSeconds = Now - 0.3;
		LastHitSeconds = Now - 0.05;

		// Kept on the head: the sample pawn may be falling, and a number left where it was put is soon off screen.
		if (const ASpaceMMOCharacterPawn* Sampled = Cast<ASpaceMMOCharacterPawn>(GetOwner()))
		{
			RecentHits.Last().PointKilometres =
				Sampled->GetSystemPosition().Kilometres + Sampled->GetSurfaceNormal().GetSafeNormal() * 0.0017;
		}

		LastCombatServerSeconds = ServerNowSeconds();
	}
#endif

	// Equipping happens between this client and the API, so the server never hears of it. Whatever changed Weapon 1
	// -- a double-click, a drag, a claimed rifle going straight into its slot -- this client sees it in its own
	// inventory, and says so once.
	if (Pawn != nullptr && Pawn->IsLocallyControlled() && !Pawn->HasAuthority())
	{
		if (const USpaceMMOBackendClient* Client = Backend())
		{
			int64 InWeaponOne = 0;

			for (const FBackendItemInstance& Instance : Client->GetItemInstances())
			{
				if (Instance.Kind == EBackendInventoryKind::Equipped && Instance.bEquipped
					&& Instance.EquippedSlot == EBackendEquipmentSlot::Weapon1)
				{
					InWeaponOne = Instance.Id;
				}
			}

			if (InWeaponOne != SeenWeaponInstanceId)
			{
				SeenWeaponInstanceId = InWeaponOne;
				RequestLoadoutRefresh();
			}
		}
	}
}

void USpaceMMOCombatComponent::BindInput(UInputComponent* InputComponent)
{
	if (InputComponent == nullptr || BoundInput.Get() == InputComponent)
	{
		return;
	}

	InputComponent->BindAction(TEXT("Fire"), IE_Pressed, this, &USpaceMMOCombatComponent::RequestFire);

	BoundInput = InputComponent;

	UE_LOG(LogSpaceMMOBackend, Log, TEXT("Fire key bound on %s (input %s)."),
		*GetNameSafe(GetOwner()), *GetNameSafe(InputComponent));
}

// ------------------------------------------------------------------------------------------------
// Identity and status (server)

void USpaceMMOCombatComponent::SetCharacter(const int32 InCharacterId, const FString& InDisplayName)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || InCharacterId == 0)
	{
		return;
	}

	const bool bChanged = CharacterId != InCharacterId;

	CharacterId = InCharacterId;
	DisplayName = InDisplayName;

	if (bChanged)
	{
		FetchStatus();
	}
}

void USpaceMMOCombatComponent::FetchStatus()
{
	USpaceMMOBackendClient* Client = Backend();

	if (Client == nullptr || CharacterId == 0)
	{
		return;
	}

	TWeakObjectPtr<USpaceMMOCombatComponent> WeakThis(this);

	Client->FetchCombatStatusAsServer(CharacterId, [WeakThis](const FBackendCombatStatus& Status)
	{
		USpaceMMOCombatComponent* Self = WeakThis.Get();

		if (Self == nullptr)
		{
			return;
		}

		const double Now = Self->ServerNowSeconds();

		Self->MaxHealth = Status.MaxHealth;
		Self->StoredHealth = Status.Health;
		Self->bDead = Status.Health <= 0;

		// Where health stands in coming back, not only what it is: within the 10 s wait, the wait continues; past
		// it, health now is the stored value and the clock sits exactly at the wait's end, so it keeps coming back
		// at the same rate from here.
		Self->LastCombatServerSeconds = Status.SecondsSinceCombat < 0.0
			? -1.0
			: Now - FMath::Min(Status.SecondsSinceCombat, RegenDelaySeconds);

		Self->WeaponName = Status.bHasWeapon ? Status.Weapon.Name : FString();
		Self->WeaponCooldownMs = Status.bHasWeapon ? Status.Weapon.CooldownMs : 0;
		Self->WeaponRangeMetres = Status.bHasWeapon ? Status.Weapon.RangeMetres : 0;

		if (ASpaceMMOCharacterPawn* Pawn = Cast<ASpaceMMOCharacterPawn>(Self->GetOwner()))
		{
			Pawn->SetMovementLocked(Self->bDead);
		}

		// Found dead at sign-in, on a machine that draws: lying down already (task 201).
		Self->ShowDeathOnBody();

		UE_LOG(LogSpaceMMOBackend, Log, TEXT("Combat: character %d has %d/%d health%s, holding %s."),
			Self->CharacterId, Status.Health, Status.MaxHealth, Self->bDead ? TEXT(" (dead)") : TEXT(""),
			Status.bHasWeapon ? *Status.Weapon.Name : TEXT("nothing that fires"));
	});
}

void USpaceMMOCombatComponent::RequestLoadoutRefresh()
{
	ServerRefreshLoadout();
}

void USpaceMMOCombatComponent::ServerRefreshLoadout_Implementation()
{
	FetchStatus();
}

void USpaceMMOCombatComponent::MarkInCombat(const double ServerNow, const int32 HealthNow)
{
	StoredHealth = HealthNow;
	LastCombatServerSeconds = ServerNow;
}

// ------------------------------------------------------------------------------------------------
// Firing

void USpaceMMOCombatComponent::RequestFire()
{
	ASpaceMMOPlayerController* Controller = OwningController();

	// Never through an open screen: a click on the inventory is a click on the inventory.
	if (Controller == nullptr || !Controller->IsLocalController() || Controller->IsAnyScreenOpen())
	{
		return;
	}

	if (bDead)
	{
		return;
	}

	if (WeaponName.IsEmpty())
	{
		Controller->ShowTransientMessage(RefusalMessage(TEXT("no_weapon")), ESpaceMMOMessageTone::Warning);

		return;
	}

	const UWorld* World = GetWorld();
	const double Now = World != nullptr ? World->GetRealTimeSeconds() : 0.0;

	// Dropped here rather than sent and refused: the server holds the same cooldown.
	if (LastRequestSeconds >= 0.0 && Now - LastRequestSeconds < WeaponCooldownMs / 1000.0)
	{
		return;
	}

	LastRequestSeconds = Now;

	FVector Aim;

	if (!AimPoint(Aim))
	{
		return;
	}

	const int32 Shot = ++ShotsSent;

	ShotSentSeconds.Add(Shot, FPlatformTime::Seconds());

	// A burst, not a history: an answer later than this is lost to the log anyway.
	ShotSentSeconds.Remove(Shot - 32);

	UE_LOG(LogSpaceMMOBackend, Log, TEXT("Combat: shot %d sent."), Shot);

	DrawOwnBolt(Aim);
	ServerFire(Aim, Shot);
}

double USpaceMMOCombatComponent::MillisecondsSinceSent(const int32 Shot) const
{
	const double* Sent = ShotSentSeconds.Find(Shot);

	return Sent != nullptr ? (FPlatformTime::Seconds() - *Sent) * 1000.0 : -1.0;
}

void USpaceMMOCombatComponent::DrawOwnBolt(const FVector& AimPointKilometres)
{
	UWorld* World = GetWorld();

	FTracedShot Traced;

	if (World == nullptr || !TraceShot(AimPointKilometres, false, Traced))
	{
		return;
	}

	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	if (ASpaceMMOBoltActor* Bolt = World->SpawnActor<ASpaceMMOBoltActor>(Spawn))
	{
		Bolt->Launch(
			FSystemCoordinate(Traced.MuzzleKilometres), FSystemCoordinate(Traced.EndKilometres), Traced.bLanded);
	}
}

bool USpaceMMOCombatComponent::AimPoint(FVector& OutKilometres) const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const APlayerController* Controller = Pawn != nullptr ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	UWorld* World = GetWorld();

	const USpaceMMORenderOriginSubsystem* Origin =
		World != nullptr ? World->GetSubsystem<USpaceMMORenderOriginSubsystem>() : nullptr;

	if (Controller == nullptr || Origin == nullptr)
	{
		return false;
	}

	FVector CameraWorld;
	FRotator CameraRotation;

	Controller->GetPlayerViewPoint(CameraWorld, CameraRotation);

	const FVector Direction = CameraRotation.Vector();
	const FVector CameraKilometres = Origin->GetRenderOrigin().Kilometres + CameraWorld / 100000.0;

	// The camera sits behind the shoulder, so its ray has a few metres further to go than the muzzle's.
	const double ReachMetres = WeaponRangeMetres + 10.0;

	double Nearest = ReachMetres;

	// Buildings and anything else solid under the crosshair.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SpaceMMOAim), false, Pawn);
	FHitResult Hit;

	if (World->LineTraceSingleByObjectType(
		Hit,
		CameraWorld,
		CameraWorld + Direction * ReachMetres * 100.0,
		FCollisionObjectQueryParams(ECC_TO_BITFIELD(ECC_WorldStatic) | ECC_TO_BITFIELD(ECC_WorldDynamic)),
		Params))
	{
		Nearest = FMath::Min(Nearest, Hit.Distance / 100.0);
	}

	// And people, by the same test the server makes.
	TArray<FSpaceMMOShotTarget> Targets;

	for (TActorIterator<ASpaceMMOCharacterPawn> It(World); It; ++It)
	{
		const ASpaceMMOCharacterPawn* Other = *It;

		if (Other == nullptr || Other == Pawn)
		{
			continue;
		}

		FSpaceMMOShotTarget Target;
		Target.Index = Targets.Num();
		Target.FeetKilometres = Other->GetSystemPosition().Kilometres;
		Target.Up = Other->GetSurfaceNormal();
		Target.Forward = Other->GetActorForwardVector();
		Target.RadiusMetres = Other->GetCollisionRadiusCentimetres() / 100.0;
		Target.HeightMetres = Other->GetBodyHeightCentimetres() / 100.0;

		Targets.Add(Target);
	}

	FSpaceMMOShotHit Person;

	if (SpaceMMO::Shot::Resolve(CameraKilometres, Direction, ReachMetres, Targets, Person))
	{
		Nearest = FMath::Min(Nearest, Person.DistanceMetres);
	}

	OutKilometres = CameraKilometres + Direction * Nearest / 1000.0;

	return true;
}

void USpaceMMOCombatComponent::ServerFire_Implementation(const FVector AimPointKilometres, const int32 Shot)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || bDead || CharacterId == 0 || WeaponName.IsEmpty())
	{
		return;
	}

	if (AimPointKilometres.ContainsNaN())
	{
		return;
	}

	const double Now = ServerNowSeconds();

	if (LastShotServerSeconds >= 0.0 && Now - LastShotServerSeconds < WeaponCooldownMs * CooldownTolerance / 1000.0)
	{
		// Said, because a click the server drops looks exactly like lag from the other end (task 198).
		UE_LOG(LogSpaceMMOBackend, Log, TEXT("Combat: shot %d from character %d dropped, %.0f ms after the last."),
			Shot, CharacterId, (Now - LastShotServerSeconds) * 1000.0);

		return;
	}

	LastShotServerSeconds = Now;

	Fire(AimPointKilometres, Shot, FPlatformTime::Seconds());
}

double USpaceMMOCombatComponent::ClearDistanceMetres(
	const FVector& FromKilometres, const FVector& Direction, const double WantedMetres) const
{
	UWorld* World = GetWorld();

	const USpaceMMORenderOriginSubsystem* Origin =
		World != nullptr ? World->GetSubsystem<USpaceMMORenderOriginSubsystem>() : nullptr;

	double Clear = WantedMetres;

	// Buildings, ships: anything with collision that is not a person.
	if (Origin != nullptr)
	{
		const FVector From = Origin->ToWorldLocation(FSystemCoordinate(FromKilometres));

		FCollisionQueryParams Params(SCENE_QUERY_STAT(SpaceMMOShot), false, GetOwner());
		FHitResult Hit;

		if (World->LineTraceSingleByObjectType(
			Hit,
			From,
			From + Direction * WantedMetres * 100.0,
			FCollisionObjectQueryParams(ECC_TO_BITFIELD(ECC_WorldStatic) | ECC_TO_BITFIELD(ECC_WorldDynamic)),
			Params))
		{
			Clear = FMath::Min(Clear, Hit.Distance / 100.0);
		}
	}

	// The ground, which is a function rather than anything a trace meets (ADR-0013): walked along the shot.
	const ASpaceMMOPlanetActor* Planet = nullptr;
	double PlanetDistance = TNumericLimits<double>::Max();

	for (TActorIterator<ASpaceMMOPlanetActor> It(World); It; ++It)
	{
		const double Distance = FVector::Distance((*It)->GetPlanetConfig().Centre.Kilometres, FromKilometres);

		if (Distance < PlanetDistance)
		{
			Planet = *It;
			PlanetDistance = Distance;
		}
	}

	if (Planet != nullptr)
	{
		const FPlanetConfig Config = Planet->GetPlanetConfig();
		const FPlanetTerrainConfig Terrain = Planet->GetTerrainConfig();

		for (double Along = TerrainStepMetres; Along < Clear; Along += TerrainStepMetres)
		{
			const FSystemCoordinate Point(FromKilometres + Direction * Along / 1000.0);

			if (FPlanetTerrain::AltitudeAboveGroundKilometres(Config, Terrain, Point) < 0.0)
			{
				Clear = Along;

				break;
			}
		}
	}

	return Clear;
}

bool USpaceMMOCombatComponent::TraceShot(
	const FVector& AimPointKilometres, const bool bIdentifiedOnly, FTracedShot& Out) const
{
	const ASpaceMMOCharacterPawn* Pawn = Cast<ASpaceMMOCharacterPawn>(GetOwner());
	UWorld* World = GetWorld();

	if (Pawn == nullptr || World == nullptr)
	{
		return false;
	}

	const FVector Up = Pawn->GetSurfaceNormal().GetSafeNormal();
	const double HeightMetres = Pawn->GetBodyHeightCentimetres() / 100.0;

	const FVector Muzzle = Pawn->GetSystemPosition().Kilometres
		+ Up * (HeightMetres * SpaceMMO::Shot::MuzzleHeightFraction) / 1000.0;

	FVector Direction = (AimPointKilometres - Muzzle).GetSafeNormal();

	if (Direction.IsNearlyZero())
	{
		Direction = Pawn->GetActorForwardVector();
	}

	const double Range = WeaponRangeMetres;

	// Everybody who could be hit: alive, someone, and within reach.
	TArray<FSpaceMMOShotTarget> Targets;
	TArray<USpaceMMOCombatComponent*> Combatants;

	for (TActorIterator<ASpaceMMOCharacterPawn> It(World); It; ++It)
	{
		ASpaceMMOCharacterPawn* Other = *It;

		USpaceMMOCombatComponent* Combat =
			Other != nullptr && Other != Pawn ? Other->FindComponentByClass<USpaceMMOCombatComponent>() : nullptr;

		if (Combat == nullptr || Combat->bDead || (bIdentifiedOnly && Combat->CharacterId == 0))
		{
			continue;
		}

		if (FVector::Distance(Other->GetSystemPosition().Kilometres, Muzzle) * 1000.0 > Range + 5.0)
		{
			continue;
		}

		FSpaceMMOShotTarget Target;
		Target.Index = Combatants.Num();
		Target.FeetKilometres = Other->GetSystemPosition().Kilometres;
		Target.Up = Other->GetSurfaceNormal();
		Target.Forward = Other->GetActorForwardVector();
		Target.RadiusMetres = Other->GetCollisionRadiusCentimetres() / 100.0;
		Target.HeightMetres = Other->GetBodyHeightCentimetres() / 100.0;

		Targets.Add(Target);
		Combatants.Add(Combat);
	}

	FSpaceMMOShotHit Hit;
	const bool bMetSomebody = SpaceMMO::Shot::Resolve(Muzzle, Direction, Range, Targets, Hit);

	const double Clear = ClearDistanceMetres(Muzzle, Direction, bMetSomebody ? Hit.DistanceMetres : Range);
	const bool bLanded = bMetSomebody && Hit.DistanceMetres <= Clear + 0.01;

	Out.MuzzleKilometres = Muzzle;
	Out.EndKilometres = Muzzle + Direction * (bLanded ? Hit.DistanceMetres : Clear) / 1000.0;
	Out.bLanded = bLanded;
	Out.bMetSomebody = bMetSomebody;
	Out.ClearMetres = Clear;
	Out.Hit = Hit;
	Out.Target = bLanded ? Combatants[Hit.Index] : nullptr;

	return true;
}

void USpaceMMOCombatComponent::Fire(const FVector& AimPointKilometres, const int32 Shot, const double ArrivedSeconds)
{
	FTracedShot Traced;

	if (!TraceShot(AimPointKilometres, true, Traced))
	{
		return;
	}

	MulticastBolt(Traced.MuzzleKilometres, Traced.EndKilometres, Traced.bLanded, Shot);

	if (!Traced.bLanded)
	{
		UE_LOG(LogSpaceMMOBackend, Log, TEXT("Combat: character %d fired shot %d; it stopped %.1f m out%s."),
			CharacterId, Shot, Traced.ClearMetres,
			Traced.bMetSomebody ? TEXT(", short of a target, against cover or ground") : TEXT(""));

		return;
	}

	const FSpaceMMOShotHit& Hit = Traced.Hit;
	USpaceMMOCombatComponent* TargetCombat = Traced.Target;
	USpaceMMOBackendClient* Client = Backend();

	if (Client == nullptr)
	{
		return;
	}

	UE_LOG(LogSpaceMMOBackend, Log, TEXT("Combat: character %d hit character %d with shot %d, %s from the %s, %.1f m."),
		CharacterId, TargetCombat->CharacterId, Shot,
		*StaticEnum<EBackendHitZone>()->GetNameStringByValue(static_cast<int64>(Hit.Zone)),
		*StaticEnum<EBackendHitFacing>()->GetNameStringByValue(static_cast<int64>(Hit.Facing)),
		Hit.DistanceMetres);

	TWeakObjectPtr<USpaceMMOCombatComponent> WeakThis(this);
	TWeakObjectPtr<USpaceMMOCombatComponent> WeakTarget(TargetCombat);

	const EBackendHitZone Zone = Hit.Zone;
	const FVector Point = Hit.PointKilometres;
	const double AskedSeconds = FPlatformTime::Seconds();

	// Away from the shot, if this one kills (task 201): decided now, while the target stands where it was hit.
	const ESpaceMMOFall Fall = FallFrom(
		Hit.Facing,
		(Traced.EndKilometres - Traced.MuzzleKilometres).GetSafeNormal(),
		TargetCombat->GetOwner() != nullptr ? TargetCombat->GetOwner()->GetActorRightVector() : FVector::RightVector);

	Client->HitAsServer(
		CharacterId,
		TargetCombat->CharacterId,
		Hit.Zone,
		Hit.Facing,
		Traced.MuzzleKilometres,
		[WeakThis, WeakTarget, Zone, Point, Shot, ArrivedSeconds, AskedSeconds, Fall](const FBackendHitResult& Result)
		{
			USpaceMMOCombatComponent* Self = WeakThis.Get();
			USpaceMMOCombatComponent* Target = WeakTarget.Get();

			if (Self == nullptr)
			{
				return;
			}

			// The API's answer as this game thread sees it, so including the wait for the next server frame; and
			// from the shot arriving, which adds the trace (task 198).
			const double Answered = FPlatformTime::Seconds();

			UE_LOG(LogSpaceMMOBackend, Log, TEXT("Combat: shot %d answered in %.0f ms, %.0f ms after it arrived."),
				Shot, (Answered - AskedSeconds) * 1000.0, (Answered - ArrivedSeconds) * 1000.0);

			if (!Result.bLanded)
			{
				UE_LOG(LogSpaceMMOBackend, Log, TEXT("Combat: refused, %s."), *Result.Refusal);

				Self->ClientShotRefused(Result.Refusal, Shot);

				return;
			}

			const double Now = Self->ServerNowSeconds();

			// The shooter is in a fight too: their own health stops coming back for ten seconds.
			Self->MarkInCombat(Now, Self->GetHealth());

			if (Target != nullptr)
			{
				Target->MaxHealth = Result.TargetMaxHealth;
				Target->MarkInCombat(Now, Result.TargetHealth);

				if (Result.bKilled)
				{
					Target->DeathFall = Fall;
					Target->DeathVariant = static_cast<uint8>(FMath::RandRange(0, 255));
					Target->bDead = true;
					Target->ShowDeathOnBody();

					if (ASpaceMMOCharacterPawn* Fallen = Cast<ASpaceMMOCharacterPawn>(Target->GetOwner()))
					{
						Fallen->SetMovementLocked(true);
					}

					Target->ClientKilled(Self->DisplayName);

					UE_LOG(LogSpaceMMOBackend, Log, TEXT("Combat: character %d killed character %d."),
						Self->CharacterId, Target->CharacterId);
				}
			}

			Self->ClientHitConfirmed(
				Result.Damage, Zone, Point, Result.bKilled, Target != nullptr ? Target->GetOwner() : nullptr, Shot);
		},
		[WeakThis](const FBackendFailure& Failure)
		{
			UE_LOG(LogSpaceMMOBackend, Warning, TEXT("Combat: the hit could not be reported (%d): %s"),
				Failure.HttpStatus, *Failure.Message);
		});
}

void USpaceMMOCombatComponent::MulticastBolt_Implementation(
	const FVector StartKilometres, const FVector EndKilometres, const bool bHitSomebody, const int32 Shot)
{
	UWorld* World = GetWorld();

	// Nothing to draw on a server that draws nothing.
	if (World == nullptr || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	// The shooter drew this one on the click (task 198). Its arrival is the round trip to the server and back,
	// which is worth the log line: it is the part of a shot's delay no other line measures.
	if (const APawn* Pawn = Cast<APawn>(GetOwner()); Pawn != nullptr && Pawn->IsLocallyControlled())
	{
		UE_LOG(LogSpaceMMOBackend, Log, TEXT("Combat: shot %d came back from the server %.0f ms after the click."),
			Shot, MillisecondsSinceSent(Shot));

		return;
	}

	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	if (ASpaceMMOBoltActor* Bolt = World->SpawnActor<ASpaceMMOBoltActor>(Spawn))
	{
		Bolt->Launch(FSystemCoordinate(StartKilometres), FSystemCoordinate(EndKilometres), bHitSomebody);
	}
}

void USpaceMMOCombatComponent::ClientHitConfirmed_Implementation(
	const int32 Damage, const EBackendHitZone Zone, const FVector PointKilometres, const bool bKilled, AActor* Target,
	const int32 Shot)
{
	const UWorld* World = GetWorld();
	const double Now = World != nullptr ? World->GetRealTimeSeconds() : 0.0;

	FSpaceMMODealtHit Dealt;
	Dealt.Damage = Damage;
	Dealt.Zone = Zone;
	Dealt.PointKilometres = PointKilometres;
	Dealt.bKilled = bKilled;
	Dealt.ArrivedSeconds = Now;
	Dealt.Target = Target;

	RecentHits.Add(Dealt);

	if (RecentHits.Num() > RecentHitLimit)
	{
		RecentHits.RemoveAt(0, RecentHits.Num() - RecentHitLimit);
	}

	LastHitSeconds = Now;
	bLastHitKilled = bKilled;

	UE_LOG(LogSpaceMMOBackend, Log, TEXT("Combat: shot %d hit for %d%s, %.0f ms after the click."),
		Shot, Damage, bKilled ? TEXT(", and killed") : TEXT(""), MillisecondsSinceSent(Shot));
}

void USpaceMMOCombatComponent::ClientShotRefused_Implementation(const FString& Refusal, const int32 Shot)
{
	const FString Message = RefusalMessage(Refusal);

	UE_LOG(LogSpaceMMOBackend, Log, TEXT("Combat: shot %d refused, %s, %.0f ms after the click."),
		Shot, *Refusal, MillisecondsSinceSent(Shot));

	if (ASpaceMMOPlayerController* Controller = OwningController(); Controller != nullptr && !Message.IsEmpty())
	{
		Controller->ShowTransientMessage(Message, ESpaceMMOMessageTone::Warning);
	}
}

void USpaceMMOCombatComponent::ClientKilled_Implementation(const FString& Killer)
{
	KillerName = Killer;

	UE_LOG(LogSpaceMMOBackend, Log, TEXT("Combat: killed by %s."), *Killer);
}

void USpaceMMOCombatComponent::OnRep_Dead()
{
	if (!bDead)
	{
		KillerName.Reset();
	}

	ShowDeathOnBody();
}

void USpaceMMOCombatComponent::ShowDeathOnBody()
{
	const UWorld* World = GetWorld();
	ASpaceMMOCharacterPawn* Pawn = Cast<ASpaceMMOCharacterPawn>(GetOwner());

	if (Pawn == nullptr || World == nullptr || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	if (!bDead)
	{
		Pawn->EndDeath();

		return;
	}

	// From when the fight ended: the fall, for anyone watching it happen; the body already down, for anyone
	// arriving later or signing in to find themselves dead.
	const double Since = LastCombatServerSeconds >= 0.0 ? ServerNowSeconds() - LastCombatServerSeconds : 3600.0;

	Pawn->ShowDeath(DeathFall, DeathVariant, Since);
}

ESpaceMMOFall USpaceMMOCombatComponent::FallFrom(
	const EBackendHitFacing Facing, const FVector& ShotDirection, const FVector& TargetRight)
{
	switch (Facing)
	{
	case EBackendHitFacing::Front:
		return ESpaceMMOFall::Backward;

	case EBackendHitFacing::Back:
		return ESpaceMMOFall::Forward;

	default:
		// Travelling toward their right is coming from their left, and pushes them right.
		return FVector::DotProduct(ShotDirection, TargetRight) > 0.0 ? ESpaceMMOFall::ToRight : ESpaceMMOFall::ToLeft;
	}
}

void USpaceMMOCombatComponent::OnRep_StoredHealth(const int32 Before)
{
	// Only on the character's own machine: the server's "hit ... with shot N" line, read against this one's time,
	// is how long the target waited to see it (task 198). Every change, not only falls: the stored value is health
	// as of the last fight, so a hit after regeneration can store more than before.
	const APawn* Pawn = Cast<APawn>(GetOwner());

	if (Pawn != nullptr && Pawn->IsLocallyControlled())
	{
		UE_LOG(LogSpaceMMOBackend, Log, TEXT("Combat: health %d of %d as of the last fight (was %d)."),
			StoredHealth, MaxHealth, Before);
	}
}

#if !UE_BUILD_SHIPPING
void USpaceMMOCombatComponent::ShowSampleForLook(const bool bAsDead)
{
	const ASpaceMMOCharacterPawn* Pawn = Cast<ASpaceMMOCharacterPawn>(GetOwner());

	if (Pawn == nullptr)
	{
		return;
	}

	bLookSample = true;

	DisplayName = TEXT("Ayla1204");
	MaxHealth = 2000;
	StoredHealth = bAsDead ? 0 : 640;
	LastCombatServerSeconds = ServerNowSeconds();
	bDead = bAsDead;
	KillerName = bAsDead ? TEXT("Orenthal") : FString();
	WeaponName = TEXT("Laser Rifle");

	// Over this pawn's own head, which is in view in third person: the bar and the number need somebody to sit on.
	const FVector Up = Pawn->GetSurfaceNormal().GetSafeNormal();

	FSpaceMMODealtHit Sample;
	Sample.Damage = 99;
	Sample.Zone = EBackendHitZone::Head;
	Sample.PointKilometres = Pawn->GetSystemPosition().Kilometres + Up * 0.0017;
	Sample.Target = GetOwner();

	RecentHits.Reset();
	RecentHits.Add(Sample);
	bLastHitKilled = false;
}
#endif

// ------------------------------------------------------------------------------------------------
// Coming back

void USpaceMMOCombatComponent::RequestRespawn()
{
	if (bDead)
	{
		ServerRespawn();
	}
}

void USpaceMMOCombatComponent::ServerRespawn_Implementation()
{
	USpaceMMOBackendClient* Client = Backend();

	if (!bDead || bRespawnPending || Client == nullptr || CharacterId == 0)
	{
		return;
	}

	bRespawnPending = true;

	TWeakObjectPtr<USpaceMMOCombatComponent> WeakThis(this);

	Client->RespawnAsServer(
		CharacterId,
		[WeakThis](const FBackendRespawn& Respawn)
		{
			USpaceMMOCombatComponent* Self = WeakThis.Get();

			if (Self == nullptr)
			{
				return;
			}

			Self->bRespawnPending = false;

			ASpaceMMOCharacterPawn* Pawn = Cast<ASpaceMMOCharacterPawn>(Self->GetOwner());
			UWorld* World = Self->GetWorld();

			FString StationName = Respawn.StationKey;

			for (TActorIterator<ASpaceMMOStationActor> It(World); It; ++It)
			{
				if ((*It)->GetStation().Id != Respawn.StationId)
				{
					continue;
				}

				StationName = (*It)->GetStation().Name;

				FSystemCoordinate Ground;

				// Beside it on the ground, as a summoned ship is put down (task 153): twenty metres out, a metre up,
				// and let the ground take them the rest of the way.
				if (Pawn != nullptr && (*It)->GroundPositionBeside(0.02, 0.001, Ground))
				{
					Pawn->ResumeAt(Ground);
				}

				break;
			}

			Self->MaxHealth = Respawn.MaxHealth;
			Self->StoredHealth = Respawn.Health;
			Self->LastCombatServerSeconds = -1.0;
			Self->bDead = false;
			Self->ShowDeathOnBody();

			if (Pawn != nullptr)
			{
				Pawn->SetMovementLocked(false);
			}

			Self->ClientRespawned(StationName);

			UE_LOG(LogSpaceMMOBackend, Log, TEXT("Combat: character %d came back at %s."), Self->CharacterId, *StationName);
		},
		[WeakThis](const FBackendFailure& Failure)
		{
			if (USpaceMMOCombatComponent* Self = WeakThis.Get())
			{
				Self->bRespawnPending = false;
			}

			UE_LOG(LogSpaceMMOBackend, Warning, TEXT("Combat: respawn refused (%d): %s"), Failure.HttpStatus, *Failure.Message);
		});
}

void USpaceMMOCombatComponent::ClientRespawned_Implementation(const FString& StationName)
{
	KillerName.Reset();
	RecentHits.Reset();

	UE_LOG(LogSpaceMMOBackend, Log, TEXT("Combat: back at %s."), *StationName);
}
