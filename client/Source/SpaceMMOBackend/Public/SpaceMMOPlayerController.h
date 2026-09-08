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

	/**
	 * The exact half of remembering where a player was.
	 *
	 * <strong>Here rather than in EndPlay, and the engine decides that.</strong>
	 * <c>APlayerController::Destroyed</c> unpossesses or destroys the pawn before it calls up to
	 * <c>AActor::Destroyed</c>, which is what routes EndPlay (Actor.cpp:3311) — so a position read
	 * in EndPlay is read from a controller that no longer has a pawn to read it from. It would have
	 * written nothing, silently, and looked exactly like a player who never moved.
	 *
	 * There is no logout in this game: closing the window drops a connection, and this is what the
	 * server runs when one goes. It is the write that makes "where you were when you quit" exact
	 * rather than up to fifteen seconds stale.
	 */
	virtual void Destroyed() override;

	/** Server shutdown, where nothing is destroyed and EndPlay is all there is (task 147). */
	virtual void EndPlay(EEndPlayReason::Type EndPlayReason) override;

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
	 * Records where this connection is now docked, so a later pawn is handed the right station.
	 *
	 * Server-side, and called by the docking component whichever way the state changed: pressing
	 * the key, flying out of range, or docking a ship and stepping off it.
	 */
	void NoteDockedStation(int32 StationId) { ResumeAtStationId = StationId; }

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

	/** The fleet, and what can be brought here (ADR-0012). */
	void ShowShipsTab();

	/**
	 * Prints where the player is standing, as a direction ready to paste into content. Bound to P.
	 *
	 * <strong>The cheap half of task 96.</strong> Everything in the world is authored as a direction
	 * from a body's centre — deposits, stations, and caves when they land — and working one out by
	 * hand means guessing a unit vector for a place you can see. This is that number, for wherever
	 * somebody is standing.
	 *
	 * It does not write to <c>data/</c>, and deliberately: a tool that edited content while the game
	 * ran would be a second writer racing the seeder, and the export is the part that has to be
	 * right. Copying one line out of a log is a smaller thing to get wrong.
	 */
	void CaptureDirection();

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

	UPROPERTY()
	TObjectPtr<class USpaceMMOCrosshair> Crosshair;

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
	 *
	 * <strong>Kept current after sign-in as well, via NoteDockedStation.</strong> It used to hold
	 * whatever was true when identity resolved, forever -- so a player who signed in at the capital,
	 * flew to Grimhold and docked would have the <em>capital</em> pushed onto the next pawn they
	 * possessed, and the range check would undock them from a station they were standing at. Docking
	 * a ship makes that reachable in one keypress, because the pawn changes at the moment you dock
	 * (task 153).
	 */
	int32 ResumeAtStationId = 0;

	/** So docking somewhere new can open the overlay, and undocking can close it. */
	int32 LastDockedStationId = 0;

	/**
	 * Where the backend says this character was last seen, and what they were doing.
	 *
	 * Held for the same reason ResumeAtStationId is: identity and possession race, and whichever
	 * of them is last has to be the one that puts the player back.
	 */
	bool bHasResumePosition = false;

	FVector ResumePositionKilometres = FVector::ZeroVector;

	bool bResumeFlying = false;

	/**
	 * Whether this connection has been put where it belongs, one way or the other.
	 *
	 * <strong>Guards the write as much as the restore.</strong> Until this is true the pawn is
	 * standing wherever the spawn put it, and recording that would overwrite the very position
	 * about to be restored — turning a crash during sign-in into a permanent trip back to the
	 * starting point.
	 */
	bool bPlacedForThisSession = false;


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
	 * Why the accept key did nothing, in a sentence a player can act on.
	 *
	 * <strong>"Nothing to accept" was true and useless.</strong> A character partway through the
	 * onboarding chain has no available quest precisely <em>because</em> they have one running, and
	 * the refusal said neither half of that — so a quest sitting at 0/10 for three weeks read as a
	 * broken quest system rather than as one waiting to be worked on.
	 *
	 * Empty when there is genuinely nothing to say, which is a character with no active quest and
	 * nothing on offer: that one is rare and is not a mistake anybody is making.
	 */
	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Quests")
	static FString AcceptRefusal(const TArray<FBackendJournalEntry>& Journal);

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
	void AdoptIdentity(const FBackendResolvedCharacter& Resolved);

	/** Pushes the identity onto whatever the player is currently possessing. */
	void RefreshPossessedPawn();

	/**
	 * Puts a pawn in the world for the hull this character has summoned (ADR-0012, task 115).
	 *
	 * <strong>Server-side, and asked rather than told.</strong> The summon itself is a request the
	 * client made and the backend accepted; what exists in the world is the simulation's to decide,
	 * so this asks the backend which hull is actually this character's and where it is parked
	 * before spawning anything. A client saying "I summoned a freighter" is a client naming a ship
	 * it would like to have.
	 *
	 * Idempotent: a pawn already carrying that hull id is the answer, not a reason to make another.
	 * Called on summoning and on signing in, and those overlap every time somebody quits beside
	 * their parked ship.
	 */
	void EnsureActiveShipInWorld();

	/** Spawns the ship a resolved answer describes, beside the station it is parked at. */
	void PlaceSummonedShip(const struct FBackendActiveShip& Ship);

	/**
	 * Writes down where one of this character's hulls is standing (task 155).
	 *
	 * Called when a pawn is put in the world and while one is flown. A hull with no position is in
	 * a hangar and gets no pawn, so this is what makes a ship exist across a restart.
	 */
	void RecordShipWhereabouts(int64 HullItemInstanceId, const FSystemCoordinate& Where);

	/**
	 * Tells the backend what this character is sitting in, when it changes.
	 *
	 * Read off the possessed pawn rather than hooked into boarding, because every route into and
	 * out of a ship comes through possession — boarding, stepping out, being restored into a ship
	 * on sign-in (task 147), and any route nobody has written yet.
	 */
	void ReportBoarding();

	/** The hull the backend has been told this character is in, or 0. Stops repeat requests. */
	int64 ReportedAboardHullId = 0;

	UFUNCTION(Server, Reliable)
	void ServerShipSummoned();

	UFUNCTION()
	void HandleShipSummoned(const FString& ShipName);

	/** How far from a station a summoned ship waits, in kilometres. */
	static constexpr double SummonedShipOffsetKilometres = 0.03;

	/**
	 * How far above the ground a summoned ship is placed, in kilometres.
	 *
	 * Above rather than on: the server simulates every ship pawn whether anybody is flying it or
	 * not, so one placed a few metres up settles onto the terrain the same way a landing ship does.
	 * Placed exactly on the surface it would spend its first frame resolving its way out of the
	 * ground it was already inside.
	 */
	static constexpr double SummonedShipLiftKilometres = 0.005;

	/**
	 * Puts a returning player back where they were, on foot or flying (task 147).
	 *
	 * Runs on the server, once, as soon as both an identity and a pawn exist. Restoring a player
	 * who was flying means giving them a ship pawn at that position and possessing it, which is the
	 * same swap boarding does — so a player who quit in flight resumes in flight rather than
	 * standing in the air where their ship used to be.
	 */
	void RestoreWhereabouts();

	/**
	 * Tells the backend where this player is now.
	 *
	 * Server-side, and it reads the position off the pawn rather than being told one: what is
	 * being recorded is where the simulation has put somebody, and the simulation is here.
	 */
	void RecordWhereabouts();

	FTimerHandle WhereaboutsTimer;

	/**
	 * How often a position is written while somebody is playing.
	 *
	 * The floor under the write on disconnect, and only that. Fifteen seconds of travel is what a
	 * hard crash costs, which is a walk back rather than a lost session; a faster timer would buy
	 * very little and cost a database write per player per tick of it.
	 */
	static constexpr float WhereaboutsIntervalSeconds = 15.0f;

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
