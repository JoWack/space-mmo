#pragma once

#include "CoreMinimal.h"
#include "SpaceMMOCoordinates.h"
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

	/**
	 * The hull this character flies, or zero for somebody on foot with no ship (ADR-0012).
	 *
	 * Zero rather than an optional, matching how every other absent id travels here: the server
	 * sends null and there is no id zero, so the two agree without a second field to say whether
	 * the first means anything.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	int64 ActiveShipItemInstanceId = 0;
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
 * Where a panel sits when it is sharing the viewport.
 *
 * The station overlay and the inventory screen take a side each when both are open, and the middle
 * when either is alone. Here rather than beside the widgets for the same reason the message tone is:
 * the player controller's public header names it, and that header must not pull UMG in.
 */
UENUM(BlueprintType)
enum class ESpaceMMOPanelSide : uint8
{
	Centre,
	Left,
	Right,
};

/**
 * How a transient message should feel, without saying what colour it is.
 *
 * The tone is a fact about the message — a yield went well, a refusal did not — and the colour it
 * becomes is a decision for the Widget Blueprint. Naming it this way keeps that split, and lets
 * combat add a third tone later without touching anything that renders one.
 *
 * Here rather than beside the widget that draws it, because the player controller's public header
 * names it and that header must not drag UMG in: UMG is a private dependency of this module, and a
 * public header including it would break any module that later includes this one.
 */
UENUM(BlueprintType)
enum class ESpaceMMOMessageTone : uint8
{
	/** Something was gained. */
	Positive,

	/** Nothing was gained, and the player is being told why. */
	Warning,
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

	/** XP still needed for the next level, or 0 at the cap. Served, for the same reason Level is. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	int64 XpToNextLevel = 0;

	/**
	 * How far through the current level, 0 to 1.
	 *
	 * Negative when the server did not send it, which is not the same as zero: zero is a level just
	 * begun and draws an empty bar, while "not sent" must draw no bar at all. An older server
	 * omitting the field would otherwise have every skill on screen claiming no progress.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	float ProgressToNextLevel = -1.0f;

	/** Whether the server sent progress figures at all. */
	bool HasProgress() const { return ProgressToNextLevel >= 0.0f; }
};

/**
 * Where a stack lives, mirroring InventoryKind on the server.
 *
 * The numbers are the wire contract and a server-side test pins them, because nothing here can
 * tell a wrong value from a right one — a mismatch does not fail, it just quietly matches the
 * wrong stacks.
 */
UENUM(BlueprintType)
enum class EBackendInventoryKind : uint8
{
	/** On the character's person, and with them when they die. */
	CharacterCarried = 0,

	/** A ship's cargo hold: with the player rather than at the market. */
	ShipHold = 1,

	/** Rented storage at a station. The only place goods can back a market order. */
	StationHangar = 2,
};

/**
 * One container a character owns, whether or not anything is in it.
 *
 * Listed separately from contents because contents cannot describe an empty container, and a
 * transfer is addressed by inventory id — so a container a client cannot name is one it cannot move
 * goods into. The first haul anybody makes goes into a hold that is empty by definition.
 */
USTRUCT(BlueprintType)
struct SPACEMMOBACKEND_API FBackendInventoryContainer
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	int64 InventoryId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	EBackendInventoryKind Kind = EBackendInventoryKind::CharacterCarried;

	/** Which station holds it, or zero for anything not at one. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	int32 StationId = 0;
};

/** One stack in a character's inventory. */
USTRUCT(BlueprintType)
struct SPACEMMOBACKEND_API FBackendInventoryItem
{
	GENERATED_BODY()

	/** Which container it sits in. A transfer is addressed by this, not by kind and station. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	int64 InventoryId = 0;

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

	/**
	 * Where the stack is.
	 *
	 * A market order can only be placed against goods sitting at the station it is placed at, so a
	 * client that could not tell a hold from a hangar would offer to sell cargo that is with the
	 * player rather than at the market.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	EBackendInventoryKind Kind = EBackendInventoryKind::CharacterCarried;

	/** Which station holds it, or zero for anything not at one. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	int32 StationId = 0;
};

/**
 * A station, and whichever way it is placed.
 *
 * <strong>Two ways to be somewhere, and never both.</strong> A station on a body is placed by
 * direction from that body's centre, exactly like a deposit, and carries no altitude — how far
 * out the ground is at that direction is a question <c>FPlanetTerrain::SurfacePosition</c>
 * already answers identically on both machines. A station that orbits nothing has no centre for
 * a direction to be relative to, so it carries a system coordinate instead.
 *
 * A station may also be placed no way at all, which the server allows so that one can be
 * authored before anybody decides where it stands. Nothing is drawn for it and nothing can dock
 * at it, which is a station visibly missing rather than one that accepts docking from anywhere.
 */
USTRUCT(BlueprintType)
struct SPACEMMOBACKEND_API FBackendStation
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	int32 Id = 0;

