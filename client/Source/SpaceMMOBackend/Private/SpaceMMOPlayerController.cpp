#include "SpaceMMOPlayerController.h"

#include "Components/InputComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"
#include "SpaceMMOBackendClient.h"
#include "SpaceMMOBackendLog.h"
#include "SpaceMMOBackendProtocol.h"
#include "SpaceMMOGatheringComponent.h"

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
		BeginIdentifying();

		ApplyMouseCapture();
	}
}

void ASpaceMMOPlayerController::ApplyMouseCapture()
{
	if (!IsLocalController())
	{
		return;
	}

	if (bMouseCaptured)
	{
		// Set in code as well as in DefaultInput.ini. The ini values are defaults for a viewport,
		// and anything that changes input mode later -- a menu, a level transition, the editor's
		// own play-in-window handling -- leaves them behind. Asserting it here means the game window
		// owns the mouse whenever this controller is the one being played.
		SetInputMode(FInputModeGameOnly());

		bShowMouseCursor = false;

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
			TEXT("ToggleCharacterPanel"),
			IE_Pressed,
			this,
			&ASpaceMMOPlayerController::ToggleCharacterPanel);

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
			TEXT("CycleHolding"), IE_Pressed, this, &ASpaceMMOPlayerController::CycleHolding);

		InputComponent->BindAction(
			TEXT("ListForSale"), IE_Pressed, this, &ASpaceMMOPlayerController::ListSelectedForSale);

		InputComponent->BindAction(
			TEXT("BuyFromMarket"), IE_Pressed, this, &ASpaceMMOPlayerController::BuyBestAsk);

		InputComponent->BindAction(
			TEXT("ToggleMouseCapture"),
			IE_Pressed,
			this,
			&ASpaceMMOPlayerController::ToggleMouseCapture);
	}
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
	Client->StartJob(CharacterId, Available[SelectedRecipeIndex].Id, StationId, 1);
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

TArray<FBackendInventoryItem> ASpaceMMOPlayerController::SellableHoldings() const
{
	const USpaceMMOBackendClient* Client = Backend();

	if (Client == nullptr)
	{
		return TArray<FBackendInventoryItem>();
	}

	// Station hangars only. An order is placed against goods at a station, so cargo riding along in
	// a ship's hold cannot back one — and offering to sell it would produce a refusal the player
	// could not act on.
	TArray<FBackendInventoryItem> Sellable = Client->GetInventory().FilterByPredicate(
		[](const FBackendInventoryItem& Item)
		{ return Item.Kind == StationHangarKind && Item.Quantity > 0; });

	Sellable.Sort([](const FBackendInventoryItem& A, const FBackendInventoryItem& B)
		{ return A.Name < B.Name; });

	return Sellable;
}

bool ASpaceMMOPlayerController::TryGetSelectedHolding(FBackendInventoryItem& OutItem) const
{
	const TArray<FBackendInventoryItem> Sellable = SellableHoldings();

	if (Sellable.Num() == 0)
	{
		return false;
	}

	OutItem = Sellable[FMath::Clamp(SelectedHoldingIndex, 0, Sellable.Num() - 1)];

	return true;
}

int64 ASpaceMMOPlayerController::ListingPriceFor(const FBackendInventoryItem& Item)
{
	// Ten times what a faction pays, or ten credits for something no faction buys.
	//
	// A placeholder for a price box, and deliberately well clear of the faction floor: the floor
	// exists to be the worst deal available, so a listing at or near it would teach a player that
	// trading with other people is not worth the trouble.
	return Item.FactionBuyPriceMinorUnits > 0
		? Item.FactionBuyPriceMinorUnits * 10
		: 1000;
}

void ASpaceMMOPlayerController::CycleHolding()
{
	const int32 Count = SellableHoldings().Num();

	if (Count == 0)
	{
		return;
	}

	SelectedHoldingIndex = (SelectedHoldingIndex + 1) % Count;

	RefreshBook();
}

void ASpaceMMOPlayerController::RefreshBook()
{
	USpaceMMOBackendClient* Client = Backend();

	FBackendInventoryItem Selected;

	if (Client != nullptr && TryGetSelectedHolding(Selected))
	{
		Client->FetchBook(StationId, Selected.ItemDefId);
	}
}

