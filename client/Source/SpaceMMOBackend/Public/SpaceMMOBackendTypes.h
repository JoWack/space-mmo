#pragma once

#include "CoreMinimal.h"
#include "SpaceMMOBackendTypes.generated.h"

/**
 * A logged-in session.
 *
 * The token is a bearer credential: anything holding it can act as this account until it expires.
 * It lives in memory only and is deliberately never written to a save game or a config file.
 */
USTRUCT(BlueprintType)
struct SPACEMMOBACKEND_API FBackendSession
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	int32 AccountId = 0;

	/** Not exposed to Blueprint. A bearer token in a Blueprint graph is a token in a log line. */
	FString Token;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	FDateTime ExpiresAt;

	bool IsValid() const { return AccountId > 0 && !Token.IsEmpty(); }
};

/**
 * Races, mirroring the server's enum.
 *
 * <strong>The numeric values are the contract.</strong> They are persisted server-side and sent
 * across the wire as integers, so reordering these silently turns every Space Orc into a Martian.
 */
UENUM(BlueprintType)
enum class EBackendRace : uint8
{
	Humanoid = 0,
	Martian = 1,
	SpaceElf = 2,
	SpaceOrc = 3,
};

/** The two factions. TODO(name), matching the server. */
UENUM(BlueprintType)
enum class EBackendFaction : uint8
{
	A = 0,
	B = 1,
};

/**
 * A character as the server describes it.
 *
 * A read-only mirror. The client renders this and never edits it — every field here is a
 * consequence of something the server decided.
 */
USTRUCT(BlueprintType)
struct SPACEMMOBACKEND_API FBackendCharacter
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	int32 Id = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	EBackendRace Race = EBackendRace::Humanoid;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	EBackendFaction Faction = EBackendFaction::A;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	int32 HomeBodyId = 0;

	/**
	 * Balance in minor units. 100 minor units is one credit.
	 *
	 * int64, never a float. A double loses whole credits somewhere past nine quadrillion minor
	 * units, and an economy that runs for years is exactly the thing that gets there — so the
	 * client mirrors the server's representation rather than converting on receipt.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	int64 BalanceMinorUnits = 0;

	/** Formatted for display, e.g. 1234567 minor units as "12,345.67". */
	FString FormatBalance() const;
};

/** Skill categories, mirroring the server. */
UENUM(BlueprintType)
enum class EBackendSkillCategory : uint8
{
	Life = 0,
	Combat = 1,
	Pilot = 2,
};

/**
 * One skill's progress.
 *
 * Level arrives from the server rather than being derived here. The XP curve is game rules, and
 * a second implementation of it on the client is a second implementation that can disagree.
 */
USTRUCT(BlueprintType)
struct SPACEMMOBACKEND_API FBackendSkill
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	FString Key;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	EBackendSkillCategory Category = EBackendSkillCategory::Life;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	int64 Xp = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	int32 Level = 1;
};

/** One stack in a character's inventory. */
USTRUCT(BlueprintType)
struct SPACEMMOBACKEND_API FBackendInventoryItem
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	int32 ItemDefId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	FString ItemKey;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	int32 Quantity = 0;

	/**
	 * Minor units a faction standing order pays per unit, or zero if none buys it.
	 *
	 * Zero rather than an optional, because "nobody buys this" and "this is worth nothing" are the
	 * same instruction to a client deciding whether to offer the sale — and content forbids a zero
	 * price on anything a faction does buy, so the two cannot be confused.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	int64 FactionBuyPriceMinorUnits = 0;
};

/** One material a recipe consumes, per run. */
USTRUCT(BlueprintType)
struct SPACEMMOBACKEND_API FBackendRecipeInput
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	int32 ItemDefId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	FString ItemKey;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	int32 Quantity = 0;
};

/**
 * A recipe as the server describes it.
 *
 * <strong>Carries keys as well as ids.</strong> Ids are assigned by the database and differ between
 * any two seeded environments, so a client that remembered one would break the day the database was
 * rebuilt in a different order — and it would break by quietly building something else rather than
 * by failing. Same reasoning as looking bodies up by content key.
 */
USTRUCT(BlueprintType)
struct SPACEMMOBACKEND_API FBackendRecipe
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	int32 Id = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	FString Key;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	FString OutputItemKey;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	FString OutputName;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	int32 OutputQuantity = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	FString SkillKey;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	FString SkillName;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	int32 RequiredLevel = 1;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	int32 JobSeconds = 0;

	/** Empty when the recipe needs no tool. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	FString RequiredToolName;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	TArray<FBackendRecipeInput> Inputs;
};

/**
 * A job in progress.
 *
 * <strong>Whether it is finished is the server's answer, not a subtraction done here.</strong>
 * <c>bIsClaimable</c> and <c>SecondsRemaining</c> both arrive computed against the server clock. A
 * client that worked them out locally would disagree the moment the two clocks drifted, and it is
 * the server that decides — so the disagreement would show up as a claim button that does nothing.
 */
USTRUCT(BlueprintType)
struct SPACEMMOBACKEND_API FBackendIndustryJob
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	int64 Id = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	FString RecipeKey;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	FString OutputName;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	int32 OutputQuantityTotal = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	int32 Runs = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	bool bIsClaimable = false;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	int32 SecondsRemaining = 0;
};

