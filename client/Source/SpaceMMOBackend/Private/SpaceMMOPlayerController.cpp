#include "SpaceMMOPlayerController.h"

#include "Components/InputComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"
#include "SpaceMMOBackendClient.h"
#include "SpaceMMOBackendLog.h"
#include "SpaceMMOBackendProtocol.h"
#include "SpaceMMODepositActor.h"
#include "SpaceMMOFlightReadout.h"
#include "SpaceMMOHudSettings.h"
#include "SpaceMMOInventoryScreen.h"
#include "SpaceMMOLoginScreen.h"
#include "SpaceMMODockingComponent.h"
#include "SpaceMMODepositPrompt.h"
#include "SpaceMMOGatheringComponent.h"
#include "SpaceMMOCrosshair.h"
#include "SpaceMMOOnFootReadout.h"
#include "SpaceMMOCharacterPawn.h"
#include "SpaceMMOBoarding.h"
#include "SpaceMMOPlanetActor.h"
#include "SpaceMMOPlanetTerrain.h"
#include "SpaceMMOShipPawn.h"
#include "SpaceMMOStationActor.h"
#include "SpaceMMOSkillsScreen.h"
#include "SpaceMMOStationOverlay.h"
#include "SpaceMMOTransientMessages.h"

namespace
{
	/**
	 * Where a summoned ship waits: on the ground, a short walk from the station.
	 *
	 * <strong>The sideways step is FBoarding's, not a new one.</strong> Offsetting "to the side" of
	 * something standing on a sphere is the same arithmetic as stepping out of a parked ship, and
	 * the naive version -- add thirty metres of a world axis -- buries the result in the hillside
	 * whenever the station is not near the pole that axis points at.
	 *
	 * The ground is then asked where it is, rather than assumed to be at the station's own height:
	 * a station sits on the terrain under it, and thirty metres away the terrain is somewhere else.
	 */
	bool ParkingPositionBeside(
		UWorld* World,
		const ASpaceMMOStationActor& Station,
		const double OffsetKilometres,
		const double LiftKilometres,
		FSystemCoordinate& OutPosition)
	{
		const FSystemCoordinate StationPosition = Station.GetSystemPosition();

		for (TActorIterator<ASpaceMMOPlanetActor> It(World); It; ++It)
		{
			const FPlanetConfig& Planet = It->GetPlanetConfig();

			const FVector Up =
				(StationPosition.Kilometres - Planet.Centre.Kilometres).GetSafeNormal();

			if (Up.IsNearlyZero())
			{
				continue;
			}

			// Any tangent direction will do -- there is no side of a station that is its front --
			// and StepOutPosition flattens whatever it is given into the tangent plane, so a world
			// axis is a perfectly good thing to hand it.
			const FSystemCoordinate Beside = FBoarding::StepOutPosition(
				StationPosition, Up, FVector::UpVector, OffsetKilometres);

			const FVector BesideDirection =
				(Beside.Kilometres - Planet.Centre.Kilometres).GetSafeNormal();

			if (BesideDirection.IsNearlyZero())
			{
				continue;
			}

			OutPosition = FSystemCoordinate(
				FPlanetTerrain::SurfacePosition(
					Planet, It->GetTerrainConfig(), BesideDirection).Kilometres
				+ (BesideDirection * LiftKilometres));

			return true;
		}

		return false;
	}
}

ASpaceMMOPlayerController::ASpaceMMOPlayerController()
{
	bReplicates = true;

	// The panel is redrawn from current state each frame, the same way the pawns draw their
	// navigation readouts. Controllers do not tick by default.
	PrimaryActorTick.bCanEverTick = true;
}

void ASpaceMMOPlayerController::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Owner only. Who else is playing is not a secret, but it is not this controller's business to
	// broadcast it, and replicating identity to every connection would put every player's character
	// id in every other player's memory for no gain.
	DOREPLIFETIME_CONDITION(ASpaceMMOPlayerController, CharacterId, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ASpaceMMOPlayerController, CharacterName, COND_OwnerOnly);
}

void ASpaceMMOPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// Only the machine sitting in front of the player has a token to present.
	if (IsLocalController())
	{
		// The HUD first. BeginIdentifying decides whether to ask for a sign-in, and it cannot ask on
		// a screen that does not exist yet -- which is what happened when this ran the other way
		// round: it logged "no login screen configured" and the screen was created 128 ms later.
		CreateHud();

		ApplyMouseCapture();
		BeginIdentifying();
	}
}

namespace
{
	/**
	 * Loads one configured HUD widget and puts it in the viewport, or says why it did not.
	 *
	 * Unset is a legitimate state rather than a fault: the game has to run for anyone who has not
	 * made the Widget Blueprint yet, and the automated runs have no viewport to add one to at all.
	 * Named-but-wrong is a mistake somebody wants telling about, because from the outside a typo'd
	 * path and an unset one look identical.
	 */
	template <typename WidgetType>
	WidgetType* CreateHudWidget(
		APlayerController* Owner,
		const FSoftClassPath& Path,
		const TCHAR* What)
	{
		if (!Path.IsValid())
		{
			return nullptr;
		}

		UClass* WidgetClass = Path.TryLoadClass<WidgetType>();

		if (WidgetClass == nullptr)
		{
			UE_LOG(LogSpaceMMOBackend, Warning,
				TEXT("HUD: '%s' is not the right class for the %s; it will not be shown."),
				*Path.ToString(), What);

			return nullptr;
		}

		WidgetType* Widget = CreateWidget<WidgetType>(Owner, WidgetClass);

		// Says it happened, because "no warning" and "never ran" look identical from a log
		// otherwise -- and this runs behind a setting, on the local controller only, in a build
		// that may have no viewport at all.
		UE_LOG(LogSpaceMMOBackend, Log,
			TEXT("HUD: %s %s from '%s'."),
			What,
			Widget != nullptr ? TEXT("created") : TEXT("FAILED to create"),
			*Path.ToString());

		if (Widget != nullptr)
		{
			// Added once and left in the viewport; UpdateHudContext shows and hides it from the
			// controller's tick, because a widget cannot restore its own visibility once it has
			// dropped it.
			Widget->AddToViewport();
		}

		return Widget;
	}
}

void ASpaceMMOPlayerController::CreateHud()
{
	const USpaceMMOHudSettings* Settings = GetDefault<USpaceMMOHudSettings>();

	if (Settings == nullptr)
	{
		return;
	}

	// The flight readout's debug line follows the ship's own flight-debug flag, read every tick
	// rather than set here, so toggling that flag takes effect without a restart.
	FlightReadout = CreateHudWidget<USpaceMMOFlightReadout>(
		this, Settings->FlightReadout, TEXT("flight readout"));

	OnFootReadout = CreateHudWidget<USpaceMMOOnFootReadout>(
		this, Settings->OnFootReadout, TEXT("on-foot readout"));

	Crosshair = CreateHudWidget<USpaceMMOCrosshair>(
		this, Settings->Crosshair, TEXT("crosshair"));

	DepositPrompt = CreateHudWidget<USpaceMMODepositPrompt>(
		this, Settings->DepositPrompt, TEXT("deposit prompt"));

	SkillsScreen = CreateHudWidget<USpaceMMOSkillsScreen>(
		this, Settings->SkillsScreen, TEXT("skills screen"));

	TransientMessages = CreateHudWidget<USpaceMMOTransientMessages>(
		this, Settings->TransientMessages, TEXT("transient messages"));

	StationOverlay = CreateHudWidget<USpaceMMOStationOverlay>(
		this, Settings->StationOverlay, TEXT("station overlay"));

	InventoryScreen = CreateHudWidget<USpaceMMOInventoryScreen>(
		this, Settings->InventoryScreen, TEXT("inventory screen"));

	LoginScreen = CreateHudWidget<USpaceMMOLoginScreen>(
		this, Settings->LoginScreen, TEXT("login screen"));

	// Decide what belongs on screen now, rather than on the first tick.
	//
	// AddToViewport leaves a widget visible, and until something says otherwise every screen here is
	// showing -- so the inventory screen and the station overlay both appeared for a frame or two at
	// startup before the first tick collapsed them. One call closes that window entirely, and it has
	// to be after all six exist because the pairing depends on knowing which others are open.
	UpdateHudContext();
}

void ASpaceMMOPlayerController::ShowTransientMessage(
	const FString& Line, const ESpaceMMOMessageTone Tone)
{
	if (TransientMessages != nullptr)
	{
		TransientMessages->Push(Line, Tone);

		return;
	}

	// No Widget Blueprint configured. The panel line is worse -- it has no colour and no position --
	// but a message a player never sees is worse still, and the gather result is the only feedback
	// that a key press did anything at all.
	ShowTransientLine(Line);
}