void ASpaceMMOPlayerController::ListSelectedForSale()
{
	USpaceMMOBackendClient* Client = Backend();

	FBackendInventoryItem Selected;

	if (Client == nullptr || CharacterId == 0 || !TryGetSelectedHolding(Selected))
	{
		ShowNotice(TEXT("Nothing at this station to sell"), false);

		return;
	}

	Client->PlaceOrder(
		CharacterId,
		StationId,
		Selected.ItemDefId,
		EBackendOrderSide::Sell,
		ListingPriceFor(Selected),
		FMath::Min(Selected.Quantity, MarketParcel));
}

void ASpaceMMOPlayerController::BuyBestAsk()
{
	USpaceMMOBackendClient* Client = Backend();

	FBackendInventoryItem Selected;

	if (Client == nullptr || CharacterId == 0 || !TryGetSelectedHolding(Selected))
	{
		return;
	}

	// The book on screen might be another item's if a request is still in flight. Buying against
	// the wrong one would spend real credits on something the player never looked at.
	if (Client->GetBookItemDefId() != Selected.ItemDefId)
	{
		ShowNotice(TEXT("Book still loading"), false);

		return;
	}

	const FBackendBookEntry* Best = nullptr;

	for (const FBackendBookEntry& Entry : Client->GetBook())
	{
		if (Entry.Side != EBackendOrderSide::Sell || Entry.QuantityRemaining <= 0)
		{
			continue;
		}

		if (Best == nullptr || Entry.PriceMinorUnits < Best->PriceMinorUnits)
		{
			Best = &Entry;
		}
	}

	if (Best == nullptr)
	{
		ShowNotice(TEXT("Nothing on sale here"), false);

		return;
	}

	// A limit at the best ask rather than a market order. The engine crosses it immediately against
	// that ask, and if somebody else takes it first this rests on the book instead of chasing the
	// price upward — which is what a market order would do and is never what anybody meant.
	Client->PlaceOrder(
		CharacterId,
		StationId,
		Selected.ItemDefId,
		EBackendOrderSide::Buy,
		Best->PriceMinorUnits,
		FMath::Min(Best->QuantityRemaining, MarketParcel));
}

void ASpaceMMOPlayerController::AcceptNextQuest()
{
	USpaceMMOBackendClient* Client = Backend();

	if (Client == nullptr || CharacterId == 0)
	{
		return;
	}

	const TArray<FBackendAvailableQuest>& Available = Client->GetAvailableQuests();

	if (Available.Num() == 0)
	{
		ShowNotice(TEXT("Nothing to accept"), false);

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
			CharacterId, StationId, Item.ItemDefId, FMath::Min(Item.Quantity, FactionSaleParcel));

		return;
	}

	ShowNotice(TEXT("Nothing here a faction buys"), false);
}

void ASpaceMMOPlayerController::RefreshJobs()
{
	USpaceMMOBackendClient* Client = Backend();

	if (Client != nullptr && CharacterId != 0 && Client->IsSignedIn())
	{
		Client->FetchJobs(CharacterId);
	}
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

void ASpaceMMOPlayerController::ShowNotice(const FString& Message, const bool bSucceeded)
{
	UE_LOG(LogSpaceMMOBackend, Log, TEXT("%s"), *Message);

	if (GEngine != nullptr)
	{
		GEngine->AddOnScreenDebugMessage(
			NoticeMessageKey, 4.0f, bSucceeded ? FColor::Green : FColor::Orange, Message);
	}
}

void ASpaceMMOPlayerController::ToggleCharacterPanel()
{
	bShowCharacterPanel = !bShowCharacterPanel;

	if (!bShowCharacterPanel && GEngine != nullptr)
	{
		GEngine->RemoveOnScreenDebugMessage(PanelMessageKey);
	}
}

void ASpaceMMOPlayerController::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bShowCharacterPanel && IsLocalController())
	{
		DrawCharacterPanel();
	}
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

		Client->FetchRecipes();

		// Polled, because nothing pushes a countdown. Two seconds is far coarser than the display,
		// which is deliberate: the panel renders the server's last answer plus nothing, so a stale
		// second is honest where a locally-decremented one would eventually be wrong.
		GetWorldTimerManager().SetTimer(
			JobRefreshTimer, this, &ASpaceMMOPlayerController::RefreshJobs, 2.0f, true);
	}

	Client->FetchJobs(CharacterId);
}