/**
 * Where a quest has got to, mirroring the server.
 *
 * <strong>The numeric values are the contract</strong>, as with races: they are persisted and sent
 * as integers, so reordering these silently turns finished quests into abandoned ones.
 */
UENUM(BlueprintType)
enum class EBackendQuestState : uint8
{
	InProgress = 0,
	Completed = 1,
	Abandoned = 2,

	/** Every objective met, reward not yet collected. */
	ReadyToTurnIn = 3,
};

/** What a quest step asks for, mirroring the server. */
UENUM(BlueprintType)
enum class EBackendObjective : uint8
{
	Gather = 0,
	Craft = 1,
	Refine = 2,
	Travel = 3,
	Dock = 4,
	Talk = 5,
};

/**
 * One quest in the journal, including what it currently wants.
 *
 * The step fields are empty on a quest with no active step — finished, or waiting to be handed in.
 * A journal that carried only a step number would tell a player which numbered step they were on
 * and nothing about what it asked of them.
 */
USTRUCT(BlueprintType)
struct SPACEMMOBACKEND_API FBackendJournalEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Quests")
	FString QuestKey;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Quests")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Quests")
	EBackendQuestState State = EBackendQuestState::InProgress;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Quests")
	FString StepDescription;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Quests")
	EBackendObjective StepObjective = EBackendObjective::Gather;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Quests")
	FString StepTargetKey;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Quests")
	int32 StepProgress = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Quests")
	int32 StepRequired = 0;
};

/** A quest the character could accept now. */
USTRUCT(BlueprintType)
struct SPACEMMOBACKEND_API FBackendAvailableQuest
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Quests")
	FString QuestKey;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Quests")
	FString Name;
};

/** Who the backend says a connecting player is entitled to be. */
USTRUCT(BlueprintType)
struct SPACEMMOBACKEND_API FBackendResolvedCharacter
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	int32 AccountId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	int32 CharacterId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	FString CharacterName;
};

/** A planet or moon, as the server describes it. */
USTRUCT(BlueprintType)
struct SPACEMMOBACKEND_API FBackendBody
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	int32 Id = 0;

	/** Stable content key, e.g. <c>body_capital</c>. What the client looks a body up by. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	FString Key;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	int32 StarSystemId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	double RadiusKilometres = 0.0;
};

/**
 * A deposit on a body, as the server describes it.
 *
 * <strong>A direction, never a position.</strong> The server says which way the deposit lies from
 * the body's centre and stops there, because how far out the ground is at that direction is a
 * question <c>FPlanetTerrain::SurfacePosition</c> already answers identically on both machines. A
 * transmitted position would be a second answer, free to disagree the moment terrain configuration
 * changed — and the deposit would end up buried or floating with nothing in the payload looking
 * wrong.
 */
USTRUCT(BlueprintType)
struct SPACEMMOBACKEND_API FBackendResourceNode
{
	GENERATED_BODY()

	/** Server-side id. What a gather request names. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	int64 Id = 0;

	/** Stable content key, e.g. <c>node_capital_ferrite_a</c>. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	FString Key;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	int32 BodyId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	FString ItemKey;

	/** What to call it in the world, e.g. "Ferrite Ore". */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	FString ItemName;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	FString SkillKey;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	int32 RequiredLevel = 1;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	int32 QuantityMax = 0;

	/** Unit vector from the body's centre. Normalised server-side on content load. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	FVector Direction = FVector::ZeroVector;
};

/**
 * What one gathering attempt actually yielded.
 *
 * A zero quantity is a success, not a failure: it means too little time has passed since this
 * character last gathered, or the deposit is spent. Both are ordinary states to render — "nothing
 * yet" rather than "something went wrong" — which is why they arrive as a 200 with an empty result
 * instead of an error.
 */
USTRUCT(BlueprintType)
struct SPACEMMOBACKEND_API FBackendGatherResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	int32 ItemDefId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	int32 Quantity = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	int64 XpAwarded = 0;

	/** What the deposit still holds. Zero means this attempt exhausted it. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	int32 NodeRemaining = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	bool bDepleted = false;
};

/**
 * Why a request failed.
 *
 * Distinguished because the client should react differently to each: a transport failure is worth
 * retrying, an authentication failure means logging in again, and a rejection means showing the
 * player what the server said.
 */
UENUM(BlueprintType)
enum class EBackendError : uint8
{
	/** No error. */
	None = 0,

	/** Never reached the server — no connection, DNS failure, timeout. */
	Transport = 1,

	/** 401. The token is missing, expired, or forged. */
	Unauthenticated = 2,

	/** 404. Either genuinely absent, or somebody else's and deliberately indistinguishable. */
	NotFound = 3,

	/** 4xx the server explained: validation failure, conflict, a rule the request broke. */
	Rejected = 4,

	/** 5xx, or a body that did not parse. The server is wrong, not the caller. */
	Server = 5,
};

/** A failed request, with whatever the server was willing to say about it. */
USTRUCT(BlueprintType)
struct SPACEMMOBACKEND_API FBackendFailure
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	EBackendError Error = EBackendError::None;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	int32 HttpStatus = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	FString Message;
};
