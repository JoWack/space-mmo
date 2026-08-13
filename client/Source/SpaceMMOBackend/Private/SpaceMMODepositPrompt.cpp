#include "SpaceMMODepositPrompt.h"

#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "GameFramework/InputSettings.h"
#include "SpaceMMOBackendClient.h"
#include "SpaceMMODepositActor.h"
#include "SpaceMMOGatheringComponent.h"

FSpaceMMODepositPromptText USpaceMMODepositPrompt::Build(
	const FBackendResourceNode& Node,
	const TArray<FBackendSkill>& Skills,
	const TArray<FBackendItemInstance>& Instances,
	const FString& GatherKey)
{
	FSpaceMMODepositPromptText Text;

	Text.bHasDeposit = !Node.Key.IsEmpty();

	if (!Text.bHasDeposit)
	{
		return Text;
	}

	Text.ItemName = Node.ItemName;
	Text.GatherKey = GatherKey;

	// The skill is named as well as the item, because nothing else tells a player that ferrite is
	// mined and scrap is gathered — the server has always decided it from the node.
	Text.Requirement = FString::Printf(TEXT("%s  ·  lv %d"), *Node.SkillKey, Node.RequiredLevel);

	int32 Level = 0;

	for (const FBackendSkill& Skill : Skills)
	{
		if (Skill.Key == Node.SkillKey)
		{
			Level = Skill.Level;

			break;
		}
	}

	const bool bLevelMet = Level >= Node.RequiredLevel;

	Text.LevelBlocker = bLevelMet ? FString() : FString::Printf(TEXT("you are lv %d"), Level);

	if (!Node.NeedsTool())
	{
		Text.bCanGather = bLevelMet;

		return Text;
	}

	Text.Tool = FString::Printf(TEXT("needs %s"), *Node.RequiredToolName);

	// Condition above zero, because that is exactly what the server asks: GuardToolAsync ignores a
	// broken tool. A prompt that counted one would promise a gather the server then refuses, which
	// is worse than saying nothing at all.
	bool bCarried = false;

	for (const FBackendItemInstance& Instance : Instances)
	{
		if (Instance.ItemKey == Node.RequiredToolKey && Instance.Condition > 0)
		{
			bCarried = true;

			break;
		}
	}

	Text.ToolBlocker = bCarried ? FString() : TEXT("you have none");
	Text.bCanGather = bLevelMet && bCarried;

	return Text;
}

FString USpaceMMODepositPrompt::FindGatherKey()
{
	const UInputSettings* Settings = GetDefault<UInputSettings>();

	if (Settings == nullptr)
	{
		return FString();
	}

	TArray<FInputActionKeyMapping> Mappings;

	Settings->GetActionMappingByName(TEXT("Gather"), Mappings);

	// The first binding rather than all of them: the hint is one glyph beside a rock, and a deposit
	// captioned "E / Gamepad_FaceButton_Right gather" is not a hint. An unbound action gives an
	// empty string, and the Blueprint hides the row rather than drawing empty brackets.
	for (const FInputActionKeyMapping& Mapping : Mappings)
	{
		if (Mapping.Key.IsValid())
		{
			return Mapping.Key.GetDisplayName(false).ToString();
		}
	}

	return FString();
}

void USpaceMMODepositPrompt::NativeTick(const FGeometry& Geometry, const float DeltaSeconds)
{
	Super::NativeTick(Geometry, DeltaSeconds);

	// Never call SetVisibility on this widget from here — see the note on
	// USpaceMMOFlightReadout::NativeTick. UpdateHudContext owns it.
	const APlayerController* Controller = GetOwningPlayer();

	const UGameInstance* GameInstance = GetGameInstance();

	const USpaceMMOBackendClient* Client = GameInstance != nullptr
		? GameInstance->GetSubsystem<USpaceMMOBackendClient>()
		: nullptr;

	if (Controller == nullptr || Client == nullptr)
	{
		return;
	}

	// Asked of the gathering component rather than searched for here, so the prompt and the gather
	// key can never disagree about which rock is in reach — the same reason BuildNearbyPanel does
	// it this way. A player told they are standing at something they are not is worse than silence.
	FBackendResourceNode Nearby;

	if (const APawn* Possessed = Controller->GetPawn())
	{
		if (const USpaceMMOGatheringComponent* Gathering =
			Possessed->FindComponentByClass<USpaceMMOGatheringComponent>())
		{
			if (const ASpaceMMODepositActor* Deposit = Gathering->FindDepositInRange())
			{
				Nearby = Deposit->GetNode();
			}
		}
	}

	const FSpaceMMODepositPromptText Text =
		Build(Nearby, Client->GetSkills(), Client->GetItemInstances(), FindGatherKey());

	auto Set = [](UTextBlock* Block, const FString& Value)
	{
		if (Block != nullptr)
		{
			Block->SetText(FText::FromString(Value));
		}
	};

	Set(ItemNameText, Text.ItemName);
	Set(RequirementText, Text.Requirement);
	Set(LevelBlockerText, Text.LevelBlocker);
	Set(ToolText, Text.Tool);
	Set(ToolBlockerText, Text.ToolBlocker);
	Set(GatherKeyText, Text.GatherKey);

	bHasDeposit = Text.bHasDeposit;
	bCanGather = Text.bCanGather;
	bNeedsTool = !Text.Tool.IsEmpty();
}