void ASpaceMMOPlayerController::DrawCharacterPanel()
{
	if (GEngine == nullptr)
	{
		return;
	}

	const USpaceMMOBackendClient* Client = Backend();

	if (Client == nullptr)
	{
		return;
	}

	// Empty until the character list has been read, and the panel says so rather than printing a
	// confident zero -- which is indistinguishable from being broke.
	FString Balance;

	// Not named Character: AController already has a member by that name, and shadowing it is a
	// warning this project treats as an error.
	for (const FBackendCharacter& Owned : Client->GetCharacters())
	{
		if (Owned.Id == CharacterId)
		{
			Balance = Owned.FormatBalance();

			break;
		}
	}

	TArray<FString> Lines = BuildCharacterPanel(
		CharacterName, Balance, Client->GetSkills(), Client->GetInventory());

	Lines.Append(BuildQuestPanel(Client->GetJournal(), Client->GetAvailableQuests()));

	FBackendInventoryItem Selling;

	Lines.Append(TryGetSelectedHolding(Selling)
		? BuildMarketPanel(Selling.Name, Client->GetBook(), ListingPriceFor(Selling))
		: BuildMarketPanel(FString(), TArray<FBackendBookEntry>(), 0));

	Lines.Append(BuildIndustryPanel(
		Client->GetRecipes(), Client->GetJobs(), Client->GetInventory(), SelectedRecipeIndex));

	// Says so rather than silently dropping the tail. A panel that quietly stops listing at forty
	// rows would read as "I do not own that", which is the one thing an inventory display must never
	// get wrong.
	if (Lines.Num() > PanelMaxLines)
	{
		const int32 Hidden = Lines.Num() - PanelMaxLines + 1;

		Lines.SetNum(PanelMaxLines);
		Lines[PanelMaxLines - 1] = FString::Printf(TEXT("   ... and %d more"), Hidden);
	}

	// One message carrying every line, not one message per line.
	//
	// UEngine::DrawOnscreenDebugMessages walks its message map with a plain TMap iterator, so the
	// order on screen is slot order rather than key order. That would be survivable if slots were
	// stable, but these are drawn with a zero display time, which means the engine deletes every one
	// of them at the end of each frame and the next frame re-adds them into whatever slots the free
	// list hands back. The result is an order nothing here can influence -- the ship's own readouts
	// use keys 1, 3 and 2 and render as 2, 3, 1.
	//
	// Joining the lines makes the whole panel a single entry, so its internal order is simply string
	// order and cannot be shuffled. It also removes the need to clear unused rows.
	GEngine->AddOnScreenDebugMessage(
		PanelMessageKey, 0.0f, FColor::White, FString::Join(Lines, TEXT("\n")));
}

