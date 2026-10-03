#include "Misc/AutomationTest.h"

#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "SpaceMMOPanelRow.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace SpaceMMOPanelStyleTests
{
	UWidgetBlueprintGeneratedClass* LoadPanel(const FString& Name)
	{
		return LoadObject<UWidgetBlueprintGeneratedClass>(
			nullptr, *FString::Printf(TEXT("/Game/UI/%s.%s_C"), *Name, *Name));
	}

	/** The colour properties a Blueprint binding could set on a text, a border or a border's content. */
	bool IsColourBinding(const FDelegateRuntimeBinding& Binding)
	{
		return Binding.PropertyName == TEXT("ColorAndOpacity") || Binding.PropertyName == TEXT("BrushColor")
			|| Binding.PropertyName == TEXT("ContentColorAndOpacity");
	}
}

/**
 * The panels' rows, as built, style themselves -- and nothing in their Blueprints styles them back.
 *
 * <strong>Read off the saved Blueprints, not a hand-built row.</strong> Task 173 moved row colours
 * from Blueprint bindings into USpaceMMOPanelRow, by a commandlet editing Joe's Blueprints in place. A
 * row class whose C++ is right proves nothing if the Blueprint was never reparented, and a colour
 * binding left behind -- or added back in the designer -- is evaluated every frame and silently wins
 * over the selected outline and the dimmed name, which reads as the restyle not having worked.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOPanelRowsStyleThemselvesTest,
	"SpaceMMO.Panels.RowsStyleThemselves",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOPanelRowsStyleThemselvesTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMOPanelStyleTests;

	// Rows that are drawn in a box. The text row is a plain line and has none.
	const TArray<FString> Boxed = {TEXT("WBP_InventoryRow"), TEXT("WBP_MarketRow"), TEXT("WBP_BookRow"),
		TEXT("WBP_ShipRow"), TEXT("WBP_MyOrderRow"), TEXT("WBP_SkillRow"), TEXT("WBP_RecipeRow"), TEXT("WBP_JobRow"),
		TEXT("WBP_QuestRow")};

	TArray<FString> Rows = Boxed;
	Rows.Add(TEXT("WBP_TextRow"));

	for (const FString& Name : Rows)
	{
		const UWidgetBlueprintGeneratedClass* const Row = LoadPanel(Name);

		if (!TestNotNull(*FString::Printf(TEXT("%s loads"), *Name), Row))
		{
			continue;
		}

		TestTrue(*FString::Printf(TEXT("%s is a row that styles itself"), *Name),
			Row->IsChildOf(USpaceMMOPanelRow::StaticClass()));

		for (const FDelegateRuntimeBinding& Binding : Row->Bindings)
		{
			TestFalse(*FString::Printf(TEXT("%s binds no colour (found %s.%s)"), *Name, *Binding.ObjectName,
						  *Binding.PropertyName.ToString()),
				IsColourBinding(Binding));
		}

		if (Boxed.Contains(Name))
		{
			const UWidgetTree* const Tree = Row->GetWidgetTreeArchetype();

			TestTrue(*FString::Printf(TEXT("%s has the RowFrame its look is drawn on"), *Name),
				Tree != nullptr && Cast<UBorder>(Tree->FindWidget(TEXT("RowFrame"))) != nullptr);
		}
	}

	// And the overlay is told which rows to stamp out for Industry and Quests. Unset, those tabs are
	// empty with nothing but one log line to say why -- a wiring fault that reads as no recipes at all.
	if (const UWidgetBlueprintGeneratedClass* const Overlay = LoadPanel(TEXT("WBP_StationOverlay")))
	{
		for (const TCHAR* Property : {TEXT("RecipeRowClass"), TEXT("JobRowClass"), TEXT("QuestRowClass")})
		{
			const FClassProperty* const Slot = FindFProperty<FClassProperty>(Overlay, Property);

			TestTrue(*FString::Printf(TEXT("The station overlay names its %s"), Property),
				Slot != nullptr && Slot->GetObjectPropertyValue_InContainer(Overlay->GetDefaultObject()) != nullptr);
		}
	}

	return true;
}

/**
 * The panels, as built, carry the parts the approved look draws with.
 *
 * Optional parts on the C++ side, so a Blueprint missing one still opens -- as the old look, with no
 * error anywhere. This is where that would show.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOPanelsCarryTheirLookTest,
	"SpaceMMO.Panels.CarryTheirLook",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOPanelsCarryTheirLookTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMOPanelStyleTests;

	const TMap<FString, TArray<FString>> Needs = {
		{TEXT("WBP_InventoryScreen"), {TEXT("WorldDim")}},
		{TEXT("WBP_SkillsScreen"), {TEXT("WorldDim")}},
		{TEXT("WBP_StationOverlay"),
			{TEXT("WorldDim"), TEXT("MarketTabFrame"), TEXT("IndustryTabFrame"), TEXT("QuestsTabFrame"),
				TEXT("MyOrdersTabFrame"), TEXT("ShipsTabFrame"), TEXT("StartBar"), TEXT("StartButton"), TEXT("RunsText")}},

		// Round two, 3 October: the readouts, the deposit prompt, the messages and the sign-in screen.
		{TEXT("WBP_FlightReadout"), {TEXT("ReadoutCard"), TEXT("ProximityChip"), TEXT("DebugBox"), TEXT("SystemPositionBox")}},
		{TEXT("WBP_OnFootReadout"), {TEXT("ReadoutCard"), TEXT("CreditsLine")}},
		{TEXT("WBP_DepositPrompt"), {TEXT("PromptCard"), TEXT("KeyCap"), TEXT("GatherTextLabel")}},
		{TEXT("WBP_TransientMessageRow"), {TEXT("MessageFrame"), TEXT("ToneEdge")}},
		{TEXT("WBP_LoginScreen"), {TEXT("SignInGlass"), TEXT("EmailBox"), TEXT("PasswordBox"), TEXT("SignInButton")}},
	};

	for (const TPair<FString, TArray<FString>>& Panel : Needs)
	{
		const UWidgetBlueprintGeneratedClass* const Built = LoadPanel(Panel.Key);

		if (!TestNotNull(*FString::Printf(TEXT("%s loads"), *Panel.Key), Built))
		{
			continue;
		}

		const UWidgetTree* const Tree = Built->GetWidgetTreeArchetype();

		for (const FString& Part : Panel.Value)
		{
			TestTrue(*FString::Printf(TEXT("%s has %s"), *Panel.Key, *Part),
				Tree != nullptr && Tree->FindWidget(FName(*Part)) != nullptr);
		}

		// Their colours are set in C++ now -- the tabs, the gather key, a message's tone -- and a binding
		// left on one would override it every frame.
		for (const FDelegateRuntimeBinding& Binding : Built->Bindings)
		{
			TestFalse(*FString::Printf(TEXT("%s binds no colour (found %s.%s)"), *Panel.Key, *Binding.ObjectName,
						  *Binding.PropertyName.ToString()),
				IsColourBinding(Binding));
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
