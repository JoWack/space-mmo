#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "SpaceMMOBackendProtocol.h"
#include "SpaceMMOInventoryScreen.h"
#include "SpaceMMOItemStyleTag.h"
#include "SpaceMMOStationOverlay.h"
#include "SpaceMMOStationWork.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Task 187 on the client: the style tag reaches every row, and the pickers send the style they show.
 *
 * <strong>The recipes are the server's own words.</strong> RealRecipes is /industry/recipes as the API
 * answered it on 10 October after seeding the shipped pack, cut to three recipes: the thruster (a crafted
 * main input), Ares' refine (a raw one, with its world), and the laser (none). Built by hand, these tests
 * would only prove the client agrees with itself (CLAUDE.md, "feed in the real value at least once").
 */
namespace SpaceMMOItemStyleTests
{
	constexpr int32 Here = 5;
	constexpr int32 Elsewhere = 9;

	const TCHAR* RealRecipes = TEXT(R"([{"id":1,"key":"craft_crude_mining_laser","outputItemDefId":4,"outputItemKey":"crude_mining_laser","outputName":"Crude Mining Laser","outputQuantity":1,"skillKey":"toolcrafting","skillName":"Toolcrafting","requiredLevel":1,"jobSeconds":30,"xpPerRun":300,"requiredToolItemDefId":null,"requiredToolKey":null,"requiredToolName":null,"inputs":[{"itemDefId":1,"itemKey":"scrap_alloy","name":"Scrap Alloy","quantity":8,"style":0}],"mainInputKey":null},{"id":7,"key":"craft_crude_thruster","outputItemDefId":5,"outputItemKey":"crude_thruster","outputName":"Crude Thruster","outputQuantity":1,"skillKey":"electronics","skillName":"Electronics","requiredLevel":1,"jobSeconds":120,"xpPerRun":600,"requiredToolItemDefId":null,"requiredToolKey":null,"requiredToolName":null,"inputs":[{"itemDefId":3,"itemKey":"ferrite_plate","name":"Structural Plate","quantity":2,"style":0},{"itemDefId":1,"itemKey":"scrap_alloy","name":"Scrap Alloy","quantity":4,"style":0}],"mainInputKey":"ferrite_plate"},{"id":8,"key":"refine_oxide_salt_plate","outputItemDefId":3,"outputItemKey":"ferrite_plate","outputName":"Structural Plate","outputQuantity":4,"skillKey":"refining","skillName":"Refining","requiredLevel":1,"jobSeconds":60,"xpPerRun":600,"requiredToolItemDefId":null,"requiredToolKey":null,"requiredToolName":null,"inputs":[{"itemDefId":14,"itemKey":"oxide_salt","name":"Oxide Salt","quantity":20,"style":2}],"mainInputKey":"oxide_salt"}])");

	/** Indices into RealRecipes once parsed, which keeps the server's order. */
	constexpr int32 Laser = 0;
	constexpr int32 Thruster = 1;
	constexpr int32 OxideRefine = 2;

	TArray<FBackendRecipe> Recipes(FAutomationTestBase& Test)
	{
		TArray<FBackendRecipe> Parsed;
		Test.TestTrue(TEXT("The real recipes parse"), FSpaceMMOBackendProtocol::ParseRecipes(RealRecipes, Parsed));
		Test.TestEqual(TEXT("All three"), Parsed.Num(), 3);

		return Parsed;
	}

