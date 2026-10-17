#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "SpaceMMOBackendTypes.h"

#include "SpaceMMOCombatComponent.generated.h"

/** One of your hits, as your screen shows it: a number where it landed, for a moment (task 194). */
USTRUCT(BlueprintType)
struct SPACEMMOBACKEND_API FSpaceMMODealtHit
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Combat")
	int32 Damage = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Combat")
	EBackendHitZone Zone = EBackendHitZone::Body;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Combat")
	FVector PointKilometres = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Combat")
	bool bKilled = false;

	/** This machine's clock when it arrived, for fading it out. */
	double ArrivedSeconds = 0.0;

	/** Whom it hit, for their bar over their head. */
	TWeakObjectPtr<AActor> Target;
};

/**
 * Fighting on foot (task 194): health, the fire key, the shot, and dying and coming back.
 *
 * <para>
 * <strong>Who decides what.</strong> The owning client says where it is aiming, nothing more. The dedicated server
 * fires: it knows where everyone stands, finds what the shot meets (<c>SpaceMMO::Shot</c>, then buildings and the
 * ground), and draws the bolt for everyone. The API decides what a hit did -- whether it was allowed, its damage,
 * the target's health and the shooter's XP -- because persistence lives there (ADR-0003). The server then
 * replicates the answer.
 * </para>
 * <para>
 * <strong>Health is replicated as of the last fight</strong>, with the time of it, and worked out on every machine
 * from the clock by the rule the API uses (<see cref="HealthAt"/>, ADR-0017's 10 s and 5% a second). So the bar
 * fills while you wait without anything being sent.
 * </para>
 * <para>
 * Attached on the server to every character, as gathering is, and replicated from there.
 * </para>
 */
UCLASS(ClassGroup = (SpaceMMO), meta = (BlueprintSpawnableComponent))
class SPACEMMOBACKEND_API USpaceMMOCombatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USpaceMMOCombatComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** How long after the last damage dealt or taken health starts coming back: the API's rule (ADR-0017). */
	static constexpr double RegenDelaySeconds = 10.0;

	/** And how fast, in percent of the maximum a second. */
	static constexpr int32 RegenPercentPerSecond = 5;

	/**
	 * Health at a moment, from what it was at the last fight: the API's <c>CombatHealth.Current</c>, mirrored.
	 *
	 * @param LastCombatSeconds Server time of the last damage dealt or taken; negative for never.
	 */
	static int32 HealthAt(int32 Stored, int32 Max, double LastCombatSeconds, double NowSeconds);

	/** Health now, on whichever machine asks. */
	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Combat")
	int32 GetHealth() const;

	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Combat")
	int32 GetMaxHealth() const { return MaxHealth; }

	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Combat")
	bool IsDead() const { return bDead; }

	/** The character's name, for the bar over their head and for "killed by". */
	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Combat")
	const FString& GetDisplayName() const { return DisplayName; }

	/** Whether health is known yet. Before the server has asked the API, there is nothing to draw. */
	bool HasHealth() const { return MaxHealth > 0; }

	/** Server only: who this pawn is, which the controller learns from the API, and what to call them. */
	void SetCharacter(int32 InCharacterId, const FString& InDisplayName);

	int32 GetCharacterId() const { return CharacterId; }

	/** Fires, if the owning player may. Bound to the Fire action; also SpaceMMO.Fire in the console. */
	void RequestFire();

	/** Asks to come back, from the death panel's button. */
	void RequestRespawn();

	/** Asks the server to read Weapon 1 again: after equipping, which happens between client and API. */
	void RequestLoadoutRefresh();

	/** Binds the Fire key to whatever input component the pawn has now, as gathering's does. */
	void BindInput(class UInputComponent* InputComponent);

	/** Your recent hits, newest last, for damage numbers and bars over heads. Owning client only. */
	const TArray<FSpaceMMODealtHit>& GetRecentHits() const { return RecentHits; }

	/** When the last hit landed on this machine's clock, for the crosshair's flash; negative for never. */
	double GetLastHitSeconds() const { return LastHitSeconds; }

	/** Whether that hit killed. */
	bool LastHitKilled() const { return bLastHitKilled; }

	/** Who killed this character, for the death panel. */
	const FString& GetKillerName() const { return KillerName; }

	/** What the weapon in hand is called, or empty for nothing that fires. Owning client. */
	const FString& GetWeaponName() const { return WeaponName; }