void ASpaceMMOPlayerController::ApplyMouseCapture()
{
	if (!IsLocalController())
	{
		return;
	}

	// A screen you click on outranks the preference, and gives it back untouched on closing. The
	// alternative -- each screen remembering what the mouse was doing before it opened -- needs one
	// flag per screen, and two open at once then disagree about who restores what: closing the
	// inventory would snatch the cursor back while the station overlay was still waiting for a click.
	// The login screen counts too, and outranks everything: a captured cursor cannot reach a text
	// box, so a sign-in screen without this is one nobody can type into.
	const bool bScreenWantsCursor =
		bAwaitingSignIn || bInventoryScreenOpen || bStationOverlayOpen;

	if (bMouseCaptured && !bScreenWantsCursor)
	{
		// Set in code as well as in DefaultInput.ini. The ini values are defaults for a viewport,
		// and anything that changes input mode later -- a menu, a level transition, the editor's
		// own play-in-window handling -- leaves them behind. Asserting it here means the game window
		// owns the mouse whenever this controller is the one being played.
		SetInputMode(FInputModeGameOnly());

		bShowMouseCursor = false;

		return;
	}

	// UIOnly while signing in, GameAndUI otherwise. The difference is that GameAndUI still delivers
	// to the pawn, so every keystroke of an email was also rolling and pitching the ship behind the
	// screen -- a player typing is not a player flying, and this is the only screen that is true of.
	if (bAwaitingSignIn && LoginScreen != nullptr)
	{
		FInputModeUIOnly SignInInput;
		SignInInput.SetWidgetToFocus(LoginScreen->TakeWidget());
		SignInInput.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);

		SetInputMode(SignInInput);

		bShowMouseCursor = true;

		return;
	}

	SetInputMode(FInputModeGameAndUI()
		.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock)
		.SetHideCursorDuringCapture(false));

	bShowMouseCursor = true;
}

void ASpaceMMOPlayerController::ToggleMouseCapture()
{
	bMouseCaptured = !bMouseCaptured;

	ApplyMouseCapture();

	// What actually happened, not what was asked for. An open screen keeps the cursor either way,
	// and "Mouse captured" over a cursor still sitting on the screen reads as a broken key.
	const bool bHeldByScreen = bInventoryScreenOpen || bStationOverlayOpen;

	if (bMouseCaptured && bHeldByScreen)
	{
		ShowNotice(TEXT("Mouse stays free while a screen is open"), false);

		return;
	}

	ShowNotice(
		bMouseCaptured ? TEXT("Mouse captured") : TEXT("Mouse released - M to recapture"),
		bMouseCaptured);
}

void ASpaceMMOPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	RefreshPossessedPawn();
}

void ASpaceMMOPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (InputComponent != nullptr)
	{
		InputComponent->BindAction(
			TEXT("ToggleSkills"), IE_Pressed, this, &ASpaceMMOPlayerController::ToggleSkillsScreen);

		InputComponent->BindAction(
			TEXT("ToggleInventory"),
			IE_Pressed,
			this,
			&ASpaceMMOPlayerController::ToggleInventoryScreen);

		InputComponent->BindAction(
			TEXT("ToggleStation"),
			IE_Pressed,
			this,
			&ASpaceMMOPlayerController::ToggleStationOverlay);

		InputComponent->BindAction(
			TEXT("StationTabMarket"), IE_Pressed, this, &ASpaceMMOPlayerController::ShowMarketTab);

		InputComponent->BindAction(
			TEXT("StationTabIndustry"),
			IE_Pressed,
			this,
			&ASpaceMMOPlayerController::ShowIndustryTab);

		InputComponent->BindAction(
			TEXT("StationTabQuests"), IE_Pressed, this, &ASpaceMMOPlayerController::ShowQuestsTab);

		InputComponent->BindAction(
			TEXT("StationTabMyOrders"), IE_Pressed, this, &ASpaceMMOPlayerController::ShowMyOrdersTab);

		InputComponent->BindAction(
			TEXT("StationTabShips"), IE_Pressed, this, &ASpaceMMOPlayerController::ShowShipsTab);

		InputComponent->BindAction(
			TEXT("CaptureDirection"), IE_Pressed, this,
			&ASpaceMMOPlayerController::CaptureDirection);

		InputComponent->BindAction(
			TEXT("CycleRecipe"), IE_Pressed, this, &ASpaceMMOPlayerController::CycleRecipe);

		InputComponent->BindAction(
			TEXT("StartJob"), IE_Pressed, this, &ASpaceMMOPlayerController::StartSelectedJob);

		InputComponent->BindAction(
			TEXT("ClaimJob"), IE_Pressed, this, &ASpaceMMOPlayerController::ClaimReadyJob);

		InputComponent->BindAction(
			TEXT("SellToFaction"), IE_Pressed, this, &ASpaceMMOPlayerController::SellToFaction);

		InputComponent->BindAction(
			TEXT("AcceptQuest"), IE_Pressed, this, &ASpaceMMOPlayerController::AcceptNextQuest);

		InputComponent->BindAction(
			TEXT("ToggleMouseCapture"),
			IE_Pressed,
			this,
			&ASpaceMMOPlayerController::ToggleMouseCapture);
	}
}

int32 ASpaceMMOPlayerController::DockedStationId() const
{
	const USpaceMMOBackendClient* Client = Backend();

	// Zero when not docked, which the server refuses. Substituting the scene default here would
	// turn "you are not at a station" into a request that looks legitimate and fails for a reason
	// the player cannot see.
	return Client != nullptr ? Client->GetDockedStationId() : 0;
}

USpaceMMOBackendClient* ASpaceMMOPlayerController::Backend() const
{
	const UWorld* World = GetWorld();

	const UGameInstance* GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;

	return GameInstance != nullptr
		? GameInstance->GetSubsystem<USpaceMMOBackendClient>()
		: nullptr;
}

void ASpaceMMOPlayerController::CycleRecipe()
{
	const USpaceMMOBackendClient* Client = Backend();

	if (Client == nullptr || Client->GetRecipes().Num() == 0)
	{
		return;
	}

	SelectedRecipeIndex = (SelectedRecipeIndex + 1) % Client->GetRecipes().Num();
}

void ASpaceMMOPlayerController::StartSelectedJob()
{
	USpaceMMOBackendClient* Client = Backend();

	if (Client == nullptr || CharacterId == 0)
	{
		return;
	}

	const TArray<FBackendRecipe>& Available = Client->GetRecipes();

	if (!Available.IsValidIndex(SelectedRecipeIndex))
	{
		return;
	}

	// One run. Batching is a real feature — it is how a player makes forty plates without forty
	// keypresses — but it needs a way to choose a count, and there is no UI to choose one in.
	// Where the player actually is, not a fixed station.
	//
	// This used to send a hardcoded 1, and IndustryService uses that one station for both halves --
	// it consumes the inputs from that hangar and deposits the output back into it. So a job started
	// anywhere in the system was really run at the capital, which is self-consistent and therefore
	// invisible until somebody docks elsewhere and looks (task 111).
	const int32 Station = DockedStationId();

	if (Station == 0)
	{
		// Refused here rather than by the server, because the reason is about where the player is
		// and they can see that for themselves the moment it is said.
		ShowTransientMessage(
			TEXT("Dock at a station to start a job"), ESpaceMMOMessageTone::Warning);

		return;
	}

	Client->StartJob(CharacterId, Available[SelectedRecipeIndex].Id, Station, 1);
}

void ASpaceMMOPlayerController::ClaimReadyJob()
{
	USpaceMMOBackendClient* Client = Backend();

	if (Client == nullptr || CharacterId == 0)
	{
		return;
	}

	// The server's flag, not a comparison done here. Claiming the first ready one rather than all
	// of them keeps each press to a single answer the player can read.
	for (const FBackendIndustryJob& Job : Client->GetJobs())
	{
		if (Job.bIsClaimable)
		{
			Client->ClaimJob(CharacterId, Job.Id);

			return;
		}
	}

	ShowNotice(TEXT("Nothing ready to claim"), false);
}

TArray<FBackendInventoryItem> ASpaceMMOPlayerController::FilterSellable(
	const TArray<FBackendInventoryItem>& Holdings)
{
	// Station hangars only. An order is placed against goods at a station, so cargo riding along in
	// a ship's hold cannot back one — and offering to sell it would produce a refusal the player
	// could not act on.
	TArray<FBackendInventoryItem> Sellable = Holdings.FilterByPredicate(
		[](const FBackendInventoryItem& Item)
		{
			return Item.Kind == EBackendInventoryKind::StationHangar && Item.Quantity > 0;
		});

	Sellable.Sort([](const FBackendInventoryItem& A, const FBackendInventoryItem& B)
		{ return A.Name < B.Name; });

	return Sellable;
}