	/** Stable content key, e.g. <c>station_capital_hub</c>. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	FString Key;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	FString Name;

	/** The body it stands on, or zero for a station that orbits nothing. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	int32 BodyId = 0;

	/** What it is for: TradingHub, Spaceport, Housing, Social, Capital. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	FString Kind;

	/** False when the server has no position for it. Nothing is drawn and nothing may dock. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	bool bPlaced = false;

	/** True when <see cref="Direction"/> is the answer, false when <see cref="Position"/> is. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	bool bOnBody = false;

	/** Direction from the body's centre. Meaningful only when <c>bOnBody</c>. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	FVector Direction = FVector::ZeroVector;

	/** System-space position. Meaningful only when <c>bPlaced</c> and not <c>bOnBody</c>. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	FSystemCoordinate Position;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	double DockingRangeKilometres = 5.0;
};

/** Which side of the order book, mirroring the server. */
UENUM(BlueprintType)
enum class EBackendOrderSide : uint8
{
	/** A bid: somebody wants to buy at or below their price. */
	Buy = 0,

	/** An ask: somebody wants to sell at or above their price. */
	Sell = 1,
};

/**
 * One resting order on the book.
 *
 * Prices are int64 minor units the whole way, never a float. A price that survives a round trip
 * through JSON as 12.34 can come back as 12.339999999999999, and in a market that eventually
 * becomes a real credit somebody is owed.
 */
USTRUCT(BlueprintType)
struct SPACEMMOBACKEND_API FBackendBookEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Market")
	int64 OrderId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Market")
	EBackendOrderSide Side = EBackendOrderSide::Buy;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Market")
	int64 PriceMinorUnits = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Market")
	int32 QuantityRemaining = 0;

	/**
	 * Whether this is the player's own order.
	 *
	 * A flag rather than a name: who placed an order is nobody's business, and the only thing the
	 * client needs is whether taking it would be a self-trade. Matching refuses those, so a buy
	 * placed against your own ask cannot cross it and simply rests -- leaving a crossed book that
	 * reads as a broken market rather than as a rule.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Market")
	bool bIsYours = false;
};

/**
 * One tradeable item, and what a station's market is doing with it.
 *
 * <strong>The whole tradeable catalogue, not only what is for sale.</strong> A player who wants
 * ferrite and holds none could not previously discover that a market for it existed (task 105), and
 * somebody placing a buy order needs to find an item precisely because nobody is selling it.
 */
USTRUCT(BlueprintType)
struct SPACEMMOBACKEND_API FBackendMarketListing
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Market")
	int32 ItemDefId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Market")
	FString ItemKey;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Market")
	FString Name;

	/**
	 * Cheapest anyone will sell for, and whether anybody is.
	 *
	 * A flag rather than a sentinel price, because zero is a legal price — asks are floored at one
	 * minor unit but a bid of nothing is expressible, and "free" must never be confused with
	 * "nobody is offering".
	 */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Market")
	int64 BestAskMinorUnits = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Market")
	bool bHasAsk = false;

	/** Most anyone will pay, and whether anybody will. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Market")
	int64 BestBidMinorUnits = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Market")
	bool bHasBid = false;

	/** How much is actually available to buy right now. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Market")
	int32 QuantityForSale = 0;

	/**
	 * What a standing order will always pay, and whether one exists.
	 *
	 * The floor a player can always sell into. Worth showing beside a market price precisely because
	 * it is there when no market price is — an item nobody is trading yet still has a number.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Market")
	int64 GuaranteedPriceMinorUnits = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Market")
	bool bHasGuaranteed = false;
};

/**
 * One of your own orders, still resting on a book somewhere.
 *
 * <strong>Every station, not only the one being stood in.</strong> Placing an order needs a place
 * (ADR-0008); finding one you have forgotten is the opposite case, and it is the case that matters,
 * because a resting order holds goods or credits the player cannot otherwise reach.
 */
USTRUCT(BlueprintType)
struct SPACEMMOBACKEND_API FBackendMyOrder
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Market")
	int64 OrderId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Market")
	int32 StationId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Market")
	FString StationName;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Market")
	int32 ItemDefId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Market")
	FString ItemName;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Market")
	EBackendOrderSide Side = EBackendOrderSide::Buy;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Market")
	int64 PriceMinorUnits = 0;