#if !UE_BUILD_SHIPPING
	/**
	 * SpaceMMOLookPanels Combat and Dead: sample numbers to look at the HUD with, no server needed -- health at a
	 * third, a head shot just landed over this pawn's own head, and for Dead, killed by somebody named. Held fresh
	 * every tick, so a capture seconds later still shows the flash and the number.
	 */
	void ShowSampleForLook(bool bAsDead);
#endif

protected:
	virtual void BeginPlay() override;

	virtual void TickComponent(
		float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(Server, Reliable)
	void ServerFire(FVector AimPointKilometres);

	UFUNCTION(Server, Reliable)
	void ServerRespawn();

	UFUNCTION(Server, Reliable)
	void ServerRefreshLoadout();

	/** Draws a bolt everywhere: from the muzzle to wherever it stopped. */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastBolt(FVector StartKilometres, FVector EndKilometres, bool bHitSomebody);

	UFUNCTION(Client, Reliable)
	void ClientHitConfirmed(int32 Damage, EBackendHitZone Zone, FVector PointKilometres, bool bKilled, AActor* Target);

	UFUNCTION(Client, Reliable)
	void ClientShotRefused(const FString& Refusal);

	UFUNCTION(Client, Reliable)
	void ClientKilled(const FString& Killer);

	UFUNCTION(Client, Reliable)
	void ClientRespawned(const FString& StationName);

	UFUNCTION()
	void HandlePawnRestarted(APawn* Pawn);

	UFUNCTION()
	void OnRep_Dead();

	/** Health as of LastCombatServerSeconds. */
	UPROPERTY(Replicated)
	int32 StoredHealth = 0;

	UPROPERTY(Replicated)
	int32 MaxHealth = 0;

	/** Server time of the last damage dealt or taken; negative for never. */
	UPROPERTY(Replicated)
	double LastCombatServerSeconds = -1.0;

	UPROPERTY(ReplicatedUsing = OnRep_Dead)
	bool bDead = false;

	UPROPERTY(Replicated)
	FString DisplayName;

	/** What Weapon 1 holds, for the owning client's cooldown and range. */
	UPROPERTY(Replicated)
	FString WeaponName;

	UPROPERTY(Replicated)
	int32 WeaponCooldownMs = 0;

	UPROPERTY(Replicated)
	int32 WeaponRangeMetres = 0;

private:
	/** Server: reads health and Weapon 1 from the API and replicates them. */
	void FetchStatus();

	/** Server: the shot, from the owner's chest towards where they aimed. */
	void Fire(const FVector& AimPointKilometres);

	/** Server: how far a shot gets before a building or the ground stops it, in metres. */
	double ClearDistanceMetres(const FVector& FromKilometres, const FVector& Direction, double WantedMetres) const;

	/** Server: health now becomes stored, and the clock restarts: dealing or taking damage. */
	void MarkInCombat(double ServerNow, int32 HealthNow);

	/** Client: where the crosshair points, in system kilometres, out to the weapon's range past the muzzle. */
	bool AimPoint(FVector& OutKilometres) const;

	double ServerNowSeconds() const;

	class ASpaceMMOPlayerController* OwningController() const;

	class USpaceMMOBackendClient* Backend() const;

	int32 CharacterId = 0;

	double LastShotServerSeconds = -1.0;

	double LastRequestSeconds = -1.0;

	TWeakObjectPtr<class UInputComponent> BoundInput;

	TArray<FSpaceMMODealtHit> RecentHits;

	double LastHitSeconds = -1.0;

	bool bLastHitKilled = false;

	FString KillerName;

	/** So a respawn asked for twice while the API answers is asked once. */
	bool bRespawnPending = false;

	/** The instance this client last saw in Weapon 1, so a change -- however made -- is sent to the server once. */
	int64 SeenWeaponInstanceId = -1;

	/** Whether the look sample is being held, see ShowSampleForLook. */
	bool bLookSample = false;
};