void ASpaceMMOPlayerController::RefreshBook()
{
	USpaceMMOBackendClient* Client = Backend();

	// Whatever the market screen is showing, which is the only thing that can be looked at. This
	// used to re-ask for the player's selected holding every two seconds, on the poll -- so clicking
	// a catalogue row fetched the right book and had it overwritten by the old one moments later.
	// On screen that read as the prices flashing and reverting rather than as two fetches disagreeing.
	const int32 ItemDefId =
		StationOverlay != nullptr ? StationOverlay->GetSelectedItemDefId() : 0;

	if (Client != nullptr && ItemDefId != 0)
	{
		Client->FetchBook(DockedStationId(), ItemDefId);
	}
}

void ASpaceMMOPlayerController::AcceptNextQuest()
{
	USpaceMMOBackendClient* Client = Backend();

	if (Client == nullptr || CharacterId == 0)
	{
		return;
	}

	// Finished work first. A player standing on a quest that is done wants paying, and offering
	// them the next one before the reward for the last is the wrong order to do two things in --
	// the chain hands out one at a time, so these are never both waiting anyway.
	for (const FBackendJournalEntry& Entry : Client->GetJournal())
	{
		if (Entry.State != EBackendQuestState::ReadyToTurnIn)
		{
			continue;
		}

		Client->TurnInQuest(CharacterId, Entry.QuestKey);

		ShowNotice(FString::Printf(TEXT("Handed in %s"), *Entry.Name), true);

		return;
	}

	const TArray<FBackendAvailableQuest>& Available = Client->GetAvailableQuests();

	if (Available.Num() == 0)
	{
		// Names the quest already running, because that is almost always why there is nothing on
		// offer: the chain hands out one at a time, so having no next quest means holding the
		// current one.
		const FString Refusal = AcceptRefusal(Client->GetJournal());

		ShowNotice(Refusal.IsEmpty() ? TEXT("Nothing to accept") : Refusal, false);

		return;
	}

	// The first one, which for an ordered chain is the next link. A picker belongs with a real
	// journal screen; this is enough to walk the onboarding line, which is what it is for.
	Client->AcceptQuest(CharacterId, Available[0].QuestKey);
}

void ASpaceMMOPlayerController::SellToFaction()
{
	USpaceMMOBackendClient* Client = Backend();

	if (Client == nullptr || CharacterId == 0)
	{
		return;
	}

	// The first stack anything buys, sorted the same way the panel lists them, so what the key does
	// matches what the player is reading. A selection cursor would be better and belongs with a real
	// inventory screen rather than a debug panel.
	TArray<FBackendInventoryItem> Held = Client->GetInventory();

	Held.Sort([](const FBackendInventoryItem& A, const FBackendInventoryItem& B)
		{ return A.Name < B.Name; });

	for (const FBackendInventoryItem& Item : Held)
	{
		if (Item.FactionBuyPriceMinorUnits <= 0 || Item.Quantity <= 0)
		{
			continue;
		}

		// A fixed parcel rather than the whole stack. This exists to get a stranded player moving
		// again, and one keypress that empties a hangar into the worst price in the game is a
		// mistake nobody would make deliberately.
		Client->SellToFaction(
			CharacterId, DockedStationId(), Item.ItemDefId,
			FMath::Min(Item.Quantity, FactionSaleParcel));

		return;
	}

	ShowNotice(TEXT("Nothing here a faction buys"), false);
}

void ASpaceMMOPlayerController::PollServerState()
{
	USpaceMMOBackendClient* Client = Backend();

	if (Client == nullptr || CharacterId == 0 || !Client->IsSignedIn())
	{
		return;
	}

	// A job countdown finishing.
	Client->FetchJobs(CharacterId);

	// Everything a fill changes. When somebody else's order crosses ours the credits, the goods and
	// the book all move on the server without this client doing anything, so a refresh driven only
	// by local action shows a seller their own sale minutes late — whenever they next happen to
	// press something.
	//
	// Quests are left out: they advance only from this character's own gathering and crafting, both
	// of which already refresh on the way through. Skills come along with the inventory because they
	// share a call, not because anything here expects them to move.
	Client->FetchCharacters();
	Client->SelectCharacter(CharacterId);

	// Where the player is docked. Polled rather than assumed, because the server undocks a ship
	// that flies out of range without this client being told, and every market key depends on it.
	Client->FetchDockedStation(CharacterId);

	RefreshBook();
}

void ASpaceMMOPlayerController::HandleIndustryChanged()
{
	// Nothing to do but let the next frame draw. The panel reads current state rather than caching
	// its own copy, which is what keeps it from ever showing something the backend has replaced.
}

void ASpaceMMOPlayerController::HandleIndustryMessage(
	const FString& Message, const bool bSucceeded)
{
	ShowNotice(Message, bSucceeded);
}

void ASpaceMMOPlayerController::ShowTransientLine(const FString& Line)
{
	UE_LOG(LogSpaceMMOBackend, Log, TEXT("%s"), *Line);

	// Its own on-screen message again, which it could not be before.
	//
	// This was folded into the character panel because on-screen messages are ordered by slot rather
	// than by key, and that panel was redrawn every frame at a zero display time -- so it took
	// whatever slot the free list handed back, and a separate three-second message landed in an
	// order nothing could influence, usually below dozens of panel lines and off the bottom of the
	// screen. The panel is gone, so nothing competes for slots and a plain message is stable again.
	//
	// Only reached when no transient-message Widget Blueprint is configured. It has no colour and no
	// position, but a gather result is the only feedback a key press gives, and losing it entirely
	// would be worse.
	if (GEngine != nullptr)
	{
		GEngine->AddOnScreenDebugMessage(TransientMessageKey, 4.0f, FColor::White, Line);
	}
}

void ASpaceMMOPlayerController::ShowNotice(const FString& Message, const bool bSucceeded)
{
	UE_LOG(LogSpaceMMOBackend, Log, TEXT("%s"), *Message);

	if (GEngine != nullptr)
	{
		GEngine->AddOnScreenDebugMessage(
			NoticeMessageKey, 4.0f, bSucceeded ? FColor::Green : FColor::Orange, Message);
	}
}

void ASpaceMMOPlayerController::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (IsLocalController())
	{
		UpdateHudContext();
	}
}

void ASpaceMMOPlayerController::UpdateHudContext()
{
	const bool bFlying = Cast<ASpaceMMOShipPawn>(GetPawn()) != nullptr;

	// HitTestInvisible rather than Visible: a readout that swallowed clicks would make the world
	// behind it unclickable, and nothing here is meant to be pressed.
	//
	// Only assigned on a change. Slate compares and skips an identical value, but saying so here
	// stops the next reader wondering whether this costs a frame.
	// HitTestInvisible for anything a player only reads: a readout that swallowed clicks would make
	// the world behind it unclickable. Visible for anything they act on — and note the enum means
	// "not hit-testable, self *and all children*", so a screen marked HitTestInvisible silently
	// stops every row inside it receiving a mouse event. That is exactly how the inventory screen
	// came to be undraggable while looking perfectly normal.
	auto Show = [](
		UUserWidget* Widget,
		const bool bWanted,
		const ESlateVisibility WhenShown = ESlateVisibility::HitTestInvisible)
	{
		if (Widget == nullptr)
		{
			return;
		}

		const ESlateVisibility Wanted = bWanted ? WhenShown : ESlateVisibility::Collapsed;

		if (Widget->GetVisibility() != Wanted)
		{
			Widget->SetVisibility(Wanted);
		}
	};

	// Nothing else while somebody is still signing in. A HUD over a world they have no character for
	// is a set of readouts about nobody, and the ship behind it is one they cannot be flying.
	//
	// Visible rather than SelfHitTestInvisible, unlike the paired panels: this one is meant to
	// swallow clicks, because it is the only thing on screen that should be reachable.
	Show(LoginScreen, bAwaitingSignIn, ESlateVisibility::Visible);

	if (bAwaitingSignIn)
	{
		Show(FlightReadout, false);
		Show(OnFootReadout, false);
		Show(Crosshair, false);
		Show(DepositPrompt, false);
		Show(SkillsScreen, false);
		Show(InventoryScreen, false);
		Show(StationOverlay, false);

		return;
	}

	// Exactly one of these two, never both: they share a corner deliberately, because a pilot and
	// somebody on foot are never the same moment.
	Show(FlightReadout, bFlying);
	Show(OnFootReadout, !bFlying);

	// Both pawns, and gone whenever a screen is open: a reticle over an inventory is pointing at a
	// list. The widget fades itself while the camera is swung, which is a different question -- that
	// one is about whether the view still means anything, and this one is about whether the player
	// is looking at the world at all.
	Show(Crosshair, !bSkillsScreenOpen && !bInventoryScreenOpen && !bStationOverlayOpen);

	// Gathering happens on foot — the component lives on the character pawn, and a ship has nothing
	// to pick up with — so the prompt has nothing to say in flight whatever is beneath the ship.
	Show(DepositPrompt, !bFlying);

	// Skills are global, so K works in the air as well as on the ground.
	Show(SkillsScreen, bSkillsScreenOpen);

	// So is what you own, and where it is is the whole point -- so I works in flight too, which is
	// where a hauler most wants to know what is still sitting in a hangar.
	//
	// SelfHitTestInvisible, which is the third thing and the one that is actually wanted here: rows
	// inside it still take clicks and drags, but the screen itself does not.
	//
	// Visible looks equivalent and is not. Both screens fill the viewport, so the one added last --
	// this one -- is on top of the whole window, including the half where it draws nothing, and a
	// hit-testable root there consumes every click meant for the station overlay underneath. The
	// symptom is precise and misleading: the market panel is visible, the cursor is over it, and
	// nothing responds, while the same panel works perfectly when opened alone.
	Show(InventoryScreen, bInventoryScreenOpen, ESlateVisibility::SelfHitTestInvisible);

	// Open together, they take a side each and stop covering one another. That pairing is the whole
	// point while transferring: goods move between a hangar and a hold, and reading one with the
	// other hidden is what makes hauling feel like paperwork. Alone, each returns to the middle.
	const bool bPaired = bStationOverlayOpen && bInventoryScreenOpen;

	if (StationOverlay != nullptr)
	{
		StationOverlay->SetSide(bPaired ? ESpaceMMOPanelSide::Left : ESpaceMMOPanelSide::Centre);
	}

	if (InventoryScreen != nullptr)
	{
		InventoryScreen->SetSide(bPaired ? ESpaceMMOPanelSide::Right : ESpaceMMOPanelSide::Centre);
	}

	// Undocking closes the station overlay rather than leaving a station's market floating over open
	// space — and it opens on docking, so arriving somewhere shows you where you have arrived.
	const int32 Station = DockedStationId();

	if (Station == 0)
	{
		bStationOverlayOpen = false;
	}
	else if (Station != LastDockedStationId)
	{
		bStationOverlayOpen = true;
	}

	LastDockedStationId = Station;

	// SelfHitTestInvisible for the same reason as the inventory screen: rows, tabs, a search box and
	// two order buttons are all clicked, but the empty half of a full-screen root must not be.
	Show(StationOverlay, bStationOverlayOpen, ESlateVisibility::SelfHitTestInvisible);
}