TArray<FString> ASpaceMMOPlayerController::BuildMarketPanel(
	const FString& ItemName,
	const TArray<FBackendBookEntry>& Book,
	const int64 ListingPriceMinorUnits)
{
	TArray<FString> Lines;

	Lines.Add(TEXT("-- Market --  H item  N list 10  B buy"));

	if (ItemName.IsEmpty())
	{
		Lines.Add(TEXT("   nothing here a station can sell"));

		return Lines;
	}

	Lines.Add(FString::Printf(
		TEXT("   %s   N lists @ %s cr"),
		*ItemName,
		*FSpaceMMOBackendProtocol::FormatCredits(ListingPriceMinorUnits)));

	// Asks ascending, so the first is what a buyer would pay; bids descending, so the first is what
	// a seller would get. Showing them in book order instead would put the least relevant price at
	// the top of each side, which is the wrong way round for someone deciding whether to trade.
	TArray<FBackendBookEntry> Asks = Book.FilterByPredicate(
		[](const FBackendBookEntry& E) { return E.Side == EBackendOrderSide::Sell; });

	TArray<FBackendBookEntry> Bids = Book.FilterByPredicate(
		[](const FBackendBookEntry& E) { return E.Side == EBackendOrderSide::Buy; });

	Asks.Sort([](const FBackendBookEntry& A, const FBackendBookEntry& B)
		{ return A.PriceMinorUnits < B.PriceMinorUnits; });

	Bids.Sort([](const FBackendBookEntry& A, const FBackendBookEntry& B)
		{ return A.PriceMinorUnits > B.PriceMinorUnits; });

	auto AppendSide = [&Lines](const TCHAR* Label, const TArray<FBackendBookEntry>& Side)
	{
		if (Side.Num() == 0)
		{
			Lines.Add(FString::Printf(TEXT("      %s: none"), Label));

			return;
		}

		FString Row;

		// Three deep. A debug panel that printed a whole book would push everything else off the
		// screen, and the prices that matter are the ones nearest the spread.
		for (int32 Index = 0; Index < FMath::Min(3, Side.Num()); ++Index)
		{
			Row += FString::Printf(
				TEXT("  %s x%d"),
				*FSpaceMMOBackendProtocol::FormatCredits(Side[Index].PriceMinorUnits),
				Side[Index].QuantityRemaining);
		}

		Lines.Add(FString::Printf(TEXT("      %s:%s"), Label, *Row));
	};

	AppendSide(TEXT("asks"), Asks);
	AppendSide(TEXT("bids"), Bids);

	return Lines;
}