	FBackendInventoryItem Stack(
		const TCHAR* Key,
		const int32 Quantity,
		const EBackendItemStyle Style,
		const EBackendInventoryKind Kind = EBackendInventoryKind::StationHangar,
		const int32 StationId = Here,
		const int32 ItemDefId = 3)
	{
		FBackendInventoryItem Item;
		Item.ItemKey = Key;
		Item.ItemDefId = ItemDefId;
		Item.Name = TEXT("Structural Plate");
		Item.Quantity = Quantity;
		Item.Kind = Kind;
		Item.StationId = Kind == EBackendInventoryKind::StationHangar ? StationId : 0;
		Item.Style = Style;

		return Item;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOStyleRealRecipesCarryTheirMainInputTest,
	"SpaceMMO.Style.RealRecipesCarryTheirMainInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOStyleRealRecipesCarryTheirMainInputTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMOItemStyleTests;

	const TArray<FBackendRecipe> All = Recipes(*this);

	if (All.Num() != 3)
	{
		return false;
	}

	TestEqual(TEXT("The thruster is made in its plate's style"), All[Thruster].MainInputKey, FString(TEXT("ferrite_plate")));
	TestEqual(TEXT("A crafted input carries no world of its own"), All[Thruster].Inputs[0].Style, EBackendItemStyle::Common);

	TestEqual(TEXT("Ares' refine is made from its salt"), All[OxideRefine].MainInputKey, FString(TEXT("oxide_salt")));
	TestEqual(TEXT("The salt is Martian, from the wire"), All[OxideRefine].Inputs[0].Style, EBackendItemStyle::Martian);

	// Null on the wire, for the laser's scrap: Common goods, no picker.
	TestTrue(TEXT("The laser names no main input"), All[Laser].MainInputKey.IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOStyleInventoryStyleCrossesTheWireTest,
	"SpaceMMO.Style.InventoryStyleCrossesTheWire",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOStyleInventoryStyleCrossesTheWireTest::RunTest(const FString& Parameters)
{
	// The shape InventoryItemResponse and ItemInstanceResponse serialise to, the same shape the API test
	// Style_crosses_the_wire_as_a_number_and_a_transfer_moves_that_style reads back. The last stack has no
	// style, as a server older than task 187 would send.
	const FString Json = TEXT(R"({"stacks":[)")
		TEXT(R"({"inventoryId":11,"itemDefId":3,"itemKey":"ferrite_plate","name":"Structural Plate","quantity":6,"factionBuyPriceMinorUnits":null,"kind":2,"stationId":5,"style":2},)")
		TEXT(R"({"inventoryId":11,"itemDefId":2,"itemKey":"ferrite_ore","name":"Ferrite Ore","quantity":9,"factionBuyPriceMinorUnits":200,"kind":2,"stationId":5,"style":1},)")
		TEXT(R"({"inventoryId":11,"itemDefId":1,"itemKey":"scrap_alloy","name":"Scrap Alloy","quantity":40,"factionBuyPriceMinorUnits":100,"kind":2,"stationId":5}],)")
		TEXT(R"("items":[{"inventoryId":11,"id":71,"itemDefId":6,"itemKey":"hull_shuttle","name":"Shuttle","condition":100,"kind":2,"stationId":5,"category":8,"deployed":false,"style":4}],)")
		TEXT(R"("containers":[{"inventoryId":11,"kind":2,"stationId":5,"capacityM3":0}]})");

	TArray<FBackendInventoryItem> Stacks;
	TArray<FBackendItemInstance> Instances;
	TArray<FBackendInventoryContainer> Containers;

	TestTrue(TEXT("Parses"), FSpaceMMOBackendProtocol::ParseInventory(Json, Stacks, Instances, Containers));

	if (Stacks.Num() != 3 || Instances.Num() != 1)
	{
		AddError(TEXT("Expected three stacks and one instance"));

		return false;
	}

	TestEqual(TEXT("Martian plate"), Stacks[0].Style, EBackendItemStyle::Martian);
	TestEqual(TEXT("Ore reports its world"), Stacks[1].Style, EBackendItemStyle::Terran);
	TestEqual(TEXT("No style sent is Common"), Stacks[2].Style, EBackendItemStyle::Common);
	TestEqual(TEXT("A Grimhold hull"), Instances[0].Style, EBackendItemStyle::Grimhold);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOStyleRequestsNameTheStyleTest,
	"SpaceMMO.Style.RequestsNameTheStyle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOStyleRequestsNameTheStyleTest::RunTest(const FString& Parameters)
{
	// The keys and numbers the server's TransferRequest, PlaceOrderBody and StartJobRequest read.
	// Read back as JSON: this body comes from the engine's writer, which spaces its output.
	const FString Transfer = FSpaceMMOBackendProtocol::MakeTransferBody(1, 2, 3, 4, EBackendItemStyle::Martian);

	TSharedPtr<FJsonObject> Parsed;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Transfer);

	TestTrue(TEXT("A transfer body is JSON"), FJsonSerializer::Deserialize(Reader, Parsed) && Parsed.IsValid());

	int32 TransferStyle = -1;
	TestTrue(TEXT("A transfer names its stack"), Parsed.IsValid() && Parsed->TryGetNumberField(TEXT("style"), TransferStyle));
	TestEqual(TEXT("As a number"), TransferStyle, 2);

	const FString Sell = FSpaceMMOBackendProtocol::MakePlaceOrderBody(
		7, 5, 3, EBackendOrderSide::Sell, 420, 12, EBackendItemStyle::Verdant);
	TestTrue(TEXT("A sell names its style"), Sell.Contains(TEXT("\"style\":3")));

	const FString Buy = FSpaceMMOBackendProtocol::MakePlaceOrderBody(
		7, 5, 3, EBackendOrderSide::Buy, 420, 12, EBackendItemStyle::Verdant);
	TestFalse(TEXT("A buy takes any and names none"), Buy.Contains(TEXT("style")));

	const FString Unpicked = FSpaceMMOBackendProtocol::MakePlaceOrderBody(7, 5, 3, EBackendOrderSide::Sell, 420, 12);
	TestFalse(TEXT("An unpicked sell leaves it to the server"), Unpicked.Contains(TEXT("style")));

	const FString Job = FSpaceMMOBackendProtocol::MakeStartJobBody(7, 2, 5, 1, EBackendItemStyle::Grimhold);
	TestTrue(TEXT("A job names its main input's style"), Job.Contains(TEXT("\"mainInputStyle\":4")));

	const FString PlainJob = FSpaceMMOBackendProtocol::MakeStartJobBody(7, 2, 5, 1);
	TestFalse(TEXT("Or leaves it to the server"), PlainJob.Contains(TEXT("mainInputStyle")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOStyleTagsReadAsApprovedTest,
	"SpaceMMO.Style.TagsReadAsApproved",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOStyleTagsReadAsApprovedTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMO;

	TestEqual(TEXT("Terran"), ItemStyleTag::Text(EBackendItemStyle::Terran), FString(TEXT("[TER]")));
	TestEqual(TEXT("Martian"), ItemStyleTag::Text(EBackendItemStyle::Martian), FString(TEXT("[MAR]")));
	TestEqual(TEXT("Verdant"), ItemStyleTag::Text(EBackendItemStyle::Verdant), FString(TEXT("[VER]")));
	TestEqual(TEXT("Grimhold"), ItemStyleTag::Text(EBackendItemStyle::Grimhold), FString(TEXT("[GRM]")));

	// Common shows nothing, the same as scrap.
	TestTrue(TEXT("Common is untagged"), ItemStyleTag::Text(EBackendItemStyle::Common).IsEmpty());
	TestFalse(TEXT("Common is not shown"), ItemStyleTag::IsShown(EBackendItemStyle::Common));

	// The approved hexes, read back as sRGB so a typo in one is a named failure.
	TestEqual(TEXT("Terra's sky blue"), ItemStyleTag::Colour(EBackendItemStyle::Terran).ToFColorSRGB(), FColor(0x7F, 0xB8, 0xD8));
	TestEqual(TEXT("Ares' oxide"), ItemStyleTag::Colour(EBackendItemStyle::Martian).ToFColorSRGB(), FColor(0xD0, 0x58, 0x3A));
	TestEqual(TEXT("Verdance's jade"), ItemStyleTag::Colour(EBackendItemStyle::Verdant).ToFColorSRGB(), FColor(0x8F, 0xF0, 0xD0));
	TestEqual(TEXT("Grimhold's ember"), ItemStyleTag::Colour(EBackendItemStyle::Grimhold).ToFColorSRGB(), FColor(0xD2, 0x69, 0x1E));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOStyleMainInputCountsOneStyleTest,
	"SpaceMMO.Style.MainInputCountsOneStyle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOStyleMainInputCountsOneStyleTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMOItemStyleTests;

	const TArray<FBackendRecipe> All = Recipes(*this);

	if (All.Num() != 3)
	{
		return false;
	}

	const TArray<FBackendInventoryItem> Inventory{
		Stack(TEXT("ferrite_plate"), 2, EBackendItemStyle::Terran),
		Stack(TEXT("ferrite_plate"), 6, EBackendItemStyle::Martian),

		// Not here, so neither counted nor offered.
		Stack(TEXT("ferrite_plate"), 50, EBackendItemStyle::Grimhold, EBackendInventoryKind::StationHangar, Elsewhere),
		Stack(TEXT("scrap_alloy"), 40, EBackendItemStyle::Common, EBackendInventoryKind::StationHangar, Here, 1),
	};

	// Unpicked: the style held most of, as the server would choose.
	{
		const TArray<FSpaceMMORecipeRowText> Rows = FSpaceMMOStationWork::BuildRecipeRows(All, Inventory, Here, Thruster, 1);
		const FSpaceMMORecipeInputText& Plate = Rows[Thruster].Inputs[0];

		TestTrue(TEXT("The plate is the main input"), Plate.bMain);
		TestEqual(TEXT("Martian, held most"), Plate.Style, EBackendItemStyle::Martian);
		TestEqual(TEXT("Counts that style only"), Plate.Held, 6);
		TestTrue(TEXT("Two styles here: ◂ ▸"), Plate.bCanPick);
		TestFalse(TEXT("Scrap is not"), Rows[Thruster].Inputs[1].bMain);
		TestEqual(TEXT("Scrap counts every style"), Rows[Thruster].Inputs[1].Held, 40);

		const FSpaceMMOStartBarText Bar = FSpaceMMOStationWork::BuildStartBar(All, Inventory, Here, Thruster, 1);

		TestEqual(TEXT("Approved label"), Bar.Label, FString(TEXT("Start Crude Thruster [MAR] ×1")));
		TestEqual(TEXT("Sends Martian"), Bar.MainInputStyle, TOptional<EBackendItemStyle>(EBackendItemStyle::Martian));
		TestEqual(TEXT("Six Martian plates make three"), Bar.MostRuns, 3);
	}

	// Picked Terran.
	{
		const TOptional<EBackendItemStyle> Pick(EBackendItemStyle::Terran);

		const TArray<FSpaceMMORecipeRowText> Rows =
			FSpaceMMOStationWork::BuildRecipeRows(All, Inventory, Here, Thruster, 1, Pick);

		TestEqual(TEXT("Terran shown"), Rows[Thruster].Inputs[0].Style, EBackendItemStyle::Terran);
		TestEqual(TEXT("Two Terran held"), Rows[Thruster].Inputs[0].Held, 2);

		const FSpaceMMOStartBarText Bar = FSpaceMMOStationWork::BuildStartBar(All, Inventory, Here, Thruster, 3, Pick);

		TestEqual(TEXT("Two Terran plates make one"), Bar.MostRuns, 1);
		TestEqual(TEXT("And the count stops there"), Bar.Runs, 1);
		TestEqual(TEXT("Sends Terran"), Bar.MainInputStyle, Pick);
	}

	// Picked a style held only at another station: the pick is a wish, and falls back.
	{
		const TArray<FSpaceMMORecipeRowText> Rows = FSpaceMMOStationWork::BuildRecipeRows(
			All, Inventory, Here, Thruster, 1, TOptional<EBackendItemStyle>(EBackendItemStyle::Grimhold));

		TestEqual(TEXT("Falls back to most held here"), Rows[Thruster].Inputs[0].Style, EBackendItemStyle::Martian);
	}

	// ◂ ▸ step through what is held here, and wrap.
	const TArray<EBackendItemStyle> Held = FSpaceMMOStationWork::StylesHeldHere(Inventory, TEXT("ferrite_plate"), Here);

	TestEqual(TEXT("Two styles here"), Held.Num(), 2);
	TestEqual(TEXT("▸ from Terran"), FSpaceMMOStationWork::StepStyle(Held, EBackendItemStyle::Terran, 1),
		TOptional<EBackendItemStyle>(EBackendItemStyle::Martian));
	TestEqual(TEXT("▸ wraps"), FSpaceMMOStationWork::StepStyle(Held, EBackendItemStyle::Martian, 1),
		TOptional<EBackendItemStyle>(EBackendItemStyle::Terran));
	TestEqual(TEXT("◂ wraps"), FSpaceMMOStationWork::StepStyle(Held, EBackendItemStyle::Terran, -1),
		TOptional<EBackendItemStyle>(EBackendItemStyle::Martian));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOStyleRawAndCommonRecipesTest,
	"SpaceMMO.Style.RawAndCommonRecipes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOStyleRawAndCommonRecipesTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMOItemStyleTests;

	const TArray<FBackendRecipe> All = Recipes(*this);

	if (All.Num() != 3)
	{
		return false;
	}

	const TArray<FBackendInventoryItem> Nothing;

	// A refine's ore has one world: tagged before any is held, from the recipe alone, and never pickable.
	{
		const TArray<FSpaceMMORecipeRowText> Rows = FSpaceMMOStationWork::BuildRecipeRows(All, Nothing, Here, OxideRefine, 1);

		TestEqual(TEXT("Salt is Martian"), Rows[OxideRefine].Inputs[0].Style, EBackendItemStyle::Martian);
		TestFalse(TEXT("Nothing to pick"), Rows[OxideRefine].Inputs[0].bCanPick);

		const FSpaceMMOStartBarText Bar = FSpaceMMOStationWork::BuildStartBar(All, Nothing, Here, OxideRefine, 1);

		TestEqual(TEXT("Martian plate"), Bar.Label, FString(TEXT("Start Structural Plate [MAR] ×4")));
		TestTrue(TEXT("Short of salt"), Bar.bShort);
	}

	// The laser: Common, so no tag and nothing sent.
	{
		const TArray<FBackendInventoryItem> Scrap{
			Stack(TEXT("scrap_alloy"), 8, EBackendItemStyle::Common, EBackendInventoryKind::StationHangar, Here, 1)};

		const FSpaceMMOStartBarText Bar = FSpaceMMOStationWork::BuildStartBar(All, Scrap, Here, Laser, 1);

		TestEqual(TEXT("No tag"), Bar.Label, FString(TEXT("Start Crude Mining Laser ×1")));
		TestFalse(TEXT("No style sent"), Bar.MainInputStyle.IsSet());
		TestEqual(TEXT("One laser's scrap"), Bar.MostRuns, 1);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOStyleRowsCarryTheirStyleTest,
	"SpaceMMO.Style.RowsCarryTheirStyle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOStyleRowsCarryTheirStyleTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMOItemStyleTests;

	// The inventory: two styles of plate in one hangar are two lines, Terran first, each carrying its
	// style to the tag and to whatever drags it.
	{
		FBackendInventoryItem Martian = Stack(TEXT("ferrite_plate"), 4, EBackendItemStyle::Martian);
		FBackendInventoryItem Terran = Stack(TEXT("ferrite_plate"), 12, EBackendItemStyle::Terran);
		Martian.InventoryId = 11;
		Terran.InventoryId = 11;

		FBackendInventoryContainer Hangar;
		Hangar.InventoryId = 11;
		Hangar.Kind = EBackendInventoryKind::StationHangar;
		Hangar.StationId = Here;

		const TArray<FSpaceMMOInventoryLine> Lines =
			USpaceMMOInventoryScreen::Build({Martian, Terran}, {}, {Hangar}, {}, Here);

		TestEqual(TEXT("A heading and two stacks"), Lines.Num(), 3);

		if (Lines.Num() == 3)
		{
			TestEqual(TEXT("Terran first"), Lines[1].Style, EBackendItemStyle::Terran);
			TestEqual(TEXT("Its own count"), Lines[1].Quantity, 12);
			TestEqual(TEXT("Then Martian"), Lines[2].Style, EBackendItemStyle::Martian);
			TestEqual(TEXT("A heading has none"), Lines[0].Style, EBackendItemStyle::Common);
		}
	}

	// Jobs: name, tag, count.
	{
		FBackendIndustryJob Job;
		Job.Id = 3;
		Job.OutputName = TEXT("Crude Thruster");
		Job.OutputQuantityTotal = 1;
		Job.OutputStyle = EBackendItemStyle::Terran;

		const TArray<FSpaceMMOJobRowText> Rows = FSpaceMMOStationWork::BuildJobRows({Job}, {});

		TestEqual(TEXT("Name"), Rows[0].Name, FString(TEXT("Crude Thruster")));
		TestEqual(TEXT("Count"), Rows[0].Count, FString(TEXT("×1")));
		TestEqual(TEXT("Tag"), Rows[0].Style, EBackendItemStyle::Terran);
	}

	// My orders: a sell's style, a buy's "any style".
	{
		FBackendMyOrder Sell;
		Sell.OrderId = 1;
		Sell.Side = EBackendOrderSide::Sell;
		Sell.Style = EBackendItemStyle::Verdant;

		FBackendMyOrder Buy;
		Buy.OrderId = 2;
		Buy.Side = EBackendOrderSide::Buy;

		const TArray<FSpaceMMOMyOrderRowText> Rows = USpaceMMOStationOverlay::BuildMyOrderRows({Sell, Buy}, Here);

		TestEqual(TEXT("The sell's style"), Rows[0].Style, EBackendItemStyle::Verdant);
		TestFalse(TEXT("A sell is one style"), Rows[0].bAnyStyle);
		TestTrue(TEXT("A buy takes any"), Rows[1].bAnyStyle);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOStyleSellPromptOpensOnAStyleTest,
	"SpaceMMO.Style.SellPromptOpensOnAStyle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOStyleSellPromptOpensOnAStyleTest::RunTest(const FString& Parameters)
{
	using namespace SpaceMMOItemStyleTests;

	const TArray<FBackendInventoryItem> Inventory{
		Stack(TEXT("ferrite_plate"), 3, EBackendItemStyle::Terran),

		// In their pockets, which the server tops up from when they are docked.
		Stack(TEXT("ferrite_plate"), 5, EBackendItemStyle::Martian, EBackendInventoryKind::CharacterCarried),

		// In a ship's hold and another station's hangar: neither can back a sell here.
		Stack(TEXT("ferrite_plate"), 9, EBackendItemStyle::Grimhold, EBackendInventoryKind::ShipHold),
		Stack(TEXT("ferrite_plate"), 9, EBackendItemStyle::Verdant, EBackendInventoryKind::StationHangar, Elsewhere),
	};

	const TArray<EBackendItemStyle> Styles = USpaceMMOStationOverlay::SellableStyles(Inventory, 3, Here);

	TestEqual(TEXT("Hangar and pockets only"), Styles.Num(), 2);
	TestTrue(TEXT("Terran"), Styles.Contains(EBackendItemStyle::Terran));
	TestTrue(TEXT("Martian"), Styles.Contains(EBackendItemStyle::Martian));

	TestEqual(TEXT("The Sell button opens on the style held most of"),
		USpaceMMOStationOverlay::OpeningSellStyle(Inventory, 3, Here, TOptional<EBackendItemStyle>()),
		TOptional<EBackendItemStyle>(EBackendItemStyle::Martian));

	TestEqual(TEXT("A drag opens on the dragged stack's"),
		USpaceMMOStationOverlay::OpeningSellStyle(Inventory, 3, Here, EBackendItemStyle::Terran),
		TOptional<EBackendItemStyle>(EBackendItemStyle::Terran));

	TestEqual(TEXT("And offers that style's amount"),
		USpaceMMOStationOverlay::SellableQuantity(Inventory, 3, Here, EBackendItemStyle::Terran), 3);

	TestFalse(TEXT("Nothing held, nothing to open on"),
		USpaceMMOStationOverlay::OpeningSellStyle({}, 3, Here, TOptional<EBackendItemStyle>()).IsSet());

	return true;
}

#endif