void ASpaceMMOPlayerController::GetStationPanels(
	FString& OutStationName,
	TArray<FString>& OutIndustry,
	TArray<FString>& OutQuests) const
{
	const USpaceMMOBackendClient* Client = Backend();

	if (Client == nullptr)
	{
		return;
	}

	// Not named StationId: this controller already has a member by that name, and shadowing it is a
	// warning this project treats as an error.
	const int32 Docked = DockedStationId();

	for (const FBackendStation& Station : Client->GetStations())
	{
		if (Station.Id == Docked)
		{
			OutStationName = Station.Name;

			break;
		}
	}

	OutIndustry = BuildIndustryPanel(
		Client->GetRecipes(), Client->GetJobs(), Client->GetInventory(), SelectedRecipeIndex);

	OutQuests = BuildQuestPanel(Client->GetJournal(), Client->GetAvailableQuests());
}

void ASpaceMMOPlayerController::ToggleStationOverlay()
{
	// Nothing when not docked. The overlay is about a place, and a refusal on every stray keypress
	// would get old faster than the information is worth.
	if (StationOverlay == nullptr || (!bStationOverlayOpen && DockedStationId() == 0))
	{
		return;
	}

	bStationOverlayOpen = !bStationOverlayOpen;

	// The same reason the inventory screen does it: rows are clicked, prices are typed and buttons
	// are pressed here, none of which a captured cursor can reach.
	ApplyMouseCapture();

	UpdateHudContext();
}

void ASpaceMMOPlayerController::ShowMarketTab()
{
	if (StationOverlay != nullptr && bStationOverlayOpen)
	{
		StationOverlay->SetTab(ESpaceMMOStationTab::Market);
	}
}

void ASpaceMMOPlayerController::ShowIndustryTab()
{
	if (StationOverlay != nullptr && bStationOverlayOpen)
	{
		StationOverlay->SetTab(ESpaceMMOStationTab::Industry);
	}
}

void ASpaceMMOPlayerController::ShowQuestsTab()
{
	if (StationOverlay != nullptr && bStationOverlayOpen)
	{
		StationOverlay->SetTab(ESpaceMMOStationTab::Quests);
	}
}

void ASpaceMMOPlayerController::CaptureDirection()
{
	const UWorld* const World = GetWorld();

	const APawn* const Possessed = GetPawn();

	if (World == nullptr || Possessed == nullptr)
	{
		return;
	}

	// Both pawns keep a system position and neither shares a base that exposes it, so this asks
	// each in turn rather than inventing a common interface for one caller.
	FSystemCoordinate Where;

	if (const ASpaceMMOShipPawn* const Ship = Cast<ASpaceMMOShipPawn>(Possessed))
	{
		Where = Ship->GetSystemPosition();
	}
	else if (const ASpaceMMOCharacterPawn* const OnFoot =
		Cast<ASpaceMMOCharacterPawn>(Possessed))
	{
		Where = OnFoot->GetSystemPosition();
	}
	else
	{
		ShowTransientMessage(
			TEXT("Nothing to take a bearing from"), ESpaceMMOMessageTone::Warning);

		return;
	}

	// The nearest body, because a direction only means anything relative to one -- and standing on
	// a planet, the one underfoot is the one being authored against.
	const ASpaceMMOPlanetActor* Nearest = nullptr;
	double NearestDistance = TNumericLimits<double>::Max();

	for (TActorIterator<ASpaceMMOPlanetActor> It(World); It; ++It)
	{
		const ASpaceMMOPlanetActor* const Planet = *It;

		if (Planet == nullptr)
		{
			continue;
		}

		const double Distance =
			(Where.Kilometres - Planet->GetPlanetConfig().Centre.Kilometres).Size();

		if (Distance < NearestDistance)
		{
			NearestDistance = Distance;
			Nearest = Planet;
		}
	}

	if (Nearest == nullptr)
	{
		ShowTransientMessage(TEXT("No body to take a bearing from"), ESpaceMMOMessageTone::Warning);

		return;
	}

	const FPlanetConfig Planet = Nearest->GetPlanetConfig();

	const FVector Direction =
		(Where.Kilometres - Planet.Centre.Kilometres).GetSafeNormal();

	if (Direction.IsNearlyZero())
	{
		// Standing exactly at a body's centre names no point on its surface, which is the same
		// reason a deposit authored with a zero direction is dropped rather than placed.
		ShowTransientMessage(
			TEXT("At the centre of the body; no direction to give"), ESpaceMMOMessageTone::Warning);

		return;
	}

	// Printed as the array content actually uses, so it is a copy rather than a transcription.
	// Six decimals is about a metre on a 20 km body and well past that on a real one.
	const FString Line = FString::Printf(
		TEXT("\"direction\": [%.6f, %.6f, %.6f]"), Direction.X, Direction.Y, Direction.Z);

	// The body key is named alongside, because a direction means nothing without knowing which world
	// it is a direction on -- and it is the key rather than a display name because the key is what
	// the content file wants next to it.
	UE_LOG(LogSpaceMMOBackend, Log,
		TEXT("Bearing on '%s' (%.3f km from its centre, %.0f m above nominal): %s"),
		*Nearest->BodyKey,
		NearestDistance,
		(NearestDistance - Planet.RadiusKilometres) * 1000.0,
		*Line);

	// And on screen, so somebody standing in the right place knows the key did something without
	// alt-tabbing to a log to find out.
	ShowTransientMessage(
		FString::Printf(TEXT("Bearing taken: %s"), *Line), ESpaceMMOMessageTone::Positive);
}

void ASpaceMMOPlayerController::ShowMyOrdersTab()
{
	if (StationOverlay != nullptr && bStationOverlayOpen)
	{
		StationOverlay->SetTab(ESpaceMMOStationTab::MyOrders);
	}
}

void ASpaceMMOPlayerController::ShowShipsTab()
{
	if (StationOverlay != nullptr && bStationOverlayOpen)
	{
		StationOverlay->SetTab(ESpaceMMOStationTab::Ships);
	}
}

void ASpaceMMOPlayerController::ToggleInventoryScreen()
{
	if (InventoryScreen == nullptr)
	{
		return;
	}

	bInventoryScreenOpen = !bInventoryScreenOpen;

	// Dragging needs a cursor, and the game holds the mouse by default. ApplyMouseCapture reads the
	// screen flags, so this neither takes nor restores the preference -- somebody who pressed M for
	// their own reasons still has the cursor after closing this, and somebody who did not gets the
	// game back.
	ApplyMouseCapture();

	// Asked for on opening rather than polled. What a player owns changes on the server -- a job
	// claimed, a sale settled, another character hauling -- and a screen opened to work out where
	// something went is the worst possible moment to be showing a stale copy.
	if (bInventoryScreenOpen)
	{
		RefreshCharacterState();
	}

	UpdateHudContext();
}

