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

	/**
	 * Says something to the player, above their pawn, for a few seconds.
	 *
	 * Falls back to the debug panel's transient line when no transient-message Widget Blueprint is
	 * configured, so a message is never simply lost — see <see cref="ShowTransientLine"/>, which is
	 * what this replaces and what it degrades to.
	 */
	void ShowTransientMessage(const FString& Line, ESpaceMMOMessageTone Tone);

	/** Opens and closes the skills screen. Bound to K. */
	void ToggleSkillsScreen();

	/**
	 * Opens and closes the inventory screen. Bound to I.
	 *
	 * Refreshes on opening rather than polling, because what a player owns changes on the server and
	 * a screen opened to explain a missing item is the worst moment to be showing a stale copy.
	 */
	void ToggleInventoryScreen();

	/**
	 * Opens and closes the station overlay. Bound to Tab, and opened by docking.
	 *
	 * Does nothing when not docked: the overlay is about a place, and a refusal message on every
	 * stray keypress would get old faster than the information is worth.
	 */
	void ToggleStationOverlay();

	/**
	 * Which station the player is docked at, or 0.
	 *
	 * Public because the inventory screen dims goods it cannot reach, and reaching them is exactly
	 * the question of whether they are here — the same rule the API enforces on a transfer.
	 */
	int32 DockedStationId() const;

	/**
	 * Switches the station overlay's tab. Bound to 1, 2 and 3.
	 *
	 * Only while the overlay is open, so the number keys stay free for anything else later — and so
	 * pressing them in flight cannot change something the player cannot see.
	 */
	void ShowMarketTab();
	void ShowIndustryTab();
	void ShowQuestsTab();

	/** And 4, for the orders this character has resting anywhere. */
	void ShowMyOrdersTab();

	/**
	 * The three panels the station overlay renders, and where the player is.
	 *
	 * Assembled here rather than in the widget because the selection state and the price arithmetic
	 * are the controller's, and because these are the same pure builders the debug panel uses — the
	 * only automated coverage the HUD's wording has.
	 */
	void GetStationPanels(
		FString& OutStationName,
		TArray<FString>& OutIndustry,
		TArray<FString>& OutQuests) const;

	/** The flight readout, or null when none is configured. */
	UPROPERTY()
	TObjectPtr<class USpaceMMOFlightReadout> FlightReadout;

	/** Name and credits while on foot, or null when none is configured. */
	UPROPERTY()
	TObjectPtr<class USpaceMMOOnFootReadout> OnFootReadout;

	/** The deposit prompt above the reticle, or null when none is configured. */
	UPROPERTY()
	TObjectPtr<class USpaceMMODepositPrompt> DepositPrompt;

	/** The skills screen, or null when none is configured. */
	UPROPERTY()
	TObjectPtr<class USpaceMMOSkillsScreen> SkillsScreen;

	/**
	 * Messages floating above the pawn, or null when none is configured.
	 *
	 * Deliberately absent from UpdateHudContext: it belongs to every context, and it has to keep
	 * ticking to expire its own messages.
	 */
	UPROPERTY()
	TObjectPtr<class USpaceMMOTransientMessages> TransientMessages;

	/** The station screen shown while docked, or null when none is configured. */
	UPROPERTY()
	TObjectPtr<class USpaceMMOStationOverlay> StationOverlay;

	/** Everything the character owns, or null when none is configured. */
	UPROPERTY()
	TObjectPtr<class USpaceMMOInventoryScreen> InventoryScreen;

	/** The sign-in screen, or null when none is configured. */
	UPROPERTY()
	TObjectPtr<class USpaceMMOLoginScreen> LoginScreen;

	/**
	 * Whether the player still has to sign in.
	 *
	 * Set when BeginIdentifying finds no credentials anywhere and a login screen exists to ask on.
	 * Everything else stays hidden while this is true: a HUD over a world you have no character for
	 * is a set of readouts about nobody.
	 */
	bool bAwaitingSignIn = false;

	/**
	 * Whether the skills screen is open.
	 *
	 * Held here rather than read back off the widget, because the widget stops ticking while it is
	 * closed and so cannot be asked anything about itself.
	 */
	bool bSkillsScreenOpen = false;

	/** Whether the station overlay is open. Same reasoning as bSkillsScreenOpen. */
	bool bStationOverlayOpen = false;

	/** Whether the inventory screen is open. Same reasoning as bSkillsScreenOpen. */
	bool bInventoryScreenOpen = false;

	/**
	 * Where the backend says this character was left docked, or 0.
	 *
	 * Held because identity and possession race: a pawn can arrive before or after the answer does,
	 * and whichever is last has to be the one that puts the ship back at the station.
	 */
	int32 ResumeAtStationId = 0;

	/** So docking somewhere new can open the overlay, and undocking can close it. */
	int32 LastDockedStationId = 0;


	/**
	 * Renders a whole number with thousands separators, e.g. 1234567 as "1,234,567".
	 *
	 * Takes int64 rather than clamping to int32 for FString::FormatAsNumber. XP fits in 32 bits
	 * today, but a formatter that silently saturates is one that reports a wrong number confidently
	 * the first time it is pointed at a credit balance, which is int64 for exactly that reason
	 * (ADR-0005).
	 *
	 * Outlived the character panel it was written for: the skills screen and the inventory screen
	 * both use it.
	 */
	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Identity")
	static FString GroupDigits(int64 Value);

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
	 * Pure and static, like the other panel builders, so the selection arithmetic and the
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

	/**
	 * This character's balance, formatted, or empty until the character list has been read.
	 *
	 * Empty rather than "0" deliberately: a confident zero is indistinguishable from being broke,
	 * and the two want different reactions from whoever is reading it.
	 */
	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Identity")
	FString GetCharacterBalance() const;

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
	void AdoptIdentity(
		int32 ResolvedCharacterId, const FString& ResolvedName, int32 ResolvedDockedStationId);

	/** Pushes the identity onto whatever the player is currently possessing. */
	void RefreshPossessedPawn();

	/** Draws the panel. Local client only; a dedicated server has nobody to draw for. */
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
	 * Whether the player wants the game to own the mouse. On by default.
	 *
	 * The release key exists because two clients share one desktop during testing, and a captured
	 * cursor cannot reach the other window. Alt-tab also works; this is the version that does not
	 * make the first client lose focus.
	 *
	 * <strong>A preference, not the current state.</strong> An open screen overrides it without
	 * changing it, so closing the screen restores whatever the player had chosen — see
	 * <c>ApplyMouseCapture</c>.
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

	/**
	 * The station the market keys off: where this character is docked, or zero.
	 *
	 * Not the StationId field, which is a scene-wide default for crafting and storage. The market
	 * is a place you have to be at, and asking with the wrong station is refused by the server --
	 * so asking with the right one is the client's job, not a hope.
	 */

	void RefreshBook();

	/** Which sellable holding the H key has landed on. */
	/** Units per market action. Small, like the faction parcel, and for the same reason. */
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
	 * Keys for the two on-screen messages that outlived the character panel.
	 *
	 * Well clear of the navigation readouts the pawns draw, which use 1 through 11: two writers
	 * sharing a key overwrite each other, and the symptom is a line flickering between two unrelated
	 * pieces of text. Fixed rather than allocated, so repeated presses replace the last message
	 * rather than stacking a column of them.
	 *
	 * The engine offers no way to order separate messages -- it iterates its map by slot, and a zero
	 * display time makes it delete and re-add each one every frame, so slots come back from a free
	 * list in an order nothing here decides. That is what drove the whole HUD into UMG. These two
	 * survive because they are single lines with nothing to be ordered against.
	 */
	static constexpr int32 NoticeMessageKey = 199;

	/** Only used when no transient-message Widget Blueprint is configured. */
	static constexpr int32 TransientMessageKey = 198;


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