	/** What is left, not what was placed: a partly filled order is the one that would mislead. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Market")
	int32 QuantityRemaining = 0;

	/** Credits locked against it. Zero for a sell order. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Market")
	int64 EscrowedMinorUnits = 0;

	/** Goods held against it. Zero for a buy order. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Market")
	int32 ReservedQuantity = 0;
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

	/**
	 * Where this character was left docked, or 0.
	 *
	 * Carried on identity because that is the moment it is needed: a docked character has to be put
	 * back at their station rather than at a default spawn, or the record and the world disagree and
	 * a restart becomes a free trip to a market they are nowhere near (task 114).
	 */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	int32 DockedStationId = 0;
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

	/**
	 * What this body's ground looks like, and whether anybody has said.
	 *
	 * A planet's look is content, the same as its radius: Ares is red oxide and Grimhold is black
	 * slag because somebody decided and wrote it down in <c>data/universe/origin.json</c>. Unpainted
	 * is a working state -- the client keeps whatever material it was configured with.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	bool bHasAppearance = false;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	FLinearColor LowColour = FLinearColor::Black;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	FLinearColor HighColour = FLinearColor::White;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	FLinearColor RockColour = FLinearColor::Gray;

	/** Where the height blend starts and finishes, in fractions of the body's maximum relief. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	float HeightFrom = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	float HeightTo = 1.0f;

	/** And where rock begins and finishes covering, as the sine of the slope angle. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	float SlopeFrom = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	float SlopeTo = 1.0f;

	/**
	 * The shape of this body's ground, and whether anybody has authored one.
	 *
	 * Separate from the palette because they answer different questions: the palette says what a
	 * world is made of, this says whether it is swells, hills or crags. Two planets sharing a
	 * palette and differing here read as different places.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	bool bHasTerrain = false;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	int64 TerrainSeed = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	double MaxElevationKilometres = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	double BaseFrequency = 0.0;
};

/**
 * One owned item that does not stack — a tool, a weapon, a hull.
 *
 * Separate from FBackendInventoryItem because these are not stacks: two lasers at different
 * condition are two things, and a quantity of two would say they were one. Kept apart on the wire
 * for the same reason (ADR-0006 insures each instance against its own acquisition value).
 */
/**
 * What kind of thing an item is, mirroring <c>SpaceMMO.Domain.Items.ItemCategory</c>.
 *
 * <strong>The numbers are the contract.</strong> They arrive over the wire as integers, so a value
 * inserted in the middle on the server silently reclassifies everything after it here — a Hull
 * becoming a Weapon reads as a ship you cannot summon and a gun you cannot fire, with nothing in the
 * payload looking wrong. Append only.
 */
UENUM(BlueprintType)
enum class EBackendItemCategory : uint8
{
	Raw = 0,
	Refined = 1,
	Component = 2,
	Consumable = 3,
	Tool = 4,
	Module = 5,
	Armor = 6,
	Weapon = 7,
	Hull = 8,
};

USTRUCT(BlueprintType)
struct SPACEMMOBACKEND_API FBackendItemInstance
{
	GENERATED_BODY()

	/** Which container it sits in. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	int64 InventoryId = 0;

	/** Server-side id. What a transfer names. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	int64 Id = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	int32 ItemDefId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	FString ItemKey;

	/** What to call it to a player, e.g. "Crude Mining Laser". */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	FString Name;

	/** 0 to 100. Below a threshold the item is unusable until repaired. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	int32 Condition = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	EBackendInventoryKind Kind = EBackendInventoryKind::CharacterCarried;

	/** Set for a station hangar; zero for anything that travels with its owner. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	int32 StationId = 0;

	/**
	 * What kind of thing this is, so a hull can be told from a tool.
	 *
	 * The key looks like it would do the job and does not: <c>hull_shuttle</c> against
	 * <c>shuttle_hull_section</c> is one Component away from a prefix match being wrong, and it is
	 * already shipped.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	EBackendItemCategory Category = EBackendItemCategory::Raw;
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

	/** Tool this deposit needs, empty for bare hands. Machine-readable half. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	FString RequiredToolKey;

	/**
	 * What to call that tool to a player, e.g. "Crude Mining Laser".
	 *
	 * Carried so a deposit can say what it wants before it is swung at. The refusal is correct but
	 * it arrives afterwards, and knowing you need a laser is only useful while there is still time
	 * to go and craft one.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	FString RequiredToolName;

	/** Whether working this deposit needs a tool at all. */
	bool NeedsTool() const { return !RequiredToolKey.IsEmpty(); }
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

	/**
	 * True if the swing was stopped by a full pack rather than by time or by an empty deposit.
	 *
	 * The three reasons a gather yields nothing are not interchangeable, and two of them are worth
	 * waiting out while this one never is.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Backend")
	bool bNoRoom = false;
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