void ASpaceMMOPlayerController::ToggleSkillsScreen()
{
	bSkillsScreenOpen = !bSkillsScreenOpen;

	// Applied immediately rather than waiting for the next tick, so the screen answers the keypress
	// in the frame it was pressed.
	UpdateHudContext();
}

void ASpaceMMOPlayerController::OnRep_CharacterId()
{
	RefreshPossessedPawn();

	RefreshCharacterState();
}

void ASpaceMMOPlayerController::RefreshCharacterState()
{
	if (CharacterId == 0 || !IsLocalController())
	{
		return;
	}

	USpaceMMOBackendClient* Client = Backend();

	if (Client == nullptr || !Client->IsSignedIn())
	{
		return;
	}

	Client->SelectCharacter(CharacterId);

	// The balance lives on the character list, which is otherwise only read once at sign-in. Without
	// this it would be correct exactly until the first job fee and wrong from then on -- and a wrong
	// number displayed confidently is worse than no number, because the refusal that eventually
	// follows makes no sense next to it.
	Client->FetchCharacters();

	// Quests advance as a consequence of gathering and crafting, both of which route through here,
	// so refreshing alongside skills and inventory keeps the journal honest without a poll of its
	// own. A step that had quietly advanced and a panel that still showed the old count would be
	// the same class of bug as the frozen balance.
	Client->FetchQuests(CharacterId);

	// The book too, since a fill by somebody else changes it without this client doing anything.
	RefreshBook();

	if (!bIndustryBound)
	{
		bIndustryBound = true;

		Client->OnIndustryChanged.AddDynamic(
			this, &ASpaceMMOPlayerController::HandleIndustryChanged);
		Client->OnIndustryMessage.AddDynamic(
			this, &ASpaceMMOPlayerController::HandleIndustryMessage);

		// Bound beside the industry delegates and guarded by the same flag, because identity can
		// resolve more than once and a second binding would say everything twice.
		Client->OnShipSummoned.AddDynamic(
			this, &ASpaceMMOPlayerController::HandleShipSummoned);

		Client->FetchRecipes();

		// Polled, because nothing pushes a countdown and nothing pushes another player's trade.
		// Two seconds is far coarser than the display, which is deliberate: the panel renders the
		// server's last answer plus nothing, so a stale second is honest where a locally-decremented
		// one would eventually be wrong.
		//
		// A poll standing in for a push, and it should become one when there is a real UI — four
		// small reads per client every two seconds does not survive a populated station.
		GetWorldTimerManager().SetTimer(
			StateRefreshTimer, this, &ASpaceMMOPlayerController::PollServerState, 2.0f, true);
	}

	Client->FetchJobs(CharacterId);
}

FString ASpaceMMOPlayerController::AcceptRefusal(const TArray<FBackendJournalEntry>& Journal)
{
	for (const FBackendJournalEntry& Entry : Journal)
	{
		if (Entry.State == EBackendQuestState::Completed
			|| Entry.State == EBackendQuestState::Abandoned)
		{
			continue;
		}

		if (Entry.State == EBackendQuestState::ReadyToTurnIn)
		{
			return FString::Printf(TEXT("%s is finished - hand it in"), *Entry.Name);
		}

		// The authored step, because it is the thing to go and do. "Salvage Rights is already
		// active" on its own says why the key did nothing without saying what would move it.
		if (!Entry.StepDescription.IsEmpty())
		{
			return FString::Printf(
				TEXT("%s is already active - %s"), *Entry.Name, *Entry.StepDescription);
		}

		return FString::Printf(TEXT("%s is already active"), *Entry.Name);
	}

	return FString();
}

TArray<FString> ASpaceMMOPlayerController::BuildQuestPanel(
	const TArray<FBackendJournalEntry>& Journal,
	const TArray<FBackendAvailableQuest>& Available)
{
	TArray<FString> Lines;

	// The hint only when the key does something. Advertising it with nothing on offer is how a
	// quest already running came to read as one waiting to be accepted -- the player pressed the
	// key the header named, got "Nothing to accept", and concluded the system was broken.
	// Names what the key will actually do. One key doing the obvious next thing is only obvious
	// while the panel says which thing that is -- and handing in comes first, because a player
	// standing on finished work wants paying before they are offered more of it.
	const FBackendJournalEntry* Ready = Journal.FindByPredicate(
		[](const FBackendJournalEntry& Entry)
		{ return Entry.State == EBackendQuestState::ReadyToTurnIn; });

	if (Ready != nullptr)
	{
		Lines.Add(FString::Printf(TEXT("-- Quests --  J hands in %s"), *Ready->Name));
	}
	else
	{
		Lines.Add(Available.Num() > 0
			? TEXT("-- Quests --  J accepts the next one")
			: TEXT("-- Quests --"));
	}

	bool bAnyActive = false;

	for (const FBackendJournalEntry& Entry : Journal)
	{
		if (Entry.State == EBackendQuestState::Completed
			|| Entry.State == EBackendQuestState::Abandoned)
		{
			// Finished quests are history. A journal that lists everything ever done buries the one
			// line saying what to do next, which is the only line being looked for.
			continue;
		}

		// Headed, and only once there is something under it. Without this an active quest and an
		// offered one are two identical lines, and the whole difference between them -- that one is
		// yours and the other is not -- is left for the player to infer.
		if (!bAnyActive)
		{
			Lines.Add(TEXT("  ACTIVE"));
		}

		bAnyActive = true;

		if (Entry.State == EBackendQuestState::ReadyToTurnIn)
		{
			Lines.Add(FString::Printf(TEXT("   %s  READY TO HAND IN"), *Entry.Name));

			continue;
		}

		Lines.Add(FString::Printf(
			TEXT("   %s  %d/%d"), *Entry.Name, Entry.StepProgress, Entry.StepRequired));

		if (!Entry.StepDescription.IsEmpty())
		{
			Lines.Add(FString::Printf(TEXT("      %s"), *Entry.StepDescription));
		}
	}

	if (!bAnyActive)
	{
		Lines.Add(TEXT("   none active"));
	}

	// Only worth naming when there is something to take. A permanent empty heading is noise on a
	// display that has to be readable at a glance.
	if (Available.Num() > 0)
	{
		Lines.Add(TEXT("  AVAILABLE"));

		Lines.Add(FString::Printf(TEXT("   %s"), *Available[0].Name));

		if (Available.Num() > 1)
		{
			Lines.Add(FString::Printf(TEXT("   ... and %d more"), Available.Num() - 1));
		}
	}

	return Lines;
}

TArray<FString> ASpaceMMOPlayerController::BuildIndustryPanel(
	const TArray<FBackendRecipe>& Recipes,
	const TArray<FBackendIndustryJob>& Jobs,
	const TArray<FBackendInventoryItem>& Inventory,
	const int32 SelectedIndex)
{
	TArray<FString> Lines;

	Lines.Add(TEXT("-- Industry --  R select  X start  Z claim"));

	if (Recipes.Num() == 0)
	{
		Lines.Add(TEXT("   no recipes loaded"));
	}

	// Clamped rather than trusted. The catalog can be re-fetched at any time, and a selection left
	// pointing past the end would read as "nothing is selected" while the start key silently did
	// nothing.
	const int32 Selected = Recipes.Num() > 0
		? FMath::Clamp(SelectedIndex, 0, Recipes.Num() - 1)
		: INDEX_NONE;

	for (int32 Index = 0; Index < Recipes.Num(); ++Index)
	{
		const FBackendRecipe& Recipe = Recipes[Index];

		Lines.Add(FString::Printf(
			TEXT(" %s %s x%d  %ds  %s %d"),
			Index == Selected ? TEXT(">") : TEXT(" "),
			*Recipe.OutputName,
			Recipe.OutputQuantity,
			Recipe.JobSeconds,
			*Recipe.SkillName,
			Recipe.RequiredLevel));

		// Materials only for the selected recipe. Listing every input of every recipe would be a
		// wall of text on a display that has to be read at a glance.
		if (Index != Selected)
		{
			continue;
		}

		if (!Recipe.RequiredToolName.IsEmpty())
		{
			Lines.Add(FString::Printf(TEXT("      tool: %s"), *Recipe.RequiredToolName));
		}

		for (const FBackendRecipeInput& Input : Recipe.Inputs)
		{
			int32 Held = 0;

			for (const FBackendInventoryItem& Item : Inventory)
			{
				if (Item.ItemKey == Input.ItemKey)
				{
					Held += Item.Quantity;
				}
			}

			// Two numbers the server already sent, shown side by side. Deliberately not turned into
			// a verdict: deciding "you cannot build this" here would be a second copy of the gates.
			Lines.Add(FString::Printf(
				TEXT("      %s  %d/%d"), *Input.Name, Held, Input.Quantity));
		}
	}

	Lines.Add(TEXT("-- Jobs --"));

	if (Jobs.Num() == 0)
	{
		Lines.Add(TEXT("   none running"));
	}

	for (const FBackendIndustryJob& Job : Jobs)
	{
		Lines.Add(FString::Printf(
			TEXT("   %s x%d  %s"),
			*Job.OutputName,
			Job.OutputQuantityTotal,
			Job.bIsClaimable
				? TEXT("READY")
				: *FString::Printf(TEXT("%ds"), Job.SecondsRemaining)));
	}

	return Lines;
}

