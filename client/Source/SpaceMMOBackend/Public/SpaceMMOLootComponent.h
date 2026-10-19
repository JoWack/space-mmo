#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "SpaceMMODeathWire.h"

#include "SpaceMMOLootComponent.generated.h"

/**
 * Taking from dropped crates, and being told what a death cost (task 103), on every character.
 *
 * <para>
 * <strong>The server decides who is in reach</strong>, as gathering's does: the client names a crate and the stacks it
 * wants, and the server takes them only if the crate is within <see cref="RangeMetres"/> of where it has the
 * character, then asks the API, which knows only who and what.
 * </para>
 * <para>
 * <strong>E is shared with gathering</strong> (Joe, 19 October): the nearer of a crate and a deposit in reach is what
 * E works (<see cref="CrateTakesE"/>). On a crate it opens the loot window beside the inventory.
 * </para>
 * <para>
 * Attached by <c>USpaceMMOLootSubsystem</c> on the server and replicated, as gathering is.
 * </para>
 */
UCLASS(ClassGroup = (SpaceMMO))
class SPACEMMOBACKEND_API USpaceMMOLootComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USpaceMMOLootComponent();

	/** How close a crate must be to open or take from, in metres. Generous, as gathering's is, for a 0.6 m crate. */
	static constexpr double RangeMetres = 5.0;

	/**
	 * Whether E opens a crate rather than gathering: a crate in reach, and nearer than any deposit in reach. Pure,
	 * for the tests; the deposit prompt asks the same question, so the two never both claim the key.
	 */
	static bool CrateTakesE(bool bCrate, double CrateMetres, bool bDeposit, double DepositMetres);

	/** The crate in reach, on any machine, or null. */
	class ASpaceMMOLootCrateActor* FindCrateInRange(double* OutMetres = nullptr) const;

	/** Whether E means this crate now, given the deposit gathering would work; null when it does not. */
	class ASpaceMMOLootCrateActor* CrateForE() const;

	/** Owning client: takes these stacks from the crate, or everything that fits when none are named. */
	void RequestLoot(class ASpaceMMOLootCrateActor* Crate, const TArray<FBackendLootItem>& Wanted);

	/** Server: tells this character's player what their death cost. */
	void ReportDeathCosts(const FBackendDeathCosts& Costs);

	/** What the last death cost, on the owning client, while they are dead; null before the answer arrives. */
	const FBackendDeathCosts* GetDeathReport() const { return bHasDeathReport ? &DeathReport : nullptr; }

	/**
	 * The line a looter reads afterwards: "Took 3 Ferrite Ore, 3 Structural Plate", or why nothing was taken. Pure,
	 * for the tests.
	 */
	static FString LootMessage(const TArray<FBackendLootItem>& Taken, const FString& Refusal);

#if !UE_BUILD_SHIPPING
	/** SpaceMMOLookPanels Dead: a sample of what a death cost, for the panel. */
	void ShowSampleDeathReportForLook();
#endif

protected:
	virtual void TickComponent(
		float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(Server, Reliable)
	void ServerLoot(class ASpaceMMOLootCrateActor* Crate, const TArray<FBackendLootItem>& Wanted);

	UFUNCTION(Client, Reliable)
	void ClientLootResult(const TArray<FBackendLootItem>& Taken, const FString& Refusal);

	UFUNCTION(Client, Reliable)
	void ClientDeathCosts(const FBackendDeathCosts& Costs);

private:
	class ASpaceMMOPlayerController* OwningController() const;

	FBackendDeathCosts DeathReport;

	bool bHasDeathReport = false;

	/** So a hammered button is one request at a time, as gathering's key is. */
	bool bLootPending = false;

	bool bLookSample = false;
};
