#include "SpaceMMOCharacterScreens.h"

#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "SpaceMMOBackendClient.h"
#include "SpaceMMOBackendLog.h"
#include "SpaceMMOPlayerController.h"

namespace SpaceMMOCharacterScreens
{
	void SetText(UTextBlock* Block, const FString& Value)
	{
		if (Block != nullptr)
		{
			Block->SetText(FText::FromString(Value));
		}
	}

	void ShowIf(UWidget* Widget, const bool bShown)
	{
		if (Widget != nullptr)
		{
			Widget->SetVisibility(bShown ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		}
	}

	USpaceMMOBackendClient* BackendFor(const UUserWidget* Widget)
	{
		const UGameInstance* GameInstance = Widget != nullptr ? Widget->GetGameInstance() : nullptr;

		return GameInstance != nullptr ? GameInstance->GetSubsystem<USpaceMMOBackendClient>() : nullptr;
	}

	ASpaceMMOPlayerController* ControllerFor(const UUserWidget* Widget)
	{
		return Widget != nullptr ? Cast<ASpaceMMOPlayerController>(Widget->GetOwningPlayer()) : nullptr;
	}

	/** Says once that a screen cannot list anything, and why. Same reasoning as the skills screen. */
	bool CheckWiring(
		const UPanelWidget* Panel,
		const UClass* RowClass,
		const TCHAR* Screen,
		const TCHAR* PanelName,
		const TCHAR* RowClassName,
		bool& bWarned)
	{
		if (Panel != nullptr && RowClass != nullptr)
		{
			return true;
		}

		if (!bWarned)
		{
			bWarned = true;

			TArray<FString> Missing;

			if (Panel == nullptr)
			{
				Missing.Add(FString::Printf(TEXT("no panel named '%s'"), PanelName));
			}

			if (RowClass == nullptr)
			{
				Missing.Add(FString::Printf(TEXT("no %s set"), RowClassName));
			}

			UE_LOG(LogSpaceMMOBackend, Warning,
				TEXT("Menus: the %s lists nothing -- %s. %s is bound by name; %s is set in Class "
					 "Defaults."),
				Screen,
				*FString::Join(Missing, TEXT(" and ")),
				PanelName,
				RowClassName);
		}

		return false;
	}
}

FSpaceMMOOpening FSpaceMMOOpening::Decide(
	const int32 DesiredCharacterId,
	const bool bSignedInFromCredentialsFile,
	const TArray<FBackendCharacter>& Characters,
	const bool bHaveSelectScreen,
	const bool bHaveNewCharacterScreen)
{
	FSpaceMMOOpening Opening;

	if (DesiredCharacterId != 0)
	{
		Opening.Kind = ESpaceMMOOpening::Claim;
		Opening.CharacterId = DesiredCharacterId;

		return Opening;
	}

	// The credentials file, and anything with no screen to choose on: the first character, which is
	// what the game did before there were menus.
	if (bSignedInFromCredentialsFile || !bHaveSelectScreen)
	{
		if (Characters.Num() > 0)
		{
			Opening.Kind = ESpaceMMOOpening::Claim;
			Opening.CharacterId = Characters[0].Id;

			return Opening;
		}

		// A file-signed account with none can still be given somewhere to make one, if there is
		// one. Nothing else would ever let that account play.
		Opening.Kind = bHaveNewCharacterScreen
			? ESpaceMMOOpening::CreateCharacter
			: ESpaceMMOOpening::NothingToPlay;

		return Opening;
	}

	// Straight to creation for an account with none, as agreed. Select would only be an empty list
	// with one useful button on it.
	if (Characters.Num() == 0 && bHaveNewCharacterScreen)
	{
		Opening.Kind = ESpaceMMOOpening::CreateCharacter;

		return Opening;
	}

	Opening.Kind = ESpaceMMOOpening::ChooseCharacter;

	return Opening;
}

// ---------------------------------------------------------------------------------------------------
// Character select

FString USpaceMMOCharacterSelectScreen::DescribeLastSeen(const FBackendCharacter& Character)
{
	if (Character.LastSeenWorld.IsEmpty())
	{
		return TEXT("Not played yet");
	}

	// The hull's name whenever there is one, flying or landed: somebody sitting in a parked Shuttle
	// is in the Shuttle. "In a ship" only for the unowned prop ship, which has no name to give.
	const FString Doing = !Character.LastSeenShip.IsEmpty()
		? FString::Printf(TEXT("in the %s"), *Character.LastSeenShip)
		: Character.bLastSeenFlying
			? FString(TEXT("in a ship"))
			: FString(TEXT("on foot"));

	return FString::Printf(TEXT("Last seen: %s, %s"), *Character.LastSeenWorld, *Doing);
}

FSpaceMMOCharacterRowText USpaceMMOCharacterSelectScreen::BuildRow(
	const FBackendCharacter& Character, const TArray<FBackendRace>& Races)
{
	FSpaceMMOCharacterRowText Row;
	Row.CharacterId = Character.Id;
	Row.Name = Character.Name;
	Row.Credits = Character.FormatBalance() + TEXT(" cr");
	Row.LastSeen = DescribeLastSeen(Character);

	// Names from the server's race list. Empty until it arrives, rather than the enum's spelling:
	// "SpaceElf · A" for a frame is a worse thing to have seen than nothing.
	for (const FBackendRace& Race : Races)
	{
		if (Race.Race == Character.Race)
		{
			Row.Lineage = FString::Printf(TEXT("%s · %s"), *Race.Name, *Race.FactionName);

			break;
		}
	}

	return Row;
}

void USpaceMMOCharacterRow::NativeConstruct()
{
	Super::NativeConstruct();

	if (SelectButton != nullptr)
	{
		SelectButton->OnClicked.AddUniqueDynamic(this, &USpaceMMOCharacterRow::HandleClicked);
	}
}

void USpaceMMOCharacterRow::SetRow(
	const FSpaceMMOCharacterRowText& Row, const bool bInSelected, USpaceMMOCharacterSelectScreen* InOwner)
{
	using namespace SpaceMMOCharacterScreens;

	CharacterId = Row.CharacterId;
	Owner = InOwner;
	bSelected = bInSelected;

	SetText(NameText, Row.Name);
	SetText(LineageText, Row.Lineage);
	SetText(CreditsText, Row.Credits);
	SetText(LastSeenText, Row.LastSeen);

	ShowIf(SelectionFrame, bSelected);
}

void USpaceMMOCharacterRow::HandleClicked()
{
	if (USpaceMMOCharacterSelectScreen* Screen = Owner.Get())
	{
		Screen->Select(CharacterId);
	}
}

void USpaceMMOCharacterSelectScreen::NativeConstruct()
{
	Super::NativeConstruct();

	// Focusable so the controller can hand it keyboard focus; a screen that cannot take focus cannot
	// be reached at all while the input mode is UI only.
	SetIsFocusable(true);

	if (PlayButton != nullptr)
	{
		PlayButton->OnClicked.AddUniqueDynamic(this, &USpaceMMOCharacterSelectScreen::Play);
	}

	if (NewCharacterButton != nullptr)
	{
		NewCharacterButton->OnClicked.AddUniqueDynamic(
			this, &USpaceMMOCharacterSelectScreen::HandleNewCharacter);
	}

	if (SignOutButton != nullptr)
	{
		SignOutButton->OnClicked.AddUniqueDynamic(this, &USpaceMMOCharacterSelectScreen::HandleSignOut);
	}
}

USpaceMMOBackendClient* USpaceMMOCharacterSelectScreen::Backend() const
{
	return SpaceMMOCharacterScreens::BackendFor(this);
}

void USpaceMMOCharacterSelectScreen::Open()
{
	// Forced, so a list that changed while the screen was away is redrawn rather than trusted.
	RowSignature.Reset();

	if (USpaceMMOBackendClient* Client = Backend())
	{
		if (Client->GetRaces().Num() == 0)
		{
			Client->FetchRaces();
		}

		// The first character, until somebody picks another: Play is then one press, which is what
		// the game did before this screen existed.
		const TArray<FBackendCharacter>& Characters = Client->GetCharacters();

		const bool bStillThere = Characters.ContainsByPredicate(
			[this](const FBackendCharacter& Character) { return Character.Id == SelectedCharacterId; });

		if (!bStillThere)
		{
			SelectedCharacterId = Characters.Num() > 0 ? Characters[0].Id : 0;
		}
	}
}

void USpaceMMOCharacterSelectScreen::Select(const int32 CharacterId)
{
	SelectedCharacterId = CharacterId;
}

void USpaceMMOCharacterSelectScreen::Play()
{
	if (SelectedCharacterId == 0)
	{
		return;
	}

	if (ASpaceMMOPlayerController* Controller = SpaceMMOCharacterScreens::ControllerFor(this))
	{
		Controller->PlayCharacter(SelectedCharacterId);
	}
}

void USpaceMMOCharacterSelectScreen::HandleNewCharacter()
{
	if (ASpaceMMOPlayerController* Controller = SpaceMMOCharacterScreens::ControllerFor(this))
	{
		Controller->ShowNewCharacter();
	}
}

void USpaceMMOCharacterSelectScreen::HandleSignOut()
{
	if (ASpaceMMOPlayerController* Controller = SpaceMMOCharacterScreens::ControllerFor(this))
	{
		Controller->SignOutAndReload();
	}
}

void USpaceMMOCharacterSelectScreen::NativeTick(const FGeometry& Geometry, const float DeltaSeconds)
{
	Super::NativeTick(Geometry, DeltaSeconds);

	if (PlayButton != nullptr)
	{
		PlayButton->SetIsEnabled(SelectedCharacterId != 0);
	}

	if (!SpaceMMOCharacterScreens::CheckWiring(
			CharacterRows, RowClass, TEXT("character select screen"), TEXT("CharacterRows"),
			TEXT("RowClass"), bWarnedAboutWiring))
	{
		return;
	}

	const USpaceMMOBackendClient* Client = Backend();

	if (Client == nullptr)
	{
		return;
	}

	TArray<FSpaceMMOCharacterRowText> Rows;

	for (const FBackendCharacter& Character : Client->GetCharacters())
	{
		Rows.Add(BuildRow(Character, Client->GetRaces()));
	}

	// Rebuilt only when what it says changes, the selection included.
	FString Signature = FString::FromInt(SelectedCharacterId);

	for (const FSpaceMMOCharacterRowText& Row : Rows)
	{
		Signature += TEXT("|") + Row.Name + Row.Lineage + Row.Credits + Row.LastSeen;
	}

	if (Signature == RowSignature)
	{
		return;
	}

	RowSignature = Signature;

	CharacterRows->ClearChildren();

	for (const FSpaceMMOCharacterRowText& Row : Rows)
	{
		USpaceMMOCharacterRow* Widget = CreateWidget<USpaceMMOCharacterRow>(GetOwningPlayer(), RowClass);

		if (Widget == nullptr)
		{
			continue;
		}

		CharacterRows->AddChild(Widget);

		Widget->SetRow(Row, Row.CharacterId == SelectedCharacterId, this);
	}
}

// ---------------------------------------------------------------------------------------------------
// New character

FSpaceMMORaceRowText USpaceMMONewCharacterScreen::BuildRaceRow(
	const FBackendRace& Race, const TArray<FBackendBody>& Bodies)
{
	FSpaceMMORaceRowText Row;
	Row.Race = Race.Race;
	Row.RaceName = Race.Name;
	Row.FactionName = Race.FactionName;

	// The key if the server has no name for it, which only happens with the body unseeded. An empty
	// "home world" would read as though the race had none.
	const FString World = !Race.HomeBodyName.IsEmpty() ? Race.HomeBodyName : Race.HomeBodyKey;

	Row.HomeWorld = FString::Printf(TEXT("home world %s"), *World);
	Row.Summary = FString::Printf(TEXT("%s — %s — %s"), *Row.RaceName, *Row.FactionName, *Row.HomeWorld);

	for (const FBackendBody& Body : Bodies)
	{
		if (Body.Key == Race.HomeBodyKey && Body.bHasAppearance)
		{
			Row.bHasPalette = true;
			Row.LowColour = Body.LowColour;
			Row.HighColour = Body.HighColour;
			Row.RockColour = Body.RockColour;

			break;
		}
	}

	return Row;
}

void USpaceMMORaceRow::NativeConstruct()
{
	Super::NativeConstruct();

	if (SelectButton != nullptr)
	{
		SelectButton->OnClicked.AddUniqueDynamic(this, &USpaceMMORaceRow::HandleClicked);
	}
}

void USpaceMMORaceRow::SetRow(
	const FSpaceMMORaceRowText& Row, const bool bInSelected, USpaceMMONewCharacterScreen* InOwner)
{
	using namespace SpaceMMOCharacterScreens;

	Race = Row.Race;
	Owner = InOwner;
	bSelected = bInSelected;

	SetText(SummaryText, Row.Summary);
	SetText(RaceText, Row.RaceName);
	SetText(FactionText, Row.FactionName);
	SetText(HomeWorldText, Row.HomeWorld);

	auto Tint = [&Row](UImage* Swatch, const FLinearColor& Colour)
	{
		if (Swatch == nullptr)
		{
			return;
		}

		// Hidden rather than black when the world is unpainted, which is a working state for content.
		Swatch->SetVisibility(Row.bHasPalette ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		Swatch->SetColorAndOpacity(Colour);
	};

	Tint(LowSwatch, Row.LowColour);
	Tint(HighSwatch, Row.HighColour);
	Tint(RockSwatch, Row.RockColour);

	ShowIf(SelectionFrame, bSelected);
}

void USpaceMMORaceRow::HandleClicked()
{
	if (USpaceMMONewCharacterScreen* Screen = Owner.Get())
	{
		Screen->SelectRace(Race);
	}
}

USpaceMMOBackendClient* USpaceMMONewCharacterScreen::Backend() const
{
	return SpaceMMOCharacterScreens::BackendFor(this);
}

void USpaceMMONewCharacterScreen::NativeConstruct()
{
	Super::NativeConstruct();

	SetIsFocusable(true);

	if (BackButton != nullptr)
	{
		BackButton->OnClicked.AddUniqueDynamic(this, &USpaceMMONewCharacterScreen::HandleBack);
	}

	if (CreateButton != nullptr)
	{
		CreateButton->OnClicked.AddUniqueDynamic(this, &USpaceMMONewCharacterScreen::Create);
	}

	if (USpaceMMOBackendClient* Client = Backend())
	{
		Client->OnCharacterCreated.AddUniqueDynamic(this, &USpaceMMONewCharacterScreen::HandleCreated);
		Client->OnCharacterRefused.AddUniqueDynamic(this, &USpaceMMONewCharacterScreen::HandleRefused);
	}
}

void USpaceMMONewCharacterScreen::NativeDestruct()
{
	if (USpaceMMOBackendClient* Client = Backend())
	{
		Client->OnCharacterCreated.RemoveDynamic(this, &USpaceMMONewCharacterScreen::HandleCreated);
		Client->OnCharacterRefused.RemoveDynamic(this, &USpaceMMONewCharacterScreen::HandleRefused);
	}

	Super::NativeDestruct();
}

void USpaceMMONewCharacterScreen::Open()
{
	FailureMessage.Reset();
	bCreating = false;
	RowSignature.Reset();

	if (NameBox != nullptr)
	{
		NameBox->SetText(FText::GetEmpty());
	}

	if (USpaceMMOBackendClient* Client = Backend())
	{
		if (Client->GetRaces().Num() == 0)
		{
			Client->FetchRaces();
		}

		// For the swatches. Normally already loaded by the world; asked for here because this screen
		// can be the first thing a new account sees.
		if (Client->GetBodies().Num() == 0)
		{
			Client->FetchBodies();
		}
	}
}

void USpaceMMONewCharacterScreen::SelectRace(const EBackendRace Race)
{
	SelectedRace = Race;
}

void USpaceMMONewCharacterScreen::Create()
{
	USpaceMMOBackendClient* Client = Backend();

	if (Client == nullptr || bCreating)
	{
		return;
	}

	// Sent as typed. The server trims and checks the length, and its refusal is what gets shown --
	// a second copy of the rule here would be a second answer to keep in step.
	const FString Name = NameBox != nullptr ? NameBox->GetText().ToString() : FString();

	FailureMessage.Reset();
	bCreating = true;

	UE_LOG(LogSpaceMMOBackend, Log,
		TEXT("Menus: creating '%s' as race %d."), *Name, static_cast<int32>(SelectedRace));

	Client->CreateCharacter(Name, SelectedRace);
}

void USpaceMMONewCharacterScreen::HandleBack()
{
	if (ASpaceMMOPlayerController* Controller = SpaceMMOCharacterScreens::ControllerFor(this))
	{
		Controller->ShowCharacterSelect();
	}
}

void USpaceMMONewCharacterScreen::HandleCreated(const int32 CharacterId)
{
	// Only the attempt this screen made. The delegate is the client's, and nothing else creates
	// characters today, but a screen that played whichever character anybody created would be wrong
	// the day something does.
	if (!bCreating)
	{
		return;
	}

	bCreating = false;

	UE_LOG(LogSpaceMMOBackend, Log, TEXT("Menus: created character %d; playing it."), CharacterId);

	// Straight into the game (Joe, 1 October).
	if (ASpaceMMOPlayerController* Controller = SpaceMMOCharacterScreens::ControllerFor(this))
	{
		if (CharacterId != 0)
		{
			Controller->PlayCharacter(CharacterId);
		}
	}
}

void USpaceMMONewCharacterScreen::HandleRefused(const FString& Message)
{
	if (!bCreating)
	{
		return;
	}

	bCreating = false;
	FailureMessage = Message;

	UE_LOG(LogSpaceMMOBackend, Log, TEXT("Menus: creation refused: %s"), *Message);
}

void USpaceMMONewCharacterScreen::NativeTick(const FGeometry& Geometry, const float DeltaSeconds)
{
	Super::NativeTick(Geometry, DeltaSeconds);

	SpaceMMOCharacterScreens::SetText(FailureText, FailureMessage);
	SpaceMMOCharacterScreens::ShowIf(FailureText, !FailureMessage.IsEmpty());

	if (CreateButton != nullptr)
	{
		CreateButton->SetIsEnabled(!bCreating);
	}

	if (!SpaceMMOCharacterScreens::CheckWiring(
			RaceRows, RaceRowClass, TEXT("new character screen"), TEXT("RaceRows"),
			TEXT("RaceRowClass"), bWarnedAboutWiring))
	{
		return;
	}

	const USpaceMMOBackendClient* Client = Backend();

	if (Client == nullptr)
	{
		return;
	}

	TArray<FSpaceMMORaceRowText> Rows;

	for (const FBackendRace& Race : Client->GetRaces())
	{
		Rows.Add(BuildRaceRow(Race, Client->GetBodies()));
	}

	FString Signature = FString::FromInt(static_cast<int32>(SelectedRace));

	for (const FSpaceMMORaceRowText& Row : Rows)
	{
		Signature += TEXT("|") + Row.Summary + (Row.bHasPalette ? Row.LowColour.ToString() : FString());
	}

	if (Signature == RowSignature)
	{
		return;
	}

	RowSignature = Signature;

	RaceRows->ClearChildren();

	for (const FSpaceMMORaceRowText& Row : Rows)
	{
		USpaceMMORaceRow* Widget = CreateWidget<USpaceMMORaceRow>(GetOwningPlayer(), RaceRowClass);

		if (Widget == nullptr)
		{
			continue;
		}

		RaceRows->AddChild(Widget);

		Widget->SetRow(Row, Row.Race == SelectedRace, this);
	}
}