FString ASpaceMMOPlayerController::GetCharacterBalance() const
{
	const USpaceMMOBackendClient* Client = Backend();

	if (Client == nullptr)
	{
		return FString();
	}

	// Not named Character: AController already has a member by that name, and shadowing it is a
	// warning this project treats as an error.
	for (const FBackendCharacter& Owned : Client->GetCharacters())
	{
		if (Owned.Id == CharacterId)
		{
			return Owned.FormatBalance();
		}
	}

	// Empty until the character list has been read. A confident zero is indistinguishable from being
	// broke, and the two want different reactions from whoever is reading it.
	return FString();
}

FString ASpaceMMOPlayerController::GroupDigits(const int64 Value)
{
	const FString Digits = FString::Printf(TEXT("%lld"), FMath::Abs(Value));

	FString Grouped;

	for (int32 Index = 0; Index < Digits.Len(); ++Index)
	{
		// Counted from the right, so the leading group is the short one: 1234567 groups as
		// 1,234,567 rather than 123,456,7.
		if (Index > 0 && (Digits.Len() - Index) % 3 == 0)
		{
			Grouped.AppendChar(TEXT(','));
		}

		Grouped.AppendChar(Digits[Index]);
	}

	// XP is never negative today, but a formatter that silently drops a sign is a formatter that
	// lies the first time it is reused for a balance or a delta.
	return Value < 0 ? TEXT("-") + Grouped : Grouped;
}

void ASpaceMMOPlayerController::BeginIdentifying()
{
	const UWorld* World = GetWorld();

	const UGameInstance* GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;

	USpaceMMOBackendClient* Backend =
		GameInstance != nullptr
			? GameInstance->GetSubsystem<USpaceMMOBackendClient>()
			: nullptr;

	if (Backend == nullptr)
	{
		return;
	}

	FParse::Value(FCommandLine::Get(), TEXT("CharacterId="), DesiredCharacterId);

	// Already signed in — a level transition, say — so there is nothing to wait for.
	if (Backend->IsSignedIn())
	{
		PresentCredentials();

		return;
	}

	// Bound before deciding how the session will arrive, because every route ends in the same three
	// answers and the two paths had already drifted: the login screen subscribed to the session and
	// not to the character list, so signing in worked and then nothing followed it -- no identity,
	// no inventory, no skills, and no error to say why.
	Backend->OnSessionChanged.AddDynamic(this, &ASpaceMMOPlayerController::HandleSessionChanged);
	Backend->OnCharactersLoaded.AddDynamic(this, &ASpaceMMOPlayerController::HandleCharactersLoaded);
	Backend->OnFailed.AddDynamic(this, &ASpaceMMOPlayerController::HandleBackendFailed);

	FString Email;
	FString Password;

	// The credentials file wins over everything, including a remembered session. It exists so that
	// two clients on one desktop can be two different players, and a remembered token would quietly
	// make both of them whoever signed in last -- which is the one thing that arrangement is for.
	const bool bHaveCredentials = FindCredentials(Email, Password);

	if (!bHaveCredentials && Backend->RestoreRememberedSession())
	{
		PresentCredentials();

		return;
	}

	if (!bHaveCredentials)
	{
		// A screen to ask on, or nothing to ask with. Without one this connection simply has no
		// identity and every action that needs one says so where it is needed -- which is the state
		// the automated runs and any machine without a Widget Blueprint are in.
		if (LoginScreen != nullptr)
		{
			bAwaitingSignIn = true;

			// Released, and the world hidden behind the screen. A captured cursor cannot reach a
			// text box, and a sign-in nobody can type into is worse than no screen at all.
			ApplyMouseCapture();
			UpdateHudContext();

			UE_LOG(LogSpaceMMOBackend, Log, TEXT("No credentials found; asking for a sign-in."));

			return;
		}

		UE_LOG(LogSpaceMMOBackend, Log,
			TEXT("No credentials found and no login screen configured; this connection will have "
				 "no character."));

		return;
	}

	// The email is logged and the password never is. A mangled address is the single most likely
	// reason a login fails here, and it is invisible unless the value actually used is shown —
	// which cost a debugging round when "joe@gmail.com" arrived as "joe@gmail".
	UE_LOG(LogSpaceMMOBackend, Log, TEXT("Signing in as %s."), *Email);

	Backend->LogIn(Email, Password);
}

bool ASpaceMMOPlayerController::FindCredentials(FString& OutEmail, FString& OutPassword)
{
	const bool bFromCommandLine =
		FParse::Value(FCommandLine::Get(), TEXT("BackendEmail="), OutEmail)
		&& FParse::Value(FCommandLine::Get(), TEXT("BackendPassword="), OutPassword);

	if (bFromCommandLine)
	{
		return true;
	}

	// Overridable so two clients on one machine can be two different players — which is exactly
	// what testing a server needs, and impossible when both read the same file.
	FString Path;

	if (!FParse::Value(FCommandLine::Get(), TEXT("BackendLoginFile="), Path) || Path.IsEmpty())
	{
		Path = FPaths::Combine(
			FPaths::ProjectDir(), TEXT(".."), TEXT("secrets"), TEXT("player-login.txt"));
	}

	TArray<FString> Lines;

	if (!FFileHelper::LoadFileToStringArray(Lines, *Path) || Lines.Num() < 2)
	{
		return false;
	}

	OutEmail = Lines[0].TrimStartAndEnd();
	OutPassword = Lines[1].TrimStartAndEnd();

	return !OutEmail.IsEmpty() && !OutPassword.IsEmpty();
}

void ASpaceMMOPlayerController::HandleBackendFailed(const FBackendFailure& Failure)
{
	// After sign-in, failures belong to whatever the player just did — a refused craft, most often —
	// and they are the only explanation available. Until this was here, every post-login failure was
	// discarded, so pressing a key with too little ore produced no message, no log line, and nothing
	// on screen: identical to the key not being bound.
	if (bPresented)
	{
		ShowNotice(
			Failure.Message.IsEmpty()
				? FString::Printf(TEXT("Refused (%d)"), Failure.HttpStatus)
				: Failure.Message,
			false);

		return;
	}

	UE_LOG(LogSpaceMMOBackend, Warning,
		TEXT("Sign-in failed (%d): %s. This connection will have no character, so gathering will "
			 "credit nobody. Check the address above is the one you registered, and that the "
			 "account exists."),
		Failure.HttpStatus,
		*Failure.Message);
}

void ASpaceMMOPlayerController::HandleSessionChanged(const bool bIsSignedIn)
{
	if (!bIsSignedIn)
	{
		return;
	}

	// Whichever way the session arrived -- typed, restored, or from the credentials file -- the
	// screen has done its job and the world comes back.
	if (bAwaitingSignIn)
	{
		bAwaitingSignIn = false;

		ApplyMouseCapture();
		UpdateHudContext();
	}

	const UWorld* World = GetWorld();

	const UGameInstance* GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;

	USpaceMMOBackendClient* Backend =
		GameInstance != nullptr
			? GameInstance->GetSubsystem<USpaceMMOBackendClient>()
			: nullptr;

	if (Backend == nullptr)
	{
		return;
	}

	// A named character still has to be confirmed as one this account owns, and the list is how the
	// client discovers that. It is not a security check — the server repeats it — but sending a
	// claim the account plainly cannot back is a guaranteed refusal.
	Backend->FetchCharacters();
}

void ASpaceMMOPlayerController::HandleCharactersLoaded()
{
	PresentCredentials();
}

void ASpaceMMOPlayerController::PresentCredentials()
{
	if (bPresented)
	{
		return;
	}

	const UWorld* World = GetWorld();

	const UGameInstance* GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;

	const USpaceMMOBackendClient* Backend =
		GameInstance != nullptr
			? GameInstance->GetSubsystem<USpaceMMOBackendClient>()
			: nullptr;

	if (Backend == nullptr || !Backend->IsSignedIn())
	{
		return;
	}

	int32 Claimed = DesiredCharacterId;

	if (Claimed == 0)
	{
		const TArray<FBackendCharacter>& Characters = Backend->GetCharacters();

		if (Characters.Num() == 0)
		{
			UE_LOG(LogSpaceMMOBackend, Warning,
				TEXT("Signed in but this account has no characters; nothing to play as."));

			return;
		}

		Claimed = Characters[0].Id;
	}

	bPresented = true;

	UE_LOG(LogSpaceMMOBackend, Log, TEXT("Claiming character %d."), Claimed);

	ServerIdentify(Backend->GetSessionToken(), Claimed);
}