TArray<FString> ASpaceMMOPlayerController::BuildQuestPanel(
	const TArray<FBackendJournalEntry>& Journal,
	const TArray<FBackendAvailableQuest>& Available)
{
	TArray<FString> Lines;

	Lines.Add(TEXT("-- Quests --  J accepts the next one"));

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
		Lines.Add(FString::Printf(TEXT("   available: %s"), *Available[0].Name));

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

TArray<FString> ASpaceMMOPlayerController::BuildCharacterPanel(
	const FString& CharacterName,
	const FString& Balance,
	const TArray<FBackendSkill>& Skills,
	const TArray<FBackendInventoryItem>& Inventory)
{
	TArray<FString> Lines;

	Lines.Add(CharacterName.IsEmpty()
		? TEXT("Not identified")
		: FString::Printf(TEXT("== %s =="), *CharacterName));

	// Only trained skills. A character has a row for every skill in the game from creation, and
	// listing thirty untouched zeroes would bury the one line that changed.
	TArray<FBackendSkill> Trained = Skills.FilterByPredicate(
		[](const FBackendSkill& Skill) { return Skill.Xp > 0; });

	// Sorted here rather than trusted from the response. JSON array order is whatever the query
	// returned, and a list that reorders itself between refreshes is unreadable precisely when it is
	// being watched -- which, for this panel, is always.
	Trained.Sort([](const FBackendSkill& A, const FBackendSkill& B) { return A.Name < B.Name; });

	Lines.Add(TEXT("-- Skills --"));

	if (Trained.Num() == 0)
	{
		Lines.Add(TEXT("   nothing trained yet"));
	}

	for (const FBackendSkill& Skill : Trained)
	{
		Lines.Add(FString::Printf(
			TEXT("   %s  lv %d  (%s xp)"),
			*Skill.Name,
			Skill.Level,
			*GroupDigits(Skill.Xp)));
	}

	TArray<FBackendInventoryItem> Held = Inventory;

	Held.Sort([](const FBackendInventoryItem& A, const FBackendInventoryItem& B)
		{ return A.Name < B.Name; });

	// "Holdings", not "Hold". The endpoint returns every stack the character owns across every
	// inventory -- ship holds and station hangars alike -- and gathered ore lands in a hangar. A
	// panel headed "Hold" would have a player looking in their cargo bay for ore that is on a
	// different planet.
	// Credits sit with the holdings rather than in the header, because that is where a player looks
	// when deciding whether they can afford to do the thing they are looking at.
	Lines.Add(Balance.IsEmpty()
		? TEXT("-- Holdings --")
		: FString::Printf(TEXT("-- Holdings --  %s cr"), *Balance));

	if (Held.Num() == 0)
	{
		Lines.Add(TEXT("   empty"));
	}

	for (const FBackendInventoryItem& Item : Held)
	{
		// The faction price is shown on the stack rather than hidden behind a menu, because the
		// moment it matters is the moment a player is broke and looking for anything they can turn
		// into credits. It is marked as a floor so nobody mistakes it for what the thing is worth.
		Lines.Add(Item.FactionBuyPriceMinorUnits > 0
			? FString::Printf(
				TEXT("   %s  x%d   V sells @ %s cr"),
				*Item.Name,
				Item.Quantity,
				*FSpaceMMOBackendProtocol::FormatCredits(Item.FactionBuyPriceMinorUnits))
			: FString::Printf(TEXT("   %s  x%d"), *Item.Name, Item.Quantity));
	}

	return Lines;
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

	FString Email;
	FString Password;

	if (!FindCredentials(Email, Password))
	{
		// Not an error yet. Without credentials this connection simply has no identity, and every
		// action that needs one says so at the point it is needed rather than here.
		UE_LOG(LogSpaceMMOBackend, Log,
			TEXT("No credentials found; this connection will have no character. "
				 "Write email and password on two lines in secrets\\player-login.txt."));

		return;
	}

	// The email is logged and the password never is. A mangled address is the single most likely
	// reason a login fails here, and it is invisible unless the value actually used is shown —
	// which cost a debugging round when "joe@gmail.com" arrived as "joe@gmail".
	UE_LOG(LogSpaceMMOBackend, Log, TEXT("Signing in as %s."), *Email);

	Backend->OnSessionChanged.AddDynamic(this, &ASpaceMMOPlayerController::HandleSessionChanged);
	Backend->OnCharactersLoaded.AddDynamic(this, &ASpaceMMOPlayerController::HandleCharactersLoaded);
	Backend->OnFailed.AddDynamic(this, &ASpaceMMOPlayerController::HandleBackendFailed);

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
			[WeakThis, ClaimedCharacterId](
				const int32 AccountId, const int32 ResolvedCharacterId, const FString& Name)
			{
				ASpaceMMOPlayerController* Controller = WeakThis.Get();

				if (Controller == nullptr)
				{
					return;
				}

				if (ResolvedCharacterId == 0)
				{
					// Logged, and the connection simply stays anonymous. Kicking would be the
					// harsher option and is worth considering once there is a login screen to send
					// somebody back to.
					UE_LOG(LogSpaceMMOBackend, Warning,
						TEXT("Refused claim on character %d: the token does not entitle it."),
						ClaimedCharacterId);

					return;
				}

				Controller->AdoptIdentity(ResolvedCharacterId, Name);

				UE_LOG(LogSpaceMMOBackend, Log,
					TEXT("Connection identified as character %d (%s) on account %d."),
					ResolvedCharacterId, *Name, AccountId);
			}));
}

void ASpaceMMOPlayerController::AdoptIdentity(
	const int32 ResolvedCharacterId, const FString& ResolvedName)
{
	CharacterId = ResolvedCharacterId;
	CharacterName = ResolvedName;

	RefreshPossessedPawn();

	// Also here, not only in OnRep_CharacterId. A replication callback does not fire on the machine
	// that owns the property, so in standalone play -- where the controller is its own authority --
	// OnRep never runs and the panel would sit empty forever. On a dedicated server this is a no-op,
	// because the connection's controller is not local there.
	RefreshCharacterState();
}

void ASpaceMMOPlayerController::RefreshPossessedPawn()
{
	// Identity can arrive before or after a pawn — the backend round trip races possession — so
	// both orders have to work. This handles "identity last"; the component asks the controller
	// when it is spawned, which handles "identity first".
	if (APawn* Possessed = GetPawn())
	{
		if (USpaceMMOGatheringComponent* Gathering =
			Possessed->FindComponentByClass<USpaceMMOGatheringComponent>())
		{
			Gathering->CharacterId = CharacterId;
			Gathering->StationId = StationId;

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
	}
}
