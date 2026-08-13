#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "SpaceMMOBackendTypes.h"
#include "SpaceMMOPlayerController.generated.h"

/**
 * A connection, and which character the server has agreed it may act as.
 *
 * <strong>Identity lives here rather than on a pawn.</strong> A player swaps between a ship and a
 * character every time they land, and each swap destroys and spawns pawns; identity that lived on
 * one would be lost the moment somebody stepped out of their ship. The controller outlives all of
 * it.
 *
 * <strong>The claim is checked, not believed.</strong> The client sends the character id it wants
 * along with its session token, and the server hands both to the backend, which reports the
 * character only if the token really belongs to the account that owns it. Ids are sequential
 * integers, so an unchecked claim would let any player join as any character in the game and spend
 * its inventory. The command-line -GatherCharacterId= that this replaces was exactly that hole,
 * kept deliberately narrow while it was single-player scaffolding.
 */
UCLASS()
class SPACEMMOBACKEND_API ASpaceMMOPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ASpaceMMOPlayerController();

	virtual void BeginPlay() override;

	virtual void Tick(float DeltaSeconds) override;

	virtual void SetupInputComponent() override;

	/** Pushes identity onto each new pawn, since a player swaps between ship and character. */
	virtual void OnPossess(APawn* InPawn) override;

	/**
	 * Re-reads skills and inventory from the backend.
	 *
	 * Called after anything that changes them — gathering today, crafting and trading later. The
	 * client asks rather than being told because the backend owns the numbers; a client that
	 * incremented its own copy would be guessing, and would be wrong the moment two things happened
	 * at once.
	 */
	UFUNCTION(BlueprintCallable, Category = "SpaceMMO|Identity")
	void RefreshCharacterState();

	/**
	 * A line the panel carries for a few seconds — what a gather yielded, or why it was refused.
	 *
	 * Part of the panel rather than its own on-screen message, for the reason the panel itself is
	 * one entry: messages are ordered by slot rather than by key, the panel is redrawn every frame
	 * with a zero display time and so takes whatever slot the free list hands back, and a separate
	 * three-second message ends up in an order nothing here can influence — usually below a panel
	 * dozens of lines long, which is off the bottom of the screen. It appeared once and never again.
	 */
	void ShowTransientLine(const FString& Line);

	/** Creates the HUD widgets named in SpaceMMO HUD settings, if any are. */
	void CreateHud();

	/**
	 * Shows the HUD widgets that belong to the pawn the player is in, and hides the rest.
	 *
	 * <strong>This has to live here, not in the widgets.</strong> Slate drives
	 * <c>NativeTick</c> from <c>Paint</c> (<c>SWidget.cpp:1505</c>) and a compound widget arranges
	 * its children through an <c>EVisibility::Visible</c> filter
	 * (<c>SCompoundWidget.cpp:24</c>), so a collapsed widget is never painted and therefore never
	 * ticks. A widget that hides itself from its own tick can never show itself again — which is
	 * exactly what the flight readout did: it vanished on leaving the ship and stayed gone on
	 * getting back in.
	 *
	 * The controller's tick is unconditional, so the decision is sound here for every context the
	 * contextual HUD grows — flying, on foot and docked.
	 */
	void UpdateHudContext();

	/** The flight readout, or null when none is configured. */
	UPROPERTY()
	TObjectPtr<class USpaceMMOFlightReadout> FlightReadout;


	/**
	 * Builds the character panel's lines from backend state.
	 *
	 * Pure and static so the wording, ordering and empty cases can be tested without a world, a
	 * backend or a pawn — the same reason FormatGatherMessage is. Every interesting case here is an
	 * edge one: a brand-new character has no trained skills and nothing in the hold, and the panel
	 * has to say so rather than render as a blank space that reads like a bug.
	 */
	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Identity")
	static TArray<FString> BuildCharacterPanel(
		const FString& CharacterName,
		const FString& Balance,
		const TArray<FBackendSkill>& Skills,
		const TArray<FBackendInventoryItem>& Inventory,
		const TArray<FBackendItemInstance>& Instances);

	/**
	 * Renders a whole number with thousands separators, e.g. 1234567 as "1,234,567".
	 *
	 * Takes int64 rather than clamping to int32 for FString::FormatAsNumber. XP fits in 32 bits
	 * today, but a formatter that silently saturates is one that reports a wrong number confidently
	 * the first time it is pointed at a credit balance, which is int64 for exactly that reason
	 * (ADR-0005).
	 */
	/**
	 * What the deposit within reach is, and whether this character can work it.
	 *
	 * <strong>An empty Key means nothing is in reach</strong>, which is an ordinary state and says
	 * so rather than rendering a blank heading.
	 *
	 * Pure and static like the other panels, so every interesting case — a rock needing a tool the
	 * player does not carry, a tool they carry but have broken, a level they have not reached — is
	 * testable without a running game. The refusal a player would otherwise meet arrives after they
	 * have already walked there and pressed the key.
	 */
	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Identity")
	static TArray<FString> BuildNearbyPanel(
		const FBackendResourceNode& Node,
		const TArray<FBackendSkill>& Skills,
		const TArray<FBackendItemInstance>& Instances);

	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Identity")
	static FString GroupDigits(int64 Value);

	/**
	 * Builds the market panel: what is selected, what it would list at, and the book around it.
	 *
	 * Pure and static like the others. Both sides are sorted towards the spread — asks ascending,
	 * bids descending — because book order puts the least relevant price at the top of each side,
	 * which is backwards for somebody deciding whether to trade.
	 */
	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Market")
	static TArray<FString> BuildMarketPanel(
		const FString& ItemName,
		const TArray<FBackendBookEntry>& Book,
		int64 ListingPriceMinorUnits);

	/**
	 * Holdings that could back a sell order here: stocked station hangars, sorted as the panel
	 * lists them.
	 *
	 * An order is placed against goods at a station, so cargo riding along in a ship's hold cannot
	 * back one, and offering to sell it would produce a refusal the player could not act on.
	 */
	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Market")
	static TArray<FBackendInventoryItem> FilterSellable(
		const TArray<FBackendInventoryItem>& Holdings);

	/**
	 * Builds the quest panel's lines.
	 *
	 * Pure and static like the others, so the filtering can be tested without a backend. Finished
	 * quests are deliberately dropped: a journal listing everything ever completed buries the one
	 * line saying what to do next, which is the only line anybody is looking for.
	 */
	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Quests")
	static TArray<FString> BuildQuestPanel(
		const TArray<FBackendJournalEntry>& Journal,
		const TArray<FBackendAvailableQuest>& Available);

	/**
	 * Builds the industry panel's lines: what can be built, and what is cooking.
	 *
	 * Pure and static, like <see cref="BuildCharacterPanel"/>, so the selection arithmetic and the
	 * have-versus-need arithmetic can be tested without a backend.
	 *
	 * <strong>It reports quantities but never decides eligibility.</strong> Showing "20/8" is
	 * arithmetic over two numbers the server already sent. Concluding "you cannot build this" would
	 * be a second implementation of the skill, tool, material and fee gates, free to disagree with
	 * the real ones — so the player is always allowed to press, and the server answers.
	 */
	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Industry")
	static TArray<FString> BuildIndustryPanel(
		const TArray<FBackendRecipe>& Recipes,
		const TArray<FBackendIndustryJob>& Jobs,
		const TArray<FBackendInventoryItem>& Inventory,
		int32 SelectedIndex);

	virtual void GetLifetimeReplicatedProps(
		TArray<FLifetimeProperty>& OutLifetimeProps) const override;


	/**
	 * The character this connection may act as, or zero until the server has agreed.
	 *
	 * Server-authoritative and replicated to its owner only. A client reads it to know who it is;
	 * a client writing it changes nothing, because every decision that matters is taken on the
	 * server against the server's copy.
	 */
	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Identity")
	int32 GetCharacterId() const { return CharacterId; }

	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Identity")
	FString GetCharacterName() const { return CharacterName; }

	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Identity")
	bool IsIdentified() const { return CharacterId != 0; }

	/** Where this player's gathered material goes. Not yet chosen per player. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Identity")
	int32 StationId = 1;

	/**
	 * Whether the character panel is drawn.
	 *
	 * On by default. The panel is the only way to see that mining credited anything, and a display
	 * that has to be discovered before it can report is no better than no display at all.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpaceMMO|Identity")
	bool bShowCharacterPanel = true;

private:
	/**
	 * Begins the client-side half: sign in if needed, then present the token to the server.
	 *
	 * Credentials come from the command line because there is no login UI yet. That is a
	 * placeholder for the UI, not for the security model — the token still has to be earned from
	 * the backend, and the server still checks it.
	 */
	void BeginIdentifying();

	/**
	 * Finds an email and password: command line first, then secrets/player-login.txt.
	 *
	 * <strong>The file exists because command lines are not reliable here.</strong> A launch that
	 * passed <c>-BackendEmail=someone@gmail.com</c> arrived as <c>someone@gmail .com</c> — a space
	 * inserted before the dot — and FParse stops at whitespace, so the client cheerfully tried to
	 * log in as "someone@gmail" and got a 401 that looked like a wrong password. The same mangling
	 * turned -ShipStartX=39.56 into 39. A file has no quoting, no escaping and no shell between it
	 * and the value.
	 *
	 * Two lines: email, then password. Same directory as the service secret, and git-ignored for
	 * the same reason.
	 */
	static bool FindCredentials(FString& OutEmail, FString& OutPassword);

	UFUNCTION()
	void HandleSessionChanged(bool bIsSignedIn);

	UFUNCTION()
	void HandleBackendFailed(const FBackendFailure& Failure);

	UFUNCTION()
	void HandleCharactersLoaded();

	/** Sends whatever token and character the client now holds. */
	void PresentCredentials();

	UFUNCTION(Server, Reliable)
	void ServerIdentify(const FString& Token, int32 ClaimedCharacterId);

	/** Applies a resolved identity and tells anything that was waiting for it. */
	void AdoptIdentity(int32 ResolvedCharacterId, const FString& ResolvedName);

	/** Pushes the identity onto whatever the player is currently possessing. */
	void RefreshPossessedPawn();

	/** Draws the panel. Local client only; a dedicated server has nobody to draw for. */
	void DrawCharacterPanel();

	void ToggleCharacterPanel();

	/**
	 * Confines the mouse to the game window, or hands it back.
	 *
	 * <strong>Asserted in code rather than left to DefaultInput.ini.</strong> Those values are a
	 * viewport's starting state, and anything that changes input mode afterwards leaves them behind.
	 * Setting it here means the window owns the mouse whenever this controller is the one being
	 * played, however it got there.
	 */
	void ApplyMouseCapture();

	void ToggleMouseCapture();

	/**
	 * Whether the game currently owns the mouse. On by default.
	 *
	 * The release key exists because two clients share one desktop during testing, and a captured
	 * cursor cannot reach the other window. Alt-tab also works; this is the version that does not
	 * make the first client lose focus.
	 */
	bool bMouseCaptured = true;

	void CycleRecipe();

	void StartSelectedJob();

	/** Claims the first job the server says is ready. */
	void ClaimReadyJob();

	/** Sells a parcel of the first faction-bought stack in the hold. */
	void SellToFaction();

	/** Accepts the first quest the server says is available. */
	void AcceptNextQuest();

	/** <see cref="FilterSellable"/> over what the backend last sent. */
	TArray<FBackendInventoryItem> SellableHoldings() const;

	bool TryGetSelectedHolding(FBackendInventoryItem& OutItem) const;

	/** Stand-in for a price box. Well clear of the faction floor, which exists to be the worst deal. */
	/**
	 * The station the market keys off: where this character is docked, or zero.
	 *
	 * Not the StationId field, which is a scene-wide default for crafting and storage. The market
	 * is a place you have to be at, and asking with the wrong station is refused by the server --
	 * so asking with the right one is the client's job, not a hope.
	 */
	int32 DockedStationId() const;

	static int64 ListingPriceFor(const FBackendInventoryItem& Item);

	void CycleHolding();

	void RefreshBook();

	void ListSelectedForSale();

	void BuyBestAsk();

	/** Which sellable holding the H key has landed on. */
	int32 SelectedHoldingIndex = 0;

	/** Units per market action. Small, like the faction parcel, and for the same reason. */
	static constexpr int32 MarketParcel = 10;

	/**
	 * Units sold per press. Deliberately small.
	 *
	 * A faction standing order is the worst price in the game by design, so a key that emptied a
	 * hangar into it in one press would be a way to lose a lot of value very quickly. Small parcels
	 * make it a way out of being stuck rather than a way to sell.
	 */
	static constexpr int32 FactionSaleParcel = 10;

	UFUNCTION()
	void HandleIndustryChanged();

	UFUNCTION()
	void HandleIndustryMessage(const FString& Message, bool bSucceeded);

	/** Puts a short-lived line under the panel, in the same place gather results appear. */
	void ShowNotice(const FString& Message, bool bSucceeded);

	/**
	 * Polls what changes without this player doing anything: a job's remaining time, and the
	 * credits, goods and book that another player's fill moves.
	 */
	void PollServerState();

	FTimerHandle StateRefreshTimer;

	/** Which recipe the R key has landed on. Wraps, and survives the list being re-fetched. */
	int32 SelectedRecipeIndex = 0;

	/** Guards against subscribing twice, since identity can resolve more than once. */
	bool bIndustryBound = false;

	/** The backend subsystem, or null. */
	class USpaceMMOBackendClient* Backend() const;

	/**
	 * Key for the panel, which is drawn as one multi-line message rather than one message per row.
	 *
	 * Well clear of the navigation readouts the pawns draw, which use 1 through 11: two writers
	 * sharing a key overwrite each other, and the symptom is a line flickering between two unrelated
	 * pieces of text.
	 *
	 * The engine offers no way to order separate messages. It iterates its message map by slot, and
	 * a zero display time makes it delete and re-add every message each frame, so slots come back
	 * from a free list in an order nothing here decides.
	 */

	static constexpr int32 PanelMessageKey = 200;

	/** Rows the panel will draw before it starts saying how many it is hiding. */
	static constexpr int32 PanelMaxLines = 40;

	/**
	 * Key for the transient notice line.
	 *
	 * Separate from the panel so a refusal does not have to be rebuilt into it, and fixed so
	 * repeated presses replace the last notice rather than stacking a column of them. Where it lands
	 * relative to the panel is up to the engine, for the reason above.
	 */
	static constexpr int32 NoticeMessageKey = PanelMessageKey - 1;

	/** The transient line, and when it stops being shown. */
	FString TransientLine;

	double TransientLineExpiresAt = 0.0;

	/**
	 * The client's cue that the server has agreed who it is.
	 *
	 * Skills and inventory are fetched here rather than when the client picked a character, because
	 * until the server confirms the claim the client only has an intention. Loading a character's
	 * private state on the strength of an unconfirmed guess would show a player numbers that may not
	 * be theirs.
	 */
	UFUNCTION()
	void OnRep_CharacterId();

	UPROPERTY(ReplicatedUsing = OnRep_CharacterId)
	int32 CharacterId = 0;

	UPROPERTY(Replicated)
	FString CharacterName;

	/** Which character the client intends to play, from -CharacterId= or the first one it owns. */
	int32 DesiredCharacterId = 0;

	/** Guards against presenting twice when both delegates fire. */
	bool bPresented = false;
};