void ASpaceMMOPlayerController::ServerIdentify_Implementation(
	const FString& Token, const int32 ClaimedCharacterId)
{
	const UWorld* World = GetWorld();

	const UGameInstance* GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;

	USpaceMMOBackendClient* Backend =
		GameInstance != nullptr
			? GameInstance->GetSubsystem<USpaceMMOBackendClient>()
			: nullptr;

	if (Backend == nullptr)
	{
		return;
	}

	// Nothing about the claim is trusted here. The backend decides, using the token as proof, and
	// the id the client sent is only the question being asked.
	TWeakObjectPtr<ASpaceMMOPlayerController> WeakThis(this);

	Backend->ResolveCharacterAsServer(
		Token,
		ClaimedCharacterId,
		USpaceMMOBackendClient::FOnCharacterResolved::CreateLambda(
			[WeakThis, ClaimedCharacterId](const FBackendResolvedCharacter& Resolved)
			{
				ASpaceMMOPlayerController* Controller = WeakThis.Get();

				if (Controller == nullptr)
				{
					return;
				}

				if (Resolved.CharacterId == 0)
				{
					// Logged, and the connection simply stays anonymous. Kicking would be the
					// harsher option and is worth considering once there is a login screen to send
					// somebody back to.
					UE_LOG(LogSpaceMMOBackend, Warning,
						TEXT("Refused claim on character %d: the token does not entitle it."),
						ClaimedCharacterId);

					return;
				}

				Controller->AdoptIdentity(Resolved);

				UE_LOG(LogSpaceMMOBackend, Log,
					TEXT("Connection identified as character %d (%s) on account %d."),
					Resolved.CharacterId, *Resolved.CharacterName, Resolved.AccountId);
			}));
}

void ASpaceMMOPlayerController::AdoptIdentity(const FBackendResolvedCharacter& Resolved)
{
	CharacterId = Resolved.CharacterId;
	CharacterName = Resolved.CharacterName;

	// Where they were left. Handed to the docking component below, which is the thing that can put
	// the ship back there -- see USpaceMMODockingComponent::ResumeDockedAt and task 114.
	ResumeAtStationId = Resolved.DockedStationId;

	// And where they were standing or flying, which is the finer-grained version of the same fact
	// (task 147). The flag rather than a zero test: the origin is a real place, and a character who
	// has never been anywhere is not at it.
	bHasResumePosition = Resolved.bHasLastPosition;
	ResumePositionKilometres = Resolved.LastPositionKilometres;
	bResumeFlying = Resolved.bLastSeenFlying;

	// Nothing to put back, so nothing is waiting: the spawn is where they belong, and the periodic
	// write may start recording it. Without this a brand new character would never be recorded at
	// all, because the guard below would stay shut for the whole session.
	if (!bHasResumePosition)
	{
		bPlacedForThisSession = true;
	}

	// Server-side only. A dedicated server runs this for every connection; a listen or standalone
	// server runs it for its own. A client has no service credential and would be refused anyway.
	if (HasAuthority() && CharacterId != 0)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(
				WhereaboutsTimer,
				this,
				&ASpaceMMOPlayerController::RecordWhereabouts,
				WhereaboutsIntervalSeconds,
				true);
		}
	}

	// A ship summoned in a previous session is still parked where it was left, and nothing has put
	// a pawn there since the world restarted. Asked once identity exists, which is the first moment
	// there is anybody to ask about.
	EnsureActiveShipInWorld();

	RefreshPossessedPawn();

	// Also here, not only in OnRep_CharacterId. A replication callback does not fire on the machine
	// that owns the property, so in standalone play -- where the controller is its own authority --
	// OnRep never runs and the panel would sit empty forever. On a dedicated server this is a no-op,
	// because the connection's controller is not local there.
	RefreshCharacterState();
}

void ASpaceMMOPlayerController::ReportBoarding()
{
	if (!HasAuthority() || CharacterId == 0)
	{
		return;
	}

	const ASpaceMMOShipPawn* Ship = Cast<ASpaceMMOShipPawn>(GetPawn());

	// Zero for on foot, and zero for the unowned prop the game mode still spawns. Deliberately the
	// same answer: sitting in a ship nobody owns is not sitting in your ship, and it must not open
	// the hold of a hull parked at a station on the other side of the system.
	const int64 Aboard = Ship != nullptr ? Ship->HullItemInstanceId : 0;

	if (Aboard == ReportedAboardHullId)
	{
		return;
	}

	ReportedAboardHullId = Aboard;

	USpaceMMOBackendClient* Client = Backend();

	if (Client == nullptr)
	{
		return;
	}

	if (Aboard > 0)
	{
		Client->BoardAsServer(CharacterId, Aboard);

		UE_LOG(LogSpaceMMOBackend, Log,
			TEXT("Character %d is aboard hull %lld; its hold travels with them."),
			CharacterId, Aboard);

		return;
	}

	Client->DisembarkAsServer(CharacterId);

	UE_LOG(LogSpaceMMOBackend, Log,
		TEXT("Character %d is no longer in a ship of their own."), CharacterId);
}

void ASpaceMMOPlayerController::ServerShipSummoned_Implementation()
{
	// Carries nothing. Which hull was summoned, and whether it really was, are the backend's to
	// say -- this is a nudge telling the server the answer has changed, not a claim about what it
	// changed to.
	EnsureActiveShipInWorld();
}

void ASpaceMMOPlayerController::HandleShipSummoned(const FString& ShipName)
{
	ShowTransientMessage(
		FString::Printf(
			TEXT("%s summoned. It is waiting outside."),
			ShipName.IsEmpty() ? TEXT("Your ship") : *ShipName),
		ESpaceMMOMessageTone::Positive);

	// The world half. The client cannot spawn anything anybody else would see, so it asks the
	// server to look again at what this character owns.
	ServerShipSummoned();
}

void ASpaceMMOPlayerController::EnsureActiveShipInWorld()
{
	if (!HasAuthority() || CharacterId == 0)
	{
		return;
	}

	USpaceMMOBackendClient* Client = Backend();

	if (Client == nullptr)
	{
		return;
	}

	TWeakObjectPtr<ASpaceMMOPlayerController> WeakThis(this);

	Client->FetchActiveShipAsServer(
		CharacterId,
		USpaceMMOBackendClient::FOnActiveShipResolved::CreateLambda(
			[WeakThis](const FBackendActiveShip& Ship)
			{
				if (ASpaceMMOPlayerController* Controller = WeakThis.Get())
				{
					Controller->PlaceSummonedShip(Ship);
				}
			}));
}

