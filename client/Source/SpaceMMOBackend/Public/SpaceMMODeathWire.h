#pragma once

#include "CoreMinimal.h"
#include "SpaceMMOBackendTypes.h"

#include "SpaceMMODeathWire.generated.h"

/** One material in a dropped crate (task 103). */
USTRUCT(BlueprintType)
struct SPACEMMOBACKEND_API FBackendLootItem
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	int32 ItemDefId = 0;

	/** The stack's own style: what taking it names. Common for a raw material, whose world is the item's. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	EBackendItemStyle Style = EBackendItemStyle::Common;

	/** The style the goods show, as the inventory lists them: a raw material's own world. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	EBackendItemStyle ShownStyle = EBackendItemStyle::Common;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	FString ItemKey;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	int32 Quantity = 0;

	/** Each unit's volume, so the window can say what will not fit before anyone asks the server. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	double VolumeM3 = 0.0;
};

/** A dropped crate: the dropped half of a death's materials, where the body fell (ADR-0016, task 103). */
USTRUCT(BlueprintType)
struct SPACEMMOBACKEND_API FBackendLootContainer
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	int64 Id = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	int32 DroppedByCharacterId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	FString DroppedBy;

	/** Where it lies, in system kilometres, as the game server placed it. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	FVector PositionKilometres = FVector::ZeroVector;

	/** How long until it is gone, as of the answer: counted down on the game server's own clock. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	double SecondsLeft = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	TArray<FBackendLootItem> Items;
};

/** One material a death took: how many dropped, how many were destroyed. */
USTRUCT(BlueprintType)
struct SPACEMMOBACKEND_API FBackendDeathLoss
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	int32 ItemDefId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	FString ItemKey;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	FString Name;

	/** The style the goods show. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	EBackendItemStyle Style = EBackendItemStyle::Common;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	int32 Dropped = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	int32 Destroyed = 0;
};

/** One worn item a death wore down. */
USTRUCT(BlueprintType)
struct SPACEMMOBACKEND_API FBackendWornLoss
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	int64 ItemInstanceId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	FString ItemKey;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	int32 Before = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	int32 After = 0;
};

/**
 * What a death cost (task 103): what the death panel lists, and the crate for the game server to put in the world.
 */
USTRUCT(BlueprintType)
struct SPACEMMOBACKEND_API FBackendDeathCosts
{
	GENERATED_BODY()

	/** False when nothing was owed: the death was already costed. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	bool bApplied = false;

	/** Died docked at a station, which costs nothing (ADR-0016). */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	bool bAtStation = false;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	TArray<FBackendDeathLoss> Lost;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	TArray<FBackendWornLoss> Worn;

	/** Whether anything dropped, and so whether Container means anything. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	bool bHasContainer = false;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	FBackendLootContainer Container;
};

/** What one go at a crate took, or why it took nothing. */
USTRUCT(BlueprintType)
struct SPACEMMOBACKEND_API FBackendLootResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	bool bTook = false;

	/** <c>gone</c>, <c>dead</c>, <c>no_room</c> or <c>not_there</c>; empty when something was taken. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	FString Refusal;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	TArray<FBackendLootItem> Taken;

	/** Whether anything is left in it; false once it is empty or gone. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	bool bHasLeft = false;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	FBackendLootContainer Left;
};

/** The wire for what dying costs and looting (task 103): the API's <c>DeathEndpoints</c>, both ways. */
struct SPACEMMOBACKEND_API FSpaceMMODeathWire
{
	static bool ParseDeathCosts(const FString& Json, FBackendDeathCosts& Out);

	static bool ParseContainers(const FString& Json, TArray<FBackendLootContainer>& Out);

	static bool ParseLootResult(const FString& Json, FBackendLootResult& Out);

	/** Where a dead character's body fell, in system kilometres, written as whereabouts writes positions. */
	static FString MakeDeathBody(int32 CharacterId, const FVector& PositionKilometres);

	/**
	 * Who is taking and which stacks: each named by its item and the stack's own style, as numbers. None named
	 * takes everything that fits.
	 */
	static FString MakeLootBody(int32 CharacterId, const TArray<FBackendLootItem>& Wanted);
};
