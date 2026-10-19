#include "SpaceMMOSessionEndedScreen.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Kismet/KismetSystemLibrary.h"
#include "SpaceMMOBackendLog.h"
#include "SpaceMMOPanelRow.h"
#include "SpaceMMOStyle.h"

FString USpaceMMOSessionEndedScreen::TitleFor(const ESpaceMMOSessionEnd Reason)
{
	// Capitals come from the title's style, as every panel's do.
	return Reason == ESpaceMMOSessionEnd::SignedInElsewhere
		? FString(TEXT("Signed in elsewhere"))
		: FString(TEXT("Disconnected"));
}

FString USpaceMMOSessionEndedScreen::NoteFor(const ESpaceMMOSessionEnd Reason)
{
	return Reason == ESpaceMMOSessionEnd::SignedInElsewhere
		? FString(TEXT("This account was signed in from another window or computer, so this one has been "
					   "disconnected."))
		: FString(TEXT("The connection to the server was lost."));
}

void USpaceMMOSessionEndedScreen::NativeConstruct()
{
	Super::NativeConstruct();

	using namespace SpaceMMO;

	PanelLook::Apply(TitleText, Style::ETextRole::Title);
	PanelLook::Apply(NoteText, Style::ETextRole::Note);

	if (QuitButton != nullptr)
	{
		QuitButton->OnClicked.AddUniqueDynamic(this, &USpaceMMOSessionEndedScreen::HandleQuitClicked);
	}
}

void USpaceMMOSessionEndedScreen::Show(const ESpaceMMOSessionEnd Reason)
{
	if (TitleText != nullptr)
	{
		TitleText->SetText(FText::FromString(TitleFor(Reason)));
	}

	if (NoteText != nullptr)
	{
		NoteText->SetText(FText::FromString(NoteFor(Reason)));
	}
}

void USpaceMMOSessionEndedScreen::HandleQuitClicked()
{
	UE_LOG(LogSpaceMMOBackend, Log, TEXT("Session over: quitting."));

	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
}
