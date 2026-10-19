// What dying costs, and looting what was dropped (task 103): the backend client's calls for them, in a file of their
// own beside SpaceMMOBackendClient.cpp so that one does not grow by another feature.

#include "SpaceMMOBackendClient.h"
#include "SpaceMMOBackendLog.h"
#include "SpaceMMODeathWire.h"

void USpaceMMOBackendClient::CostDeathAsServer(
	const int32 CharacterId,
	const FVector& CratePositionKilometres,
	TFunction<void(const FBackendDeathCosts&)> OnCosts,
	TFunction<void(const FBackendFailure&)> OnFailure)
{
	if (ServiceSecret.IsEmpty() || CharacterId == 0 || CratePositionKilometres.ContainsNaN())
	{
		UE_LOG(LogSpaceMMOBackend, Warning, TEXT("Death of character %d not costed: %s."), CharacterId,
			ServiceSecret.IsEmpty() ? TEXT("no service credential") : TEXT("no character or no position"));

		return;
	}

	Send(
		TEXT("POST"),
		TEXT("/combat/deaths"),
		FSpaceMMODeathWire::MakeDeathBody(CharacterId, CratePositionKilometres),
		false,
		[OnCosts = MoveTemp(OnCosts)](const FString& ResponseBody)
		{
			FBackendDeathCosts Costs;

			if (FSpaceMMODeathWire::ParseDeathCosts(ResponseBody, Costs) && OnCosts)
			{
				OnCosts(Costs);
			}
		},
		ServiceSecret,
		MoveTemp(OnFailure));
}

void USpaceMMOBackendClient::FetchLootContainersAsServer(
	TFunction<void(const TArray<FBackendLootContainer>&)> OnContainers,
	TFunction<void(const FBackendFailure&)> OnFailure)
{
	if (ServiceSecret.IsEmpty())
	{
		return;
	}

	Send(
		TEXT("GET"),
		TEXT("/combat/containers"),
		FString(),
		false,
		[OnContainers = MoveTemp(OnContainers)](const FString& ResponseBody)
		{
			TArray<FBackendLootContainer> Containers;

			if (FSpaceMMODeathWire::ParseContainers(ResponseBody, Containers) && OnContainers)
			{
				OnContainers(Containers);
			}
		},
		ServiceSecret,
		MoveTemp(OnFailure));
}

void USpaceMMOBackendClient::LootAsServer(
	const int64 ContainerId,
	const int32 CharacterId,
	const TArray<FBackendLootItem>& Wanted,
	TFunction<void(const FBackendLootResult&)> OnResult,
	TFunction<void(const FBackendFailure&)> OnFailure)
{
	if (ServiceSecret.IsEmpty() || ContainerId == 0 || CharacterId == 0)
	{
		return;
	}

	Send(
		TEXT("POST"),
		FString::Printf(TEXT("/combat/containers/%lld/loot"), ContainerId),
		FSpaceMMODeathWire::MakeLootBody(CharacterId, Wanted),
		false,
		[OnResult = MoveTemp(OnResult)](const FString& ResponseBody)
		{
			FBackendLootResult Result;

			if (FSpaceMMODeathWire::ParseLootResult(ResponseBody, Result) && OnResult)
			{
				OnResult(Result);
			}
		},
		ServiceSecret,
		MoveTemp(OnFailure));
}