void ASpaceMMOPlayerController::PlaceSummonedShip(const FBackendActiveShip& Ship)
{
	UWorld* World = GetWorld();

	if (!HasAuthority() || World == nullptr || Ship.HullItemInstanceId <= 0)
	{
		return;
	}

	// Not parked at a station at all, which is what being flown looks like from the database: the
	// hull instance is in a hold rather than a hangar. Nothing to place -- either somebody is
	// already in it, or task 147 is about to put them back in it.
	if (Ship.StationId == 0)
	{
		return;
	}

	// Already there. Summoning twice, or summoning and then signing in beside it, both arrive here
	// and neither should make a second copy of one ship.
	for (TActorIterator<ASpaceMMOShipPawn> It(World); It; ++It)
	{
		if (It->HullItemInstanceId == Ship.HullItemInstanceId)
		{
			UE_LOG(LogSpaceMMOBackend, Log,
				TEXT("%s (hull %lld) is already in the world; not spawning another."),
				*Ship.Name, Ship.HullItemInstanceId);

			return;
		}
	}

	const ASpaceMMOStationActor* Station = nullptr;

	for (TActorIterator<ASpaceMMOStationActor> It(World); It; ++It)
	{
		if (It->GetStation().Id == Ship.StationId)
		{
			Station = *It;

			break;
		}
	}

	// The station has not been built in the world yet. Stations arrive from the backend a moment
	// after a connection does, so this is an ordinary race rather than a fault -- signing in asks
	// again, and so does the next summon.
	if (Station == nullptr)
	{
		UE_LOG(LogSpaceMMOBackend, Log,
			TEXT("Station %d is not in the world yet; %s stays in its hangar for now."),
			Ship.StationId, *Ship.Name);

		return;
	}

	FSystemCoordinate Parking;

	if (!ParkingPositionBeside(
		World,
		*Station,
		SummonedShipOffsetKilometres,
		SummonedShipLiftKilometres,
		Parking))
	{
		UE_LOG(LogSpaceMMOBackend, Warning,
			TEXT("No planet under station %d, so %s has no ground to sit on."),
			Ship.StationId, *Ship.Name);

		return;
	}

	ASpaceMMOShipPawn* Waiting = World->SpawnActorDeferred<ASpaceMMOShipPawn>(
		ASpaceMMOShipPawn::StaticClass(),
		FTransform::Identity,
		nullptr,
		nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

	if (Waiting == nullptr)
	{
		return;
	}

	// Before FinishSpawning, like every other placed spawn: BeginPlay resolves the surface, so a
	// position applied afterwards is a frame too late.
	Waiting->SetStartingSystemPosition(Parking.Kilometres);
	Waiting->HullItemInstanceId = Ship.HullItemInstanceId;
	Waiting->FinishSpawning(FTransform::Identity);

	UE_LOG(LogSpaceMMOBackend, Log,
		TEXT("%s (hull %lld) is waiting %.0f m from station %d, at %s."),
		*Ship.Name,
		Ship.HullItemInstanceId,
		SummonedShipOffsetKilometres * 1000.0,
		Ship.StationId,
		*Parking.ToString());
}

void ASpaceMMOPlayerController::RestoreWhereabouts()
{
	// Server-side, like docking's resume and for the same reason: the client's copy has no
	// authority over where anything is, and moving a pawn there would be corrected by replication
	// a frame later.
	if (!HasAuthority() || bPlacedForThisSession || !bHasResumePosition)
	{
		return;
	}

	APawn* Possessed = GetPawn();
	UWorld* World = GetWorld();

	// Identity arrived first. Possession calls back into here, and that pass does the work.
	if (Possessed == nullptr || World == nullptr)
	{
		return;
	}

	// Set before anything can fail, not after. Every branch below is a decision about where this
	// player belongs, and retrying one that went wrong would mean moving somebody who has since
	// walked away from wherever they were put.
	bPlacedForThisSession = true;

	const FSystemCoordinate Where(ResumePositionKilometres);

	if (!bResumeFlying)
	{
		if (ASpaceMMOCharacterPawn* OnFoot = Cast<ASpaceMMOCharacterPawn>(Possessed))
		{
			OnFoot->ResumeAt(Where);
		}

		return;
	}

	// Flying. A ship pawn at the recorded position, possessed, and the character pawn destroyed --
	// which is exactly the swap boarding performs, run in reverse of stepping out.
	//
	// The hull is a plain ship pawn rather than one built from this character's
	// ActiveShipItemInstanceId, because nothing yet builds a pawn from an owned hull: that is the
	// unfinished half of task 115. When it lands, this is the call site that changes, and a
	// character whose hull has since been sold or destroyed should wake on foot rather than in a
	// ship that does not exist.
	ASpaceMMOShipPawn* Ship = World->SpawnActorDeferred<ASpaceMMOShipPawn>(
		ASpaceMMOShipPawn::StaticClass(),
		FTransform::Identity,
		nullptr,
		nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

	if (Ship == nullptr)
	{
		UE_LOG(LogSpaceMMOBackend, Warning,
			TEXT("Character %d was flying at %s and no ship could be spawned; left on foot."),
			CharacterId, *Where.ToString());

		return;
	}

	// Before FinishSpawning, like every other placed spawn here: BeginPlay resolves the surface,
	// so a position applied afterwards is a frame too late and the first frame is spent elsewhere.
	Ship->SetStartingSystemPosition(Where.Kilometres);
	Ship->FinishSpawning(FTransform::Identity);

	Possess(Ship);

	// After possession has moved on, never before -- destroying first leaves the controller
	// briefly possessing nothing, and anything running in that window has no pawn to ask.
	if (Possessed != nullptr)
	{
		Possessed->Destroy();
	}

	UE_LOG(LogSpaceMMOBackend, Log,
		TEXT("Character %d quit while flying and resumes flying, at %s."),
		CharacterId, *Where.ToString());
}

void ASpaceMMOPlayerController::RecordWhereabouts()
{
	// Nothing to say until this connection has been put where it belongs. Recording before then
	// would write the spawn position over the very one waiting to be restored, and a player whose
	// machine died during sign-in would come back to the starting point permanently.
	if (!HasAuthority() || CharacterId == 0 || !bPlacedForThisSession)
	{
		return;
	}

	USpaceMMOBackendClient* Client = Backend();

	if (Client == nullptr)
	{
		return;
	}

	// Read off the pawn rather than passed in. What is being recorded is where the simulation has
	// put somebody, and asking anything else for a position is asking somebody's opinion of it.
	//
	// The pawn's class is also the answer to what they were doing, which is why the flag needs no
	// separate bookkeeping and cannot drift: a player in a ship pawn is flying, by construction.
	const APawn* Possessed = GetPawn();

	// Said out loud, because this is the branch that would make the whole feature quietly stop
	// working: no pawn means no position, and a write that does not happen looks exactly like a
	// player who has not moved since the last one that did.
	if (Possessed == nullptr)
	{
		UE_LOG(LogSpaceMMOBackend, Log,
			TEXT("Character %d has no pawn to read a position from; leaving the last recorded one."),
			CharacterId);

		return;
	}

	if (const ASpaceMMOShipPawn* Ship = Cast<ASpaceMMOShipPawn>(Possessed))
	{
		Client->RecordWhereaboutsAsServer(
			CharacterId, Ship->GetSystemPosition().Kilometres, true);

		return;
	}

	if (const ASpaceMMOCharacterPawn* OnFoot = Cast<ASpaceMMOCharacterPawn>(Possessed))
	{
		Client->RecordWhereaboutsAsServer(
			CharacterId, OnFoot->GetSystemPosition().Kilometres, false);
	}
}

void ASpaceMMOPlayerController::Destroyed()
{
	// Before Super, and that is the whole point of overriding this rather than EndPlay.
	// APlayerController::Destroyed unpossesses or destroys the pawn and only then calls up to
	// AActor::Destroyed, which is what routes EndPlay -- so the position has to be taken here,
	// while there is still a pawn to take it from.
	RecordWhereabouts();

	Super::Destroyed();
}

void ASpaceMMOPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Server shutdown, which destroys nothing: EndPlay is routed across every actor and the pawns
	// are still standing where they were. A disconnect has already been handled in Destroyed, and
	// recording the same position twice costs one request and changes nothing.
	RecordWhereabouts();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(WhereaboutsTimer);
	}

	Super::EndPlay(EndPlayReason);
}

void ASpaceMMOPlayerController::RefreshPossessedPawn()
{
	// First, because restoring somebody who was flying replaces the pawn everything below is about
	// to be pushed onto. Possessing the ship calls straight back into here, so the work is not
	// skipped -- it is done once, on the pawn the player is actually going to be in.
	RestoreWhereabouts();

	// What they are sitting in, which decides whether a hold opens (ADR-0012 point 4). Here rather
	// than in the boarding code because every route into and out of a ship comes through
	// possession, including being restored into one on sign-in.
	ReportBoarding();

	// Identity can arrive before or after a pawn — the backend round trip races possession — so
	// both orders have to work. This handles "identity last"; the component asks the controller
	// when it is spawned, which handles "identity first".
	if (APawn* Possessed = GetPawn())
	{
		if (USpaceMMOGatheringComponent* Gathering =
			Possessed->FindComponentByClass<USpaceMMOGatheringComponent>())
		{
			Gathering->CharacterId = CharacterId;

			// Logged because this is the last link in the chain and the only one that was
			// previously invisible: identity could resolve correctly and still fail to reach the
			// thing that spends it, and the symptom would be ore credited to nobody. With two
			// players on a server it also shows, at a glance, that each pawn got its own.
			if (CharacterId != 0)
			{
				UE_LOG(LogSpaceMMOBackend, Log, TEXT("%s will gather as character %d (%s)."),
					*GetNameSafe(Possessed), CharacterId, *CharacterName);
			}
		}

		// The same for docking, which was left out of this and did not work for it.
		//
		// The component is created when the pawn spawns, and reads identity from the controller
		// then — which is usually before the backend has said who this connection is, so it holds
		// zero. ServerToggleDock refuses a character of zero and returns, logging but showing the
		// player nothing at all: a key that appears dead rather than one that says why.
		if (USpaceMMODockingComponent* Docking =
			Possessed->FindComponentByClass<USpaceMMODockingComponent>())
		{
			Docking->CharacterId = CharacterId;

			// Where the backend says this character was left, which is not where the pawn spawned.
			// The component puts the ship there once the station exists in the world.
			if (ResumeAtStationId != 0)
			{
				Docking->ResumeDockedAt(ResumeAtStationId);
			}

			// And bind here too. On a client the component arrives by replication, and its own
			// BeginPlay can run before the pawn has an input component to bind to.
			Docking->BindInput(Possessed->InputComponent);

			if (CharacterId != 0)
			{
				UE_LOG(LogSpaceMMOBackend, Log, TEXT("%s will dock as character %d."),
					*GetNameSafe(Possessed), CharacterId);
			}
		}
	}
}
