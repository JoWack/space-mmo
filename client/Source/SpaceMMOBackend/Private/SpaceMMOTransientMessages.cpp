#include "SpaceMMOTransientMessages.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "SpaceMMOBackendLog.h"
#include "SpaceMMOHudPlacement.h"

void USpaceMMOTransientMessageRow::SetMessage(const FSpaceMMOTransientMessage& Message)
{
	if (MessageText != nullptr)
	{
		MessageText->SetText(FText::FromString(Message.Text));
	}

	bIsPositive = Message.Tone == ESpaceMMOMessageTone::Positive;
}

void USpaceMMOTransientMessages::Push(const FString& Text, const ESpaceMMOMessageTone Tone)
{
	const UWorld* World = GetWorld();

	if (World == nullptr || Text.IsEmpty())
	{
		return;
	}

	FSpaceMMOTransientMessage Message;
	Message.Text = Text;
	Message.Tone = Tone;
	Message.ExpiresAt = World->GetTimeSeconds() + SecondsShown;

	Messages.Add(Message);

	// Oldest first out. A player who just pressed a key is looking for what that press did, so the
	// newest message is the one that must survive a full stack.
	while (Messages.Num() > MaxMessages)
	{
		Messages.RemoveAt(0);
	}

	RebuildRows();

	OnMessageAdded();
}

bool USpaceMMOTransientMessages::ExpireOldMessages(const double NowSeconds)
{
	const int32 Before = Messages.Num();

	Messages.RemoveAll([NowSeconds](const FSpaceMMOTransientMessage& Message)
	{
		return NowSeconds >= Message.ExpiresAt;
	});

	return Messages.Num() != Before;
}

void USpaceMMOTransientMessages::RebuildRows()
{
	if (MessageRows == nullptr || RowClass == nullptr)
	{
		// Warned once rather than per push: it is a wiring mistake, not an event, and a message that
		// goes nowhere is otherwise indistinguishable from one that was never sent.
		if (!bWarnedAboutWiring)
		{
			bWarnedAboutWiring = true;

			UE_LOG(LogSpaceMMOBackend, Warning,
				TEXT("HUD: transient messages show nothing — %s%s%s. Set them in the Widget "
					"Blueprint; MessageRows is bound by name and RowClass in Class Defaults."),
				MessageRows == nullptr ? TEXT("no panel named 'MessageRows'") : TEXT(""),
				MessageRows == nullptr && RowClass == nullptr ? TEXT(" and ") : TEXT(""),
				RowClass == nullptr ? TEXT("no RowClass set") : TEXT(""));
		}

		return;
	}

	MessageRows->ClearChildren();

	for (const FSpaceMMOTransientMessage& Message : Messages)
	{
		USpaceMMOTransientMessageRow* Row =
			CreateWidget<USpaceMMOTransientMessageRow>(GetOwningPlayer(), RowClass);

		if (Row == nullptr)
		{
			continue;
		}

		Row->SetMessage(Message);

		MessageRows->AddChild(Row);
	}
}

void USpaceMMOTransientMessages::FollowPawn()
{
	const APlayerController* Controller = GetOwningPlayer();

	if (Controller == nullptr)
	{
		return;
	}

	FVector2D Position;

	if (!SpaceMMO::Hud::ProjectAbove(Controller, Controller->GetPawn(), HeightScale, Position))
	{
		// First person puts the camera inside the pawn, so there is nothing to float above and the
		// projection lands behind the viewer. Rather than let the message be silently off screen,
		// it goes to a fixed place under the reticle — which is also where it ends up if the pawn is
		// ever behind the camera for any other reason.
		const FVector2D Viewport = UWidgetLayoutLibrary::GetViewportSize(this)
			/ FMath::Max(UWidgetLayoutLibrary::GetViewportScale(this), UE_KINDA_SMALL_NUMBER);

		Position = FVector2D(Viewport.X * 0.5, Viewport.Y * 0.62);
	}

	SpaceMMO::Hud::PlaceAt(MessageRoot, Position);
}

void USpaceMMOTransientMessages::NativeTick(const FGeometry& Geometry, const float DeltaSeconds)
{
	Super::NativeTick(Geometry, DeltaSeconds);

	// Never call SetVisibility on this widget from here — see the note on
	// USpaceMMOFlightReadout::NativeTick. This one is never hidden anyway: it has to keep ticking to
	// expire its own messages, and a widget that has stopped ticking cannot start again.
	const UWorld* World = GetWorld();

	if (World == nullptr)
	{
		return;
	}

	if (ExpireOldMessages(World->GetTimeSeconds()))
	{
		RebuildRows();
	}

	// Followed every frame rather than only when a message arrives, because the pawn moves while a
	// message is on screen — and a label that lags the thing it belongs to reads as a bug.
	if (!Messages.IsEmpty())
	{
		FollowPawn();
	}
}
