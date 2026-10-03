#include "SpaceMMOStylePanelsCommandlet.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/BorderSlot.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableText.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ProgressBar.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "EdGraph/EdGraph.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "SpaceMMOAuthoringLog.h"
#include "SpaceMMOStyle.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "WidgetBlueprint.h"

USpaceMMOStylePanelsCommandlet::USpaceMMOStylePanelsCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

namespace SpaceMMOStylePanels
{
	using namespace SpaceMMO::Style;
	using SpaceMMO::Style::ETextRole;
	using SpaceMMO::Style::ERowLook;

	/** One Blueprint being restyled: its tree, and a count of what went wrong. */
	struct FEdit
	{
		UWidgetBlueprint* Blueprint = nullptr;
		int32 Problems = 0;

		UWidgetTree* Tree() const { return Blueprint->WidgetTree; }

		template <typename WidgetType>
		WidgetType* Find(const TCHAR* Name) const
		{
			return Cast<WidgetType>(Blueprint->WidgetTree->FindWidget(FName(Name)));
		}

		/** A widget that must be there. Missing is said, and counted, rather than skipped quietly. */
		template <typename WidgetType>
		WidgetType* Need(const TCHAR* Name)
		{
			WidgetType* Found = Find<WidgetType>(Name);

			if (Found == nullptr)
			{
				UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Panels: %s has no %s named %s."),
					*Blueprint->GetName(), *WidgetType::StaticClass()->GetName(), Name);

				++Problems;
			}

			return Found;
		}

		/** Takes a widget out of the tree and out of the Blueprint's record of its variables. */
		void Remove(UWidget* Widget)
		{
			if (Widget != nullptr)
			{
				Blueprint->OnVariableRemoved(Widget->GetFName());
				Tree()->RemoveWidget(Widget);

				// Out of the Blueprint altogether, as the designer does when it deletes (WidgetBlueprint-
				// EditorUtils, "Rename(nullptr, GetTransientPackage())"). Taken out of its parent but still
				// owned by the tree, a widget is found again by the compiler and reported as one added
				// without a GUID -- which is how the sign-in screen's empty boxes failed, 3 October.
				Widget->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
				Widget->MarkAsGarbage();
			}
		}

		/** A new widget, or the one already made by an earlier run. */
		template <typename WidgetType>
		WidgetType* Make(const TCHAR* Name, const bool bPart, bool* bOutExisted = nullptr)
		{
			if (WidgetType* Existing = Find<WidgetType>(Name))
			{
				if (bOutExisted != nullptr)
				{
					*bOutExisted = true;
				}

				return Existing;
			}

			if (bOutExisted != nullptr)
			{
				*bOutExisted = false;
			}

			WidgetType* Widget = Tree()->ConstructWidget<WidgetType>(WidgetType::StaticClass(), FName(Name));
			Widget->bIsVariable = bPart;
			Blueprint->OnVariableAdded(Widget->GetFName());

			return Widget;
		}
	};

	// ------------------------------------------------------------------------------------------------
	// Styling one widget

	void Text(UTextBlock* Block, const ETextRole Role, const TOptional<FLinearColor> Colour = TOptional<FLinearColor>())
	{
		if (Block == nullptr)
		{
			return;
		}

		const FTextSpec Spec = TextSpec(Role);

		FSlateFontInfo Font = Block->GetFont();
		Font.TypefaceFontName = Spec.Typeface;
		Font.Size = Spec.Size;
		Font.LetterSpacing = Spec.LetterSpacing;

		Block->SetFont(Font);
		Block->SetColorAndOpacity(FSlateColor(Colour.Get(Spec.Colour)));
		Block->SetTextTransformPolicy(Spec.bUpper ? ETextTransformPolicy::ToUpper : ETextTransformPolicy::None);
	}

	void Figure(UTextBlock* Block, const ETextRole Role, const float Width = 140.0f)
	{
		Text(Block, Role);

		if (Block != nullptr)
		{
			Block->SetMinDesiredWidth(Width);
			Block->SetJustification(ETextJustify::Right);
		}
	}

	/**
	 * Where a part sits in its row: whether it takes the spare width, and what it keeps clear of. A row's
	 * figures are fixed-width so they line up under a heading laid out the same way; left to fill, every
	 * column shared the width and each figure ran into the next (3 October's look: "4" and "20.00 cr" read as
	 * "420.00 cr").
	 */
	void Column(UWidget* Part, const bool bFill, const FMargin Clear = FMargin(0.0f))
	{
		if (UHorizontalBoxSlot* At = Part != nullptr ? Cast<UHorizontalBoxSlot>(Part->Slot) : nullptr)
		{
			At->SetSize(FSlateChildSize(bFill ? ESlateSizeRule::Fill : ESlateSizeRule::Automatic));
			At->SetPadding(Clear);
		}
	}

	// The order columns, shared by the my-orders heading and its rows so the one sits over the other.
	constexpr float SideWidth = 60.0f;
	constexpr float QuantityWidth = 90.0f;
	constexpr float PriceWidth = 140.0f;
	constexpr float BeforeButton = 20.0f;

	// The rows end in a "Cancel Order" button the heading has no counterpart for, so the heading's last
	// column keeps clear of its width. Measured off the 1920x1080 capture; a different label needs it again.
	constexpr float CancelButtonWidth = 164.0f;

	void Button(UButton* Made, const bool bPrimary, const bool bSmall = false)
	{
		if (Made == nullptr)
		{
			return;
		}

		Made->SetStyle(ButtonStyle(bPrimary, bSmall));
		Made->SetColorAndOpacity(FLinearColor::White);
		Made->SetBackgroundColor(FLinearColor::White);

		if (UTextBlock* Label = Cast<UTextBlock>(Made->GetChildAt(0)))
		{
			Text(Label, bSmall ? ETextRole::ButtonSmall : ETextRole::Button);
		}
	}

	FSlateBrush FieldBrush(const bool bFocused)
	{
		return Rounded(White(0.04f), bFocused ? Ice() : White(0.30f), 1.0f, RowCorners());
	}

	void Field(UEditableTextBox* Box)
	{
		if (Box == nullptr)
		{
			return;
		}

		FEditableTextBoxStyle Style = Box->WidgetStyle;
		Style.SetBackgroundImageNormal(FieldBrush(false));
		Style.SetBackgroundImageHovered(FieldBrush(false));
		Style.SetBackgroundImageFocused(FieldBrush(true));
		Style.SetBackgroundImageReadOnly(FieldBrush(false));
		Style.SetPadding(FMargin(14.0f, 8.0f));

		FSlateFontInfo Font = Style.TextStyle.Font;
		Font.TypefaceFontName = TEXT("Regular");
		Font.Size = 18.0f;
		Style.SetFont(Font);
		Style.SetForegroundColor(FSlateColor(TextPrimary()));
		Style.SetFocusedForegroundColor(FSlateColor(TextPrimary()));

		// The typed text's colour is the text style's, not the box's foreground: SEditableTextBox gives the
		// foreground to its border and the text style to the text. Left at the engine's default -- dark, for
		// a white box -- the sign-in fields drew black on the glass (Joe, 3 October).
		Style.TextStyle.SetColorAndOpacity(FSlateColor(TextPrimary()));

		Box->SetWidgetStyle(Style);
	}

	void Field(UEditableText* Line)
	{
		if (Line == nullptr)
		{
			return;
		}

		FEditableTextStyle Style = Line->WidgetStyle;
		FSlateFontInfo Font = Style.Font;
		Font.TypefaceFontName = TEXT("Regular");
		Font.Size = 18.0f;
		Style.SetFont(Font);
		Style.SetColorAndOpacity(FSlateColor(TextPrimary()));

		Line->SetWidgetStyle(Style);
	}

	void Slider(USlider* Bar)
	{
		if (Bar == nullptr)
		{
			return;
		}

		FSliderStyle Style = Bar->GetWidgetStyle();
		FSlateBrush Track = Rounded(White(0.15f), White(0.0f), 0.0f, FVector4(2.0, 2.0, 2.0, 2.0));
		FSlateBrush Thumb = Rounded(Ice(), White(0.0f), 0.0f, FVector4(8.0, 8.0, 8.0, 8.0));
		Thumb.ImageSize = FVector2D(16.0, 16.0);

		Style.SetNormalBarImage(Track);
		Style.SetHoveredBarImage(Track);
		Style.SetNormalThumbImage(Thumb);
		Style.SetHoveredThumbImage(Thumb);
		Style.SetBarThickness(4.0f);

		Bar->SetWidgetStyle(Style);
		Bar->SetSliderBarColor(FLinearColor::White);
		Bar->SetSliderHandleColor(FLinearColor::White);
	}

	// ------------------------------------------------------------------------------------------------
	// Structure

	/** The four corner brackets of the menus, inside a panel's overlay. Added once. */
	void Brackets(FEdit& Edit, UOverlay* Panel)
	{
		if (Panel == nullptr || Edit.Find<UImage>(TEXT("BracketTLAcross")) != nullptr)
		{
			return;
		}

		struct FCorner
		{
			const TCHAR* Name;
			EHorizontalAlignment H;
			EVerticalAlignment V;
		};

		const FCorner Corners[] = {
			{TEXT("TL"), HAlign_Left, VAlign_Top},
			{TEXT("TR"), HAlign_Right, VAlign_Top},
			{TEXT("BL"), HAlign_Left, VAlign_Bottom},
			{TEXT("BR"), HAlign_Right, VAlign_Bottom},
		};

		for (const FCorner& Corner : Corners)
		{
			for (const bool bAcross : {true, false})
			{
				UImage* Line = Edit.Make<UImage>(
					*FString::Printf(TEXT("Bracket%s%s"), Corner.Name, bAcross ? TEXT("Across") : TEXT("Down")), false);
				Line->SetBrush(Solid(White(0.55f), bAcross ? FVector2D(20.0, 2.0) : FVector2D(2.0, 20.0)));
				Line->SetVisibility(ESlateVisibility::HitTestInvisible);

				UOverlaySlot* At = Panel->AddChildToOverlay(Line);
				At->SetHorizontalAlignment(Corner.H);
				At->SetVerticalAlignment(Corner.V);
			}
		}
	}

	/** The light dim over the world, first in the canvas so the panel draws over it. Added once. */
	void WorldDim(FEdit& Edit, UCanvasPanel* Root)
	{
		if (Root == nullptr || Edit.Find<UImage>(TEXT("WorldDim")) != nullptr)
		{
			return;
		}

		UImage* Dim = Edit.Make<UImage>(TEXT("WorldDim"), true);
		Dim->SetBrush(Solid(SpaceMMO::Style::WorldDim(), FVector2D(32.0, 32.0)));
		Dim->SetVisibility(ESlateVisibility::HitTestInvisible);

		UCanvasPanelSlot* Fill = Cast<UCanvasPanelSlot>(Root->InsertChildAt(0, Dim));
		Fill->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
		Fill->SetOffsets(FMargin(0.0f));
	}

	/**
	 * A panel's fixed size, centred on its anchor. The paired panels move it sideways at run time, by the same
	 * fraction of the screen either way, so the two paired panels are the same width or they sit unevenly.
	 */
	void PanelSize(UWidget* Panel, const float Width, const float Height)
	{
		if (UCanvasPanelSlot* Slot = Panel != nullptr ? Cast<UCanvasPanelSlot>(Panel->Slot) : nullptr)
		{
			Slot->SetAnchors(FAnchors(0.5f, 0.5f));
			Slot->SetAlignment(FVector2D(0.0, 0.0));
			Slot->SetAutoSize(false);
			Slot->SetOffsets(FMargin(-Width * 0.5f, -Height * 0.5f, Width, Height));
		}
	}

	/**
	 * Puts a new border between a widget and its parent, keeping the slot it had, and returns the border.
	 * If the widget is the tree's root, the border becomes the root.
	 */
	UBorder* Wrap(FEdit& Edit, UWidget* Inner, const TCHAR* Name, const bool bPart)
	{
		bool bExisted = false;
		UBorder* Frame = Edit.Make<UBorder>(Name, bPart, &bExisted);

		if (bExisted || Inner == nullptr)
		{
			return Frame;
		}

		if (Edit.Tree()->RootWidget == Inner)
		{
			Edit.Tree()->RootWidget = Frame;
			Frame->SetContent(Inner);

			return Frame;
		}

		UPanelWidget* Parent = Inner->GetParent();
		const int32 Index = Parent->GetChildIndex(Inner);

		// The slot's settings, read before the widget leaves it.
		FMargin Padding;
		EHorizontalAlignment H = HAlign_Fill;
		EVerticalAlignment V = VAlign_Fill;
		FSlateChildSize Size;

		if (const UVerticalBoxSlot* VB = Cast<UVerticalBoxSlot>(Inner->Slot))
		{
			Padding = VB->GetPadding();
			H = VB->GetHorizontalAlignment();
			V = VB->GetVerticalAlignment();
			Size = VB->GetSize();
		}
		else if (const UHorizontalBoxSlot* HB = Cast<UHorizontalBoxSlot>(Inner->Slot))
		{
			Padding = HB->GetPadding();
			H = HB->GetHorizontalAlignment();
			V = HB->GetVerticalAlignment();
			Size = HB->GetSize();
		}
		else if (const UBorderSlot* BS = Cast<UBorderSlot>(Inner->Slot))
		{
			Padding = BS->GetPadding();
			H = BS->GetHorizontalAlignment();
			V = BS->GetVerticalAlignment();
		}

		Parent->RemoveChild(Inner);
		UPanelSlot* Made = Parent->InsertChildAt(Index, Frame);
		Frame->SetContent(Inner);

		if (UVerticalBoxSlot* VB = Cast<UVerticalBoxSlot>(Made))
		{
			VB->SetPadding(Padding);
			VB->SetHorizontalAlignment(H);
			VB->SetVerticalAlignment(V);
			VB->SetSize(Size);
		}
		else if (UHorizontalBoxSlot* HB = Cast<UHorizontalBoxSlot>(Made))
		{
			HB->SetPadding(Padding);
			HB->SetHorizontalAlignment(H);
			HB->SetVerticalAlignment(V);
			HB->SetSize(Size);
		}
		else if (UBorderSlot* BS = Cast<UBorderSlot>(Made))
		{
			BS->SetPadding(FMargin(0.0f));
			BS->SetHorizontalAlignment(HAlign_Fill);
			BS->SetVerticalAlignment(VAlign_Fill);
		}

		return Frame;
	}

	/** A row whose look the row now sets itself: a frame round its content, which it styles at run time. */
	void RowFrame(FEdit& Edit, UWidget* Content)
	{
		UBorder* Frame = Wrap(Edit, Content, TEXT("RowFrame"), true);
		Frame->SetBrush(RowBrush(ERowLook::Normal));
		Frame->SetPadding(FMargin(20.0f, 12.0f));
		Frame->SetVerticalAlignment(VAlign_Center);
	}

	/** A border that used to be the row's box, emptied: the frame inside it is the box now. */
	void Hollow(UBorder* Old)
	{
		if (Old != nullptr)
		{
			Old->SetBrush(NoBrush());
			Old->SetPadding(FMargin(0.0f));
			Old->SetBrushColor(FLinearColor::White);
			Old->SetContentColorAndOpacity(FLinearColor::White);
		}
	}

	/**
	 * Removes Blueprint bindings of these properties on these widgets, and each bound function nothing else
	 * uses. The rows set these colours themselves now (USpaceMMOPanelRow), and a binding would overrule it.
	 */
	void Unbind(FEdit& Edit, const TArray<FString>& Widgets, const TArray<FName>& Properties)
	{
		TSet<FName> Freed;

		Edit.Blueprint->Bindings.RemoveAll([&](const FDelegateEditorBinding& Binding)
		{
			const bool bGoes = Widgets.Contains(Binding.ObjectName) && Properties.Contains(Binding.PropertyName);

			if (bGoes)
			{
				Freed.Add(Binding.FunctionName);

				UE_LOG(LogSpaceMMOAuthoring, Display, TEXT("Panels: %s: unbound %s.%s from %s."),
					*Edit.Blueprint->GetName(), *Binding.ObjectName, *Binding.PropertyName.ToString(),
					*Binding.FunctionName.ToString());
			}

			return bGoes;
		});

		for (const FName& Function : Freed)
		{
			const bool bStillUsed = Edit.Blueprint->Bindings.ContainsByPredicate(
				[&](const FDelegateEditorBinding& Binding) { return Binding.FunctionName == Function; });

			if (bStillUsed)
			{
				continue;
			}

			for (UEdGraph* Graph : TArray<UEdGraph*>(Edit.Blueprint->FunctionGraphs))
			{
				if (Graph != nullptr && Graph->GetFName() == Function)
				{
					FBlueprintEditorUtils::RemoveGraph(Edit.Blueprint, Graph);
				}
			}
		}
	}

	/** The glass frame every panel shares: brush, brackets, room inside, the old dark backing put away. */
	void Frame(FEdit& Edit, UBorder* Root, UOverlay* Panel, UWidget* Content, UImage* OldBacking)
	{
		if (Root != nullptr)
		{
			Root->SetBrush(Glass());
			Root->SetPadding(FMargin(12.0f));
			Root->SetBrushColor(FLinearColor::White);
		}

		if (OldBacking != nullptr)
		{
			OldBacking->SetVisibility(ESlateVisibility::Collapsed);
		}

		Brackets(Edit, Panel);

		if (UOverlaySlot* Inside = Content != nullptr ? Cast<UOverlaySlot>(Content->Slot) : nullptr)
		{
			Inside->SetPadding(FMargin(36.0f, 28.0f));
			Inside->SetHorizontalAlignment(HAlign_Fill);
			Inside->SetVerticalAlignment(VAlign_Fill);
		}
	}

	/** A prompt over a panel: heavier glass, room inside, its parts in the panels' type. */
	void Prompt(FEdit& Edit, UImage* Backing, UWidget* Box)
	{
		if (Backing != nullptr)
		{
			Backing->SetBrush(PromptGlass());
			Backing->SetColorAndOpacity(FLinearColor::White);
		}

		if (UOverlaySlot* Inside = Box != nullptr ? Cast<UOverlaySlot>(Box->Slot) : nullptr)
		{
			Inside->SetPadding(FMargin(32.0f, 26.0f));
		}

		Button(Edit.Find<UButton>(TEXT("ConfirmButton")), true, true);
		Button(Edit.Find<UButton>(TEXT("CancelButton")), false, true);
		Field(Edit.Find<UEditableTextBox>(TEXT("CurrentSliderValueText")));
		Slider(Edit.Find<USlider>(TEXT("QuantitySlider")));
	}

	void Slots(UPanelWidget* Box, const FMargin& Padding)
	{
		for (int32 Index = 0; Box != nullptr && Index < Box->GetChildrenCount(); ++Index)
		{
			if (UHorizontalBoxSlot* HB = Cast<UHorizontalBoxSlot>(Box->GetChildAt(Index)->Slot))
			{
				HB->SetPadding(Padding);
			}
		}
	}

	// ------------------------------------------------------------------------------------------------
	// Each Blueprint

	void InventoryScreen(FEdit& E)
	{
		WorldDim(E, E.Need<UCanvasPanel>(TEXT("CanvasPanel_47")));

		UBorder* Root = E.Need<UBorder>(TEXT("PanelRoot"));
		PanelSize(Root, 840.0f, 860.0f);
		Frame(E, Root, E.Need<UOverlay>(TEXT("Overlay_753")), E.Need<UWidget>(TEXT("VerticalBox_1258")),
			E.Need<UImage>(TEXT("Image_956")));

		UTextBlock* Title = E.Need<UTextBlock>(TEXT("PanelNameText"));
		Text(Title, ETextRole::Title);

		if (UVerticalBoxSlot* Under = Title != nullptr ? Cast<UVerticalBoxSlot>(Title->Slot) : nullptr)
		{
			Under->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 22.0f));
		}

		// The list takes the panel's height and scrolls in it, rather than running off the bottom.
		if (UScrollBox* List = E.Need<UScrollBox>(TEXT("InventoryScrollBox")))
		{
			if (UVerticalBoxSlot* Fill = Cast<UVerticalBoxSlot>(List->Slot))
			{
				Fill->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			}

			List->SetScrollbarThickness(FVector2D(4.0, 4.0));
		}

		// The move prompt. Its words are unchanged; its graph is untouched.
		Prompt(E, E.Need<UImage>(TEXT("Image")), E.Need<UWidget>(TEXT("PromptPanelBox")));
		Text(E.Find<UTextBlock>(TEXT("TextBlock_336")), ETextRole::Prompt);
		Text(E.Find<UTextBlock>(TEXT("ItemNameTextBlock")), ETextRole::Prompt);
		Text(E.Find<UTextBlock>(TEXT("TextBlock")), ETextRole::Note);
		Text(E.Find<UTextBlock>(TEXT("TextBlock_1")), ETextRole::Note);
	}

	void InventoryRow(FEdit& E)
	{
		Unbind(E, {TEXT("LabelText"), TEXT("AmountText"), TEXT("Border_484")},
			{TEXT("ColorAndOpacity"), TEXT("BrushColor")});

		UBorder* Old = E.Need<UBorder>(TEXT("Border_484"));
		RowFrame(E, E.Need<UWidget>(TEXT("HorizontalBox_54")));
		Hollow(Old);

		Text(E.Need<UTextBlock>(TEXT("LabelText")), ETextRole::Body);
		Figure(E.Need<UTextBlock>(TEXT("AmountText")), ETextRole::Figure, 120.0f);
	}

	void MarketRow(FEdit& E)
	{
		Unbind(E, {TEXT("Border_83")}, {TEXT("BrushColor"), TEXT("ContentColorAndOpacity")});

		UBorder* Old = E.Need<UBorder>(TEXT("Border_83"));
		RowFrame(E, E.Need<UWidget>(TEXT("HorizontalBox_88")));
		Hollow(Old);

		Text(E.Need<UTextBlock>(TEXT("NameText")), ETextRole::Body);
		Figure(E.Need<UTextBlock>(TEXT("SellText")), ETextRole::Figure);
		Figure(E.Need<UTextBlock>(TEXT("BuyText")), ETextRole::Figure);
		Figure(E.Need<UTextBlock>(TEXT("QuantityText")), ETextRole::Figure);
	}

	void BookRow(FEdit& E)
	{
		RowFrame(E, E.Need<UWidget>(TEXT("HorizontalBox_39")));

		Text(E.Need<UTextBlock>(TEXT("HeadingText")), ETextRole::Body);
		Figure(E.Need<UTextBlock>(TEXT("QuantityText")), ETextRole::Figure, QuantityWidth);
		Figure(E.Need<UTextBlock>(TEXT("PriceText")), ETextRole::Body, PriceWidth);
		Button(E.Need<UButton>(TEXT("Button_146")), false, true);

		Column(E.Need<UWidget>(TEXT("HeadingText")), true);
		Column(E.Need<UWidget>(TEXT("QuantityText")), false);
		Column(E.Need<UWidget>(TEXT("PriceText")), false, FMargin(0.0f, 0.0f, BeforeButton, 0.0f));
		Column(E.Need<UWidget>(TEXT("Button_146")), false);
	}

	void ShipRow(FEdit& E)
	{
		RowFrame(E, E.Need<UWidget>(TEXT("HorizontalBox_39")));

		Text(E.Need<UTextBlock>(TEXT("NameText")), ETextRole::Body);
		Text(E.Need<UTextBlock>(TEXT("WhereText")), ETextRole::Figure);
		Figure(E.Need<UTextBlock>(TEXT("ConditionText")), ETextRole::Figure, 100.0f);
		Text(E.Need<UTextBlock>(TEXT("RefusalText")), ETextRole::Note);
		Button(E.Need<UButton>(TEXT("SummonButton")), false, true);
	}

	void MyOrderRow(FEdit& E)
	{
		Unbind(E, {TEXT("StationText")}, {TEXT("ColorAndOpacity")});

		UBorder* Old = E.Need<UBorder>(TEXT("Border_93"));
		RowFrame(E, E.Need<UWidget>(TEXT("VerticalBox_0")));
		Hollow(Old);

		Text(E.Need<UTextBlock>(TEXT("SideText")), ETextRole::Figure);
		Text(E.Need<UTextBlock>(TEXT("ItemText")), ETextRole::Body);
		Figure(E.Need<UTextBlock>(TEXT("QuantityText")), ETextRole::Figure, QuantityWidth);
		Figure(E.Need<UTextBlock>(TEXT("PriceText")), ETextRole::Body, PriceWidth);

		E.Need<UTextBlock>(TEXT("SideText"))->SetMinDesiredWidth(SideWidth);
		Column(E.Need<UWidget>(TEXT("SideText")), false, FMargin(0.0f, 0.0f, 25.0f, 0.0f));
		Column(E.Need<UWidget>(TEXT("ItemText")), true);
		Column(E.Need<UWidget>(TEXT("QuantityText")), false);
		Column(E.Need<UWidget>(TEXT("PriceText")), false, FMargin(0.0f, 0.0f, BeforeButton, 0.0f));
		Column(E.Need<UWidget>(TEXT("CancelButton")), false);
		Text(E.Find<UTextBlock>(TEXT("TextBlock_610")), ETextRole::Note);
		Text(E.Need<UTextBlock>(TEXT("StationText")), ETextRole::Note);
		Button(E.Need<UButton>(TEXT("CancelButton")), false, true);
	}

	void TextRow(FEdit& E)
	{
		Text(E.Need<UTextBlock>(TEXT("LineText")), ETextRole::Body);
	}

	void SkillRow(FEdit& E)
	{
		UTextBlock* Name = E.Need<UTextBlock>(TEXT("NameText"));
		UTextBlock* Level = E.Need<UTextBlock>(TEXT("LevelText"));
		UTextBlock* Xp = E.Need<UTextBlock>(TEXT("XpText"));
		UTextBlock* ToNext = E.Need<UTextBlock>(TEXT("ToNextText"));
		UProgressBar* Bar = E.Need<UProgressBar>(TEXT("ProgressBar"));

		if (E.Problems > 0)
		{
			return;
		}

		// Rebuilt to the approved layout -- name and level, the bar, then XP and what is left to the next
		// level -- with the same parts moved rather than replaced, so nothing bound to them changes.
		// Nothing in this Blueprint's graph uses its layout.
		if (E.Find<UBorder>(TEXT("RowFrame")) == nullptr)
		{
			UWidget* OldRoot = E.Tree()->RootWidget;

			UBorder* Frame = E.Make<UBorder>(TEXT("RowFrame"), true);
			UVerticalBox* Body = E.Make<UVerticalBox>(TEXT("SkillBody"), false);
			UHorizontalBox* Top = E.Make<UHorizontalBox>(TEXT("SkillTop"), false);
			UHorizontalBox* Bottom = E.Make<UHorizontalBox>(TEXT("SkillBottom"), false);
			USizeBox* BarSize = E.Make<USizeBox>(TEXT("SkillBarSize"), false);

			for (UWidget* Part : {static_cast<UWidget*>(Name), static_cast<UWidget*>(Level),
					 static_cast<UWidget*>(Xp), static_cast<UWidget*>(ToNext), static_cast<UWidget*>(Bar)})
			{
				Part->RemoveFromParent();
			}

			E.Tree()->RootWidget = Frame;
			Frame->SetContent(Body);

			Body->AddChildToVerticalBox(Top);
			UHorizontalBoxSlot* NameSlot = Top->AddChildToHorizontalBox(Name);
			NameSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			Top->AddChildToHorizontalBox(Level);

			UVerticalBoxSlot* BarSlot = Body->AddChildToVerticalBox(BarSize);
			BarSlot->SetPadding(FMargin(0.0f, 10.0f, 0.0f, 0.0f));
			BarSize->SetHeightOverride(6.0f);
			BarSize->AddChild(Bar);

			UVerticalBoxSlot* BottomSlot = Body->AddChildToVerticalBox(Bottom);
			BottomSlot->SetPadding(FMargin(0.0f, 8.0f, 0.0f, 0.0f));
			UHorizontalBoxSlot* XpSlot = Bottom->AddChildToHorizontalBox(Xp);
			XpSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			Bottom->AddChildToHorizontalBox(ToNext);

			if (OldRoot != nullptr && OldRoot != Frame)
			{
				E.Remove(OldRoot);
			}
		}

		if (UBorder* Frame = E.Find<UBorder>(TEXT("RowFrame")))
		{
			Frame->SetBrush(RowBrush(ERowLook::Normal));
			Frame->SetPadding(FMargin(22.0f, 16.0f, 22.0f, 14.0f));
		}

		Text(Name, ETextRole::Body);
		Text(Level, ETextRole::Body);
		Text(Xp, ETextRole::Note);
		Text(ToNext, ETextRole::Note);
	}

	void SkillsScreen(FEdit& E)
	{
		WorldDim(E, E.Need<UCanvasPanel>(TEXT("CanvasPanel_43")));

		if (UBorder* Shadow = E.Need<UBorder>(TEXT("Border_127")))
		{
			Shadow->SetVisibility(ESlateVisibility::Collapsed);
		}

		UBorder* Root = E.Need<UBorder>(TEXT("Border_233"));
		UWidget* Content = E.Need<UWidget>(TEXT("VerticalBox_0"));

		if (Root == nullptr || Content == nullptr)
		{
			return;
		}

		PanelSize(Root, 760.0f, 860.0f);

		// An overlay inside the glass, for the brackets: the panel was a border straight onto its list.
		bool bExisted = false;
		UOverlay* Panel = E.Make<UOverlay>(TEXT("Panel"), false, &bExisted);

		if (!bExisted)
		{
			Content->RemoveFromParent();
			Root->SetContent(Panel);
			Panel->AddChildToOverlay(Content);
		}

		Frame(E, Root, Panel, Content, nullptr);

		UTextBlock* Title = E.Need<UTextBlock>(TEXT("SkillsHeader"));
		Text(Title, ETextRole::Title);

		if (UVerticalBoxSlot* Under = Title != nullptr ? Cast<UVerticalBoxSlot>(Title->Slot) : nullptr)
		{
			Under->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 22.0f));
		}
	}

	void StartBar(FEdit& E);

	void StationOverlay(FEdit& E)
	{
		WorldDim(E, E.Need<UCanvasPanel>(TEXT("CanvasPanel_47")));

		UBorder* Root = E.Need<UBorder>(TEXT("PanelRoot"));
		PanelSize(Root, 840.0f, 860.0f);
		Frame(E, Root, E.Need<UOverlay>(TEXT("Overlay_753")), E.Need<UWidget>(TEXT("VerticalBox_1258")),
			E.Need<UImage>(TEXT("Image_956")));

		UTextBlock* Title = E.Need<UTextBlock>(TEXT("StationNameText"));
		Text(Title, ETextRole::Title);

		if (UVerticalBoxSlot* Under = Title != nullptr ? Cast<UVerticalBoxSlot>(Title->Slot) : nullptr)
		{
			Under->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 18.0f));
		}

		// The tab strip: the five capitalised words become a frame each with its number key, outlined
		// while showing (USpaceMMOStationOverlay::StyleTabs). Their colour bindings go with them.
		Unbind(E, {TEXT("TextBlock_155"), TEXT("TextBlock_267"), TEXT("TextBlock_346"), TEXT("TextBlock_124"),
				TEXT("TextBlock_238")}, {TEXT("ColorAndOpacity")});

		if (UHorizontalBox* Strip = E.Need<UHorizontalBox>(TEXT("TabHeaders")))
		{
			if (E.Find<UBorder>(TEXT("MarketTabFrame")) == nullptr)
			{
				for (const TCHAR* Old : {TEXT("TextBlock_155"), TEXT("TextBlock_267"), TEXT("TextBlock_346"),
						 TEXT("TextBlock_124"), TEXT("TextBlock_238")})
				{
					E.Remove(E.Find<UWidget>(Old));
				}

				struct FTab
				{
					const TCHAR* Part;
					const TCHAR* Key;
					const TCHAR* Label;
				};

				const FTab Tabs[] = {
					{TEXT("Market"), TEXT("1"), TEXT("Market")},
					{TEXT("Industry"), TEXT("2"), TEXT("Industry")},
					{TEXT("Quests"), TEXT("3"), TEXT("Quests")},
					{TEXT("MyOrders"), TEXT("4"), TEXT("My orders")},
					{TEXT("Ships"), TEXT("5"), TEXT("Ships")},
				};

				for (const FTab& Tab : Tabs)
				{
					UBorder* TabFrame = E.Make<UBorder>(*FString::Printf(TEXT("%sTabFrame"), Tab.Part), true);
					TabFrame->SetBrush(TabBrush(false));
					TabFrame->SetPadding(FMargin(16.0f, 9.0f));

					UHorizontalBox* Inside = E.Make<UHorizontalBox>(*FString::Printf(TEXT("%sTabRow"), Tab.Part), false);
					TabFrame->SetContent(Inside);

					UTextBlock* Key = E.Make<UTextBlock>(*FString::Printf(TEXT("%sTabKey"), Tab.Part), false);
					Key->SetText(FText::FromString(Tab.Key));
					UHorizontalBoxSlot* KeySlot = Inside->AddChildToHorizontalBox(Key);
					KeySlot->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
					KeySlot->SetVerticalAlignment(VAlign_Center);

					UTextBlock* Label = E.Make<UTextBlock>(*FString::Printf(TEXT("%sTabText"), Tab.Part), true);
					Label->SetText(FText::FromString(Tab.Label));
					Inside->AddChildToHorizontalBox(Label)->SetVerticalAlignment(VAlign_Center);

					UHorizontalBoxSlot* At = Strip->AddChildToHorizontalBox(TabFrame);
					At->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
					At->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
				}
			}

			if (UVerticalBoxSlot* Under = Cast<UVerticalBoxSlot>(Strip->Slot))
			{
				Under->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 20.0f));
			}

			for (const TCHAR* Part : {TEXT("Market"), TEXT("Industry"), TEXT("Quests"), TEXT("MyOrders"), TEXT("Ships")})
			{
				Text(E.Find<UTextBlock>(*FString::Printf(TEXT("%sTabKey"), Part)), ETextRole::Key);
				Text(E.Find<UTextBlock>(*FString::Printf(TEXT("%sTabText"), Part)), ETextRole::Button, TextSecondary());
			}
		}

		// Market: the search field, the column headings in Joe's words, the order buttons, the book's heading.
		if (UBorder* Search = E.Find<UBorder>(TEXT("Border_0")))
		{
			Search->SetBrush(FieldBrush(false));
			Search->SetPadding(FMargin(14.0f, 8.0f));
		}

		Field(E.Find<UEditableText>(TEXT("SearchBox")));

		if (UHorizontalBox* Columns = E.Find<UHorizontalBox>(TEXT("HorizontalBox_193")))
		{
			if (UScrollBoxSlot* Above = Cast<UScrollBoxSlot>(Columns->Slot))
			{
				Above->SetPadding(FMargin(20.0f, 14.0f, 20.0f, 8.0f));
			}

			const TPair<const TCHAR*, const TCHAR*> Headings[] = {
				{TEXT("TextBlock_316"), TEXT("SELLING AT")},
				{TEXT("TextBlock_521"), TEXT("BUYING AT")},
				{TEXT("TextBlock_730"), TEXT("FOR SALE")},
			};

			if (UTextBlock* Item = E.Find<UTextBlock>(TEXT("TextBlock_162")))
			{
				Item->SetText(FText::FromString(TEXT("ITEM")));
				Text(Item, ETextRole::Column);

				if (UHorizontalBoxSlot* ItemSlot = Cast<UHorizontalBoxSlot>(Item->Slot))
				{
					ItemSlot->SetPadding(FMargin(0.0f));
					ItemSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
					ItemSlot->SetHorizontalAlignment(HAlign_Left);
				}
			}

			for (const auto& Heading : Headings)
			{
				if (UTextBlock* Column = E.Find<UTextBlock>(Heading.Key))
				{
					Column->SetText(FText::FromString(Heading.Value));
					Figure(Column, ETextRole::Column);

					if (UHorizontalBoxSlot* ColumnSlot = Cast<UHorizontalBoxSlot>(Column->Slot))
					{
						ColumnSlot->SetPadding(FMargin(0.0f, 0.0f, 5.0f, 0.0f));
					}
				}
			}
		}

		Button(E.Find<UButton>(TEXT("SellBtn")), false);
		Button(E.Find<UButton>(TEXT("BuyBtn")), false);
		Text(E.Find<UTextBlock>(TEXT("TextBlock_329")), ETextRole::Group);

		// My orders and ships: their column headings and footers.
		for (const TCHAR* Header : {TEXT("Border_1"), TEXT("Border")})
		{
			if (UBorder* Band = E.Find<UBorder>(Header))
			{
				Band->SetBrush(NoBrush());
				Band->SetPadding(FMargin(20.0f, 6.0f, 20.0f, 8.0f));
			}
		}

		for (const TCHAR* Column : {TEXT("SideText"), TEXT("ItemText"), TEXT("ShipNameText"), TEXT("ShipLocationText"),
				 TEXT("ShipConditionText")})
		{
			Text(E.Find<UTextBlock>(Column), ETextRole::Column);
		}

		// Over the order rows' columns, laid out the way MyOrderRow lays the rows out.
		Figure(E.Find<UTextBlock>(TEXT("QuantityText")), ETextRole::Column, QuantityWidth);
		Figure(E.Find<UTextBlock>(TEXT("PriceText")), ETextRole::Column, PriceWidth);

		if (UTextBlock* Side = E.Find<UTextBlock>(TEXT("SideText")))
		{
			Side->SetMinDesiredWidth(SideWidth);
			Column(Side, false, FMargin(0.0f, 0.0f, 25.0f, 0.0f));
		}

		Column(E.Find<UWidget>(TEXT("ItemText")), true);
		Column(E.Find<UWidget>(TEXT("QuantityText")), false);
		Column(E.Find<UWidget>(TEXT("PriceText")), false, FMargin(0.0f, 0.0f, BeforeButton + CancelButtonWidth, 0.0f));

		// The tabs' lists take the rest of the panel and scroll in it, rather than running off its bottom.
		if (UWidget* Lists = E.Find<UWidget>(TEXT("Overlay_206")))
		{
			if (UVerticalBoxSlot* Fill = Cast<UVerticalBoxSlot>(Lists->Slot))
			{
				Fill->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			}
		}

		Text(E.Find<UTextBlock>(TEXT("MyOrdersFooterText")), ETextRole::Note);
		Text(E.Find<UTextBlock>(TEXT("ShipsFooterText")), ETextRole::Note);

		StartBar(E);

		// The order prompt. Its words are unchanged; its graph is untouched.
		Prompt(E, E.Need<UImage>(TEXT("Image")), E.Need<UWidget>(TEXT("PromptPanelBox")));
		Text(E.Find<UTextBlock>(TEXT("TitleText")), ETextRole::Prompt);

		for (const TCHAR* Label : {TEXT("TextBlock_678"), TEXT("TextBlock_872"), TEXT("TextBlock_541"), TEXT("TextBlock"),
				 TEXT("TextBlock_1")})
		{
			Text(E.Find<UTextBlock>(Label), ETextRole::Note);
		}

		Field(E.Find<UEditableTextBox>(TEXT("PriceInput")));
		Button(E.Find<UButton>(TEXT("MatchMarketButton")), false, true);
		Button(E.Find<UButton>(TEXT("GuaranteedButton")), false, true);
	}

	/**
	 * The Industry tab's Start bar, at the foot of the tab lists (task 173): what the count comes to,
	 * a − count + stepper, and Start. Joe's mock of 3 October. The overlay shows it on the Industry tab
	 * only, and the industry list keeps clear of it.
	 */
	void StartBar(FEdit& E)
	{
		UOverlay* Lists = E.Find<UOverlay>(TEXT("Overlay_206"));

		if (Lists == nullptr)
		{
			UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Panels: the station overlay has no Overlay_206 to put Start in."));

			++E.Problems;

			return;
		}

		bool bHad = false;
		UHorizontalBox* Bar = E.Make<UHorizontalBox>(TEXT("StartBar"), true, &bHad);

		if (!bHad)
		{
			UOverlaySlot* At = Lists->AddChildToOverlay(Bar);
			At->SetHorizontalAlignment(HAlign_Fill);
			At->SetVerticalAlignment(VAlign_Bottom);

			UTextBlock* Note = E.Make<UTextBlock>(TEXT("StartNote"), true);
			Note->SetJustification(ETextJustify::Right);
			Note->SetAutoWrapText(true);
			UHorizontalBoxSlot* NoteSlot = Bar->AddChildToHorizontalBox(Note);
			NoteSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			NoteSlot->SetVerticalAlignment(VAlign_Center);
			NoteSlot->SetPadding(FMargin(0.0f, 0.0f, 14.0f, 0.0f));

			UBorder* Runs = E.Make<UBorder>(TEXT("RunsBox"), false);
			Runs->SetPadding(FMargin(1.0f));
			UHorizontalBoxSlot* RunsSlot = Bar->AddChildToHorizontalBox(Runs);
			RunsSlot->SetVerticalAlignment(VAlign_Center);
			RunsSlot->SetPadding(FMargin(0.0f, 0.0f, 14.0f, 0.0f));

			UHorizontalBox* Stepper = E.Make<UHorizontalBox>(TEXT("RunsStepper"), false);
			Runs->SetContent(Stepper);

			auto Step = [&E, Stepper](const TCHAR* Name, const TCHAR* LabelName, const TCHAR* Label)
			{
				UButton* Made = E.Make<UButton>(Name, true);
				UTextBlock* Text = E.Make<UTextBlock>(LabelName, false);
				Text->SetText(FText::FromString(Label));
				Made->SetContent(Text);
				Stepper->AddChildToHorizontalBox(Made)->SetVerticalAlignment(VAlign_Fill);
			};

			Step(TEXT("RunsLess"), TEXT("RunsLessText"), TEXT("−"));

			UTextBlock* Count = E.Make<UTextBlock>(TEXT("RunsText"), true);
			Count->SetText(FText::FromString(TEXT("1")));
			Count->SetJustification(ETextJustify::Center);
			Count->SetMinDesiredWidth(56.0f);
			UHorizontalBoxSlot* CountSlot = Stepper->AddChildToHorizontalBox(Count);
			CountSlot->SetVerticalAlignment(VAlign_Center);

			Step(TEXT("RunsMore"), TEXT("RunsMoreText"), TEXT("+"));

			UButton* Start = E.Make<UButton>(TEXT("StartButton"), true);
			UTextBlock* StartLabel = E.Make<UTextBlock>(TEXT("StartText"), true);
			StartLabel->SetText(FText::FromString(TEXT("Start")));
			Start->SetContent(StartLabel);
			Bar->AddChildToHorizontalBox(Start)->SetVerticalAlignment(VAlign_Center);
		}

		// Styled every run, so a change to the style reaches it without rebuilding anything. Wrapped, so a
		// long shortfall stays inside the panel instead of running out of its left edge (Joe, 3 October).
		Text(E.Find<UTextBlock>(TEXT("StartNote")), ETextRole::Note);

		if (UTextBlock* Note = E.Find<UTextBlock>(TEXT("StartNote")))
		{
			Note->SetAutoWrapText(true);
		}
		Text(E.Find<UTextBlock>(TEXT("RunsText")), ETextRole::Body);

		if (UBorder* Runs = E.Find<UBorder>(TEXT("RunsBox")))
		{
			Runs->SetBrush(Rounded(White(0.02f), White(0.30f), 1.0f, RowCorners()));
		}

		for (const TCHAR* Name : {TEXT("RunsLess"), TEXT("RunsMore")})
		{
			if (UButton* Made = E.Find<UButton>(Name))
			{
				Made->SetStyle(StepperButtonStyle());

				if (UTextBlock* Label = Cast<UTextBlock>(Made->GetChildAt(0)))
				{
					Text(Label, ETextRole::Body);
				}
			}
		}

		// The primary action on the tab, so ice.
		Button(E.Find<UButton>(TEXT("StartButton")), true);

		// The industry list stops short of the bar rather than scrolling under it. Both lists fill the tab:
		// they were left-aligned, which was invisible while they held lines of text and made every row
		// only as wide as its words once they held boxes.
		if (UWidget* Scroll = E.Find<UWidget>(TEXT("IndustryRowsScrollBox")))
		{
			if (UOverlaySlot* Above = Cast<UOverlaySlot>(Scroll->Slot))
			{
				Above->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 64.0f));
				Above->SetHorizontalAlignment(HAlign_Fill);
				Above->SetVerticalAlignment(VAlign_Fill);
			}
		}

		if (UWidget* Scroll = E.Find<UWidget>(TEXT("QuestRowsScrollBox")))
		{
			if (UOverlaySlot* Fill = Cast<UOverlaySlot>(Scroll->Slot))
			{
				Fill->SetHorizontalAlignment(HAlign_Fill);
				Fill->SetVerticalAlignment(VAlign_Fill);
			}
		}
		else
		{
			UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Panels: the station overlay has no IndustryRowsScrollBox."));

			++E.Problems;
		}
	}

	/**
	 * The station overlay's row classes for Industry and Quests, on the compiled class's defaults -- what
	 * Class Defaults edits. The rows are built by SpaceMMOBuildMenus, so it must have run first.
	 */
	int32 StationOverlayDefaults(UWidgetBlueprint* Blueprint)
	{
		int32 Problems = 0;

		const TPair<const TCHAR*, const TCHAR*> Rows[] = {
			{TEXT("RecipeRowClass"), TEXT("/Game/UI/WBP_RecipeRow.WBP_RecipeRow_C")},
			{TEXT("JobRowClass"), TEXT("/Game/UI/WBP_JobRow.WBP_JobRow_C")},
			{TEXT("QuestRowClass"), TEXT("/Game/UI/WBP_QuestRow.WBP_QuestRow_C")},
		};

		for (const auto& Row : Rows)
		{
			UClass* RowClass = LoadObject<UClass>(nullptr, Row.Value);
			FClassProperty* Property = FindFProperty<FClassProperty>(Blueprint->GeneratedClass, Row.Key);

			if (RowClass == nullptr || Property == nullptr)
			{
				UE_LOG(LogSpaceMMOAuthoring, Error,
					TEXT("Panels: could not set the station overlay's %s to %s; run SpaceMMOBuildMenus first."),
					Row.Key, Row.Value);

				++Problems;

				continue;
			}

			Property->SetObjectPropertyValue_InContainer(Blueprint->GeneratedClass->GetDefaultObject(), RowClass);
		}

		return Problems;
	}

	// ------------------------------------------------------------------------------------------------
	// Round two (task 173): the readouts, the deposit prompt, the messages and the sign-in screen.
	// Joe approved the mock on 3 October.

	/** Takes a widget out of whatever holds it, so it can be put somewhere else. Its variable stays. */
	void Detach(UWidget* Widget)
	{
		if (Widget != nullptr && Widget->GetParent() != nullptr)
		{
			Widget->GetParent()->RemoveChild(Widget);
		}
	}

	/** Moves every child of one panel into a vertical box, keeping each one's alignment and padding. */
	void MoveChildren(UPanelWidget* From, UVerticalBox* To)
	{
		while (From->GetChildrenCount() > 0)
		{
			UWidget* Child = From->GetChildAt(0);

			FMargin Padding;
			EHorizontalAlignment H = HAlign_Fill;
			EVerticalAlignment V = VAlign_Fill;

			if (const UVerticalBoxSlot* Was = Cast<UVerticalBoxSlot>(Child->Slot))
			{
				Padding = Was->GetPadding();
				H = Was->GetHorizontalAlignment();
				V = Was->GetVerticalAlignment();
			}

			From->RemoveChild(Child);

			UVerticalBoxSlot* Now = To->AddChildToVerticalBox(Child);
			Now->SetPadding(Padding);
			Now->SetHorizontalAlignment(H);
			Now->SetVerticalAlignment(V);
		}
	}

	/** The hairline between a readout's figures and its station line. */
	void Rule(FEdit& E, UVerticalBox* Body, UWidget* Before, const TCHAR* Name)
	{
		bool bHad = false;
		UImage* Line = E.Make<UImage>(Name, true, &bHad);
		Line->bIsVariable = true;
		Line->SetBrush(Solid(White(0.10f), FVector2D(1.0, 1.0)));

		if (!bHad && Body != nullptr && Before != nullptr)
		{
			Body->InsertChildAt(Body->GetChildIndex(Before), Line);
		}

		if (UVerticalBoxSlot* At = Cast<UVerticalBoxSlot>(Line->Slot))
		{
			At->SetPadding(FMargin(0.0f, 10.0f, 0.0f, 8.0f));
			At->SetHorizontalAlignment(HAlign_Fill);
		}
	}

	/**
	 * A readout's card: glass round its lines, a fixed width, top right of the screen. The lines' box
	 * stays the readout's; the card goes round it.
	 */
	void ReadoutCard(FEdit& E, UWidget* Body)
	{
		bool bHad = false;
		UBorder* Card = E.Make<UBorder>(TEXT("ReadoutCard"), true, &bHad);

		if (!bHad && Body != nullptr)
		{
			UCanvasPanel* Canvas = Cast<UCanvasPanel>(Body->GetParent());

			if (Canvas == nullptr)
			{
				UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Panels: %s's readout is not on its canvas."), *E.Blueprint->GetName());

				++E.Problems;

				return;
			}

			const int32 Index = Canvas->GetChildIndex(Body);
			Canvas->RemoveChild(Body);
			Canvas->InsertChildAt(Index, Card);

			USizeBox* Width = E.Make<USizeBox>(TEXT("ReadoutWidth"), false);
			Card->SetContent(Width);
			Width->SetContent(Body);
		}

		Card->SetBrush(Glass());
		Card->SetPadding(FMargin(22.0f, 18.0f, 22.0f, 16.0f));

		if (USizeBox* Width = E.Find<USizeBox>(TEXT("ReadoutWidth")))
		{
			Width->SetMinDesiredWidth(336.0f);
		}

		if (UCanvasPanelSlot* At = Cast<UCanvasPanelSlot>(Card->Slot))
		{
			At->SetAnchors(FAnchors(1.0f, 0.0f));
			At->SetAlignment(FVector2D(1.0, 0.0));
			At->SetAutoSize(true);
			At->SetPosition(FVector2D(-40.0, 36.0));
		}
	}

	/** A label on the left in column capitals, its figure on the right in body type. */
	void ReadoutLine(FEdit& E, const TCHAR* Box, const TCHAR* Label, const TCHAR* Word, const TCHAR* Value)
	{
		if (UTextBlock* Name = E.Find<UTextBlock>(Label))
		{
			Name->SetText(FText::FromString(Word));
			Text(Name, ETextRole::Column);
			Column(Name, true);

			if (UHorizontalBoxSlot* At = Cast<UHorizontalBoxSlot>(Name->Slot))
			{
				At->SetVerticalAlignment(VAlign_Center);
			}
		}

		if (UTextBlock* Figure = E.Find<UTextBlock>(Value))
		{
			Text(Figure, ETextRole::Body);
			Figure->SetJustification(ETextJustify::Right);
			Column(Figure, false);
		}

		if (UWidget* Line = E.Find<UWidget>(Box))
		{
			if (UVerticalBoxSlot* At = Cast<UVerticalBoxSlot>(Line->Slot))
			{
				At->SetPadding(FMargin(0.0f, 3.0f));
				At->SetHorizontalAlignment(HAlign_Fill);
			}
		}
	}

	void FlightReadout(FEdit& E)
	{
		UVerticalBox* Body = E.Need<UVerticalBox>(TEXT("VerticalBox_148"));
		ReadoutCard(E, Body);

		// The header: "IN FLIGHT", and where the ship is as a chip beside it.
		UHorizontalBox* Head = E.Need<UHorizontalBox>(TEXT("FlightModeHeaderBox"));

		if (UTextBlock* Title = E.Need<UTextBlock>(TEXT("FlightHeaderText")))
		{
			Title->SetText(FText::FromString(TEXT("In flight")));
			Text(Title, ETextRole::Group);
			Column(Title, true);

			if (UHorizontalBoxSlot* At = Cast<UHorizontalBoxSlot>(Title->Slot))
			{
				At->SetVerticalAlignment(VAlign_Center);
			}
		}

		if (UVerticalBoxSlot* Under = Head != nullptr ? Cast<UVerticalBoxSlot>(Head->Slot) : nullptr)
		{
			Under->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 10.0f));
			Under->SetHorizontalAlignment(HAlign_Fill);
		}

		bool bHad = false;
		UBorder* Chip = E.Make<UBorder>(TEXT("ProximityChip"), false, &bHad);
		UTextBlock* Proximity = E.Need<UTextBlock>(TEXT("ProximityText"));

		if (!bHad && Head != nullptr && Proximity != nullptr)
		{
			Detach(Proximity);
			Chip->SetContent(Proximity);
			Head->AddChildToHorizontalBox(Chip)->SetVerticalAlignment(VAlign_Center);
		}

		Chip->SetBrush(Rounded(White(0.0f), White(0.22f), 1.0f, RowCorners()));
		Chip->SetPadding(FMargin(8.0f, 2.0f));
		Text(Proximity, ETextRole::Key);
		Proximity->SetTextTransformPolicy(ETextTransformPolicy::ToUpper);

		if (UWidget* Old = E.Find<UWidget>(TEXT("ProximityBox")))
		{
			Old->SetVisibility(ESlateVisibility::Collapsed);
		}

		ReadoutLine(E, TEXT("AltitudeBox"), TEXT("AltitudeLabel"), TEXT("Altitude"), TEXT("AltitudeText"));
		ReadoutLine(E, TEXT("SpeedBox"), TEXT("SpeedLabel"), TEXT("Speed"), TEXT("SpeedText"));
		ReadoutLine(E, TEXT("OrbitalBox"), TEXT("OrbitalSpeedLabel"), TEXT("Orbital"), TEXT("OrbitalText"));

		// The station line under a rule, its label gone: the name says what it is.
		UWidget* StationLine = E.Need<UWidget>(TEXT("HorizontalBox_345"));
		Rule(E, Body, StationLine, TEXT("ReadoutRule"));

		if (UWidget* Label = E.Find<UWidget>(TEXT("StationLabel")))
		{
			Label->SetVisibility(ESlateVisibility::Collapsed);
		}

		Text(E.Need<UTextBlock>(TEXT("StationText")), ETextRole::Body);

		// The debug lines, quiet, and shown only with the ship's flight debug (USpaceMMOFlightReadout).
		for (const TCHAR* Quiet : {TEXT("SystemPositionLabel"), TEXT("SystemPositionText"), TEXT("DebugLabel"), TEXT("DebugText")})
		{
			Text(E.Find<UTextBlock>(Quiet), ETextRole::Note);
		}
	}

	void OnFootReadout(FEdit& E)
	{
		UVerticalBox* Body = E.Need<UVerticalBox>(TEXT("VerticalBox_77"));
		ReadoutCard(E, Body);

		if (UTextBlock* Title = E.Need<UTextBlock>(TEXT("OnFootHeaderBox")))
		{
			Title->SetText(FText::FromString(TEXT("On foot")));
			Text(Title, ETextRole::Group);

			if (UVerticalBoxSlot* Under = Cast<UVerticalBoxSlot>(Title->Slot))
			{
				Under->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
			}
		}

		Text(E.Need<UTextBlock>(TEXT("NameText")), ETextRole::Prompt);

		// Credits as a line: the word on the left, the balance on the right, hidden together.
		bool bHad = false;
		UHorizontalBox* Line = E.Make<UHorizontalBox>(TEXT("CreditsLine"), true, &bHad);
		UTextBlock* Credits = E.Need<UTextBlock>(TEXT("CreditsText"));

		if (!bHad && Body != nullptr && Credits != nullptr)
		{
			const int32 Index = Body->GetChildIndex(Credits);
			Detach(Credits);
			Body->InsertChildAt(Index, Line);

			UTextBlock* Word = E.Make<UTextBlock>(TEXT("CreditsLabel"), false);
			Word->SetText(FText::FromString(TEXT("Credits")));
			Line->AddChildToHorizontalBox(Word);
			Line->AddChildToHorizontalBox(Credits);
		}

		if (UVerticalBoxSlot* At = Cast<UVerticalBoxSlot>(Line->Slot))
		{
			At->SetPadding(FMargin(0.0f, 6.0f, 0.0f, 0.0f));
			At->SetHorizontalAlignment(HAlign_Fill);
		}

		if (UTextBlock* Word = E.Find<UTextBlock>(TEXT("CreditsLabel")))
		{
			Text(Word, ETextRole::Column);
			Column(Word, true);

			if (UHorizontalBoxSlot* At = Cast<UHorizontalBoxSlot>(Word->Slot))
			{
				At->SetVerticalAlignment(VAlign_Center);
			}
		}

		Text(Credits, ETextRole::Body);
		Credits->SetJustification(ETextJustify::Right);
		Column(Credits, false);

		UTextBlock* Station = E.Need<UTextBlock>(TEXT("StationText"));
		Rule(E, Body, Station, TEXT("ReadoutRule"));
		Text(Station, ETextRole::Body);
	}

	void DepositPrompt(FEdit& E)
	{
		// The key and its words are coloured by the prompt itself now (USpaceMMODepositPrompt).
		Unbind(E, {TEXT("GatherKeyText"), TEXT("GatherTextLabel")}, {TEXT("ColorAndOpacity")});

		// The card goes inside PromptRoot rather than round it: PromptRoot's visibility is bound, and a
		// card outside it would stay on screen, empty, whenever the prompt was hidden.
		UVerticalBox* Root = E.Need<UVerticalBox>(TEXT("PromptRoot"));

		bool bHad = false;
		UBorder* Card = E.Make<UBorder>(TEXT("PromptCard"), true, &bHad);

		if (!bHad && Root != nullptr)
		{
			UVerticalBox* Body = E.Make<UVerticalBox>(TEXT("PromptBody"), false);
			MoveChildren(Root, Body);
			Root->AddChildToVerticalBox(Card);
			Card->SetContent(Body);
		}

		Card->SetBrush(PromptGlass());
		Card->SetPadding(FMargin(24.0f, 14.0f, 24.0f, 16.0f));

		Text(E.Need<UTextBlock>(TEXT("ItemNameText")), ETextRole::Prompt);
		Text(E.Need<UTextBlock>(TEXT("RequirementText")), ETextRole::Note);
		Text(E.Need<UTextBlock>(TEXT("ToolText")), ETextRole::Note);

		// What stops you is red, as errors are everywhere else; it was amber (Joe, 3 October).
		Text(E.Need<UTextBlock>(TEXT("LevelBlockerText")), ETextRole::Note, SpaceMMO::Style::ErrorRed());
		Text(E.Need<UTextBlock>(TEXT("ToolBlockerText")), ETextRole::Note, SpaceMMO::Style::ErrorRed());

		// The gather line: the key in a keycap rather than brackets.
		for (const TCHAR* Bracket : {TEXT("LeftBracket"), TEXT("RightBracket")})
		{
			if (UWidget* Old = E.Find<UWidget>(Bracket))
			{
				Old->SetVisibility(ESlateVisibility::Collapsed);
			}
		}

		UBorder* Cap = Wrap(E, E.Need<UTextBlock>(TEXT("GatherKeyText")), TEXT("KeyCap"), false);
		Cap->SetBrush(Rounded(White(0.05f), White(0.35f), 1.0f, RowCorners()));
		Cap->SetPadding(FMargin(7.0f, 2.0f));

		if (UHorizontalBoxSlot* At = Cast<UHorizontalBoxSlot>(Cap->Slot))
		{
			At->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
			At->SetVerticalAlignment(VAlign_Center);
		}

		if (UTextBlock* Words = E.Need<UTextBlock>(TEXT("GatherTextLabel")))
		{
			Words->SetText(FText::FromString(TEXT("to gather")));

			if (UHorizontalBoxSlot* At = Cast<UHorizontalBoxSlot>(Words->Slot))
			{
				At->SetVerticalAlignment(VAlign_Center);
			}
		}

		if (UWidget* Gather = E.Find<UWidget>(TEXT("GatherTextBox")))
		{
			if (UVerticalBoxSlot* At = Cast<UVerticalBoxSlot>(Gather->Slot))
			{
				At->SetPadding(FMargin(0.0f, 10.0f, 0.0f, 0.0f));
			}
		}
	}

	void TransientMessageRow(FEdit& E)
	{
		// The tone is the strip's edge now, set by the row (USpaceMMOTransientMessageRow).
		Unbind(E, {TEXT("MessageText")}, {TEXT("ColorAndOpacity")});

		UTextBlock* Message = E.Need<UTextBlock>(TEXT("MessageText"));

		bool bHad = false;
		UBorder* Frame = E.Make<UBorder>(TEXT("MessageFrame"), false, &bHad);

		if (!bHad && Message != nullptr)
		{
			UHorizontalBox* Strip = E.Make<UHorizontalBox>(TEXT("MessageStrip"), false);
			UImage* Edge = E.Make<UImage>(TEXT("ToneEdge"), true);

			Detach(Message);
			E.Tree()->RootWidget = Frame;
			Frame->SetContent(Strip);

			Strip->AddChildToHorizontalBox(Edge)->SetVerticalAlignment(VAlign_Fill);

			UHorizontalBoxSlot* At = Strip->AddChildToHorizontalBox(Message);
			At->SetVerticalAlignment(VAlign_Center);
			At->SetPadding(FMargin(14.0f, 8.0f, 18.0f, 8.0f));
		}

		Frame->SetBrush(Rounded(Srgb(10, 15, 22, 0.80f), White(0.16f), 1.0f, FVector4(0.0, 4.0, 4.0, 0.0)));
		Frame->SetPadding(FMargin(0.0f));

		if (UImage* Edge = E.Find<UImage>(TEXT("ToneEdge")))
		{
			Edge->SetBrush(Solid(FLinearColor::White, FVector2D(3.0, 3.0)));
			Edge->SetColorAndOpacity(SpaceMMO::Style::Ice());
		}

		Text(Message, ETextRole::Body);
	}

	void LoginScreen(FEdit& E)
	{
		// Joe's backdrop stays; the form moves into a glass panel over a light dim, in the menus' type.
		if (UBorder* Whole = E.Need<UBorder>(TEXT("Border_245")))
		{
			Whole->SetBrush(Solid(FLinearColor(0.0f, 0.0f, 0.0f, 0.25f), FVector2D(32.0, 32.0)));
			Whole->SetBrushColor(FLinearColor::White);
			Whole->SetHorizontalAlignment(HAlign_Center);
			Whole->SetVerticalAlignment(VAlign_Center);
		}

		UVerticalBox* Column0 = E.Need<UVerticalBox>(TEXT("VerticalBox_0"));

		if (UTextBlock* Title = E.Need<UTextBlock>(TEXT("TextBlock_464")))
		{
			FSlateFontInfo Font = Title->GetFont();
			Font.TypefaceFontName = TEXT("Regular");
			Font.Size = 64;
			Font.LetterSpacing = 200;
			Title->SetFont(Font);
			Title->SetColorAndOpacity(FSlateColor(SpaceMMO::Style::TextPrimary()));
			Title->SetTextTransformPolicy(ETextTransformPolicy::ToUpper);
			Title->SetShadowOffset(FVector2D(0.0, 2.0));
			Title->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.55f));

			if (UVerticalBoxSlot* Under = Cast<UVerticalBoxSlot>(Title->Slot))
			{
				Under->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 56.0f));
				Under->SetHorizontalAlignment(HAlign_Center);
			}
		}

		bool bHad = false;
		USizeBox* Width = E.Make<USizeBox>(TEXT("SignInWidth"), false, &bHad);

		if (!bHad && Column0 != nullptr)
		{
			UOverlay* Panel = E.Make<UOverlay>(TEXT("SignInPanel"), false);
			UBorder* Glass = E.Make<UBorder>(TEXT("SignInGlass"), true);
			UVerticalBox* Body = E.Make<UVerticalBox>(TEXT("SignInBody"), false);

			Width->SetContent(Panel);
			UOverlaySlot* GlassSlot = Panel->AddChildToOverlay(Glass);
			GlassSlot->SetHorizontalAlignment(HAlign_Fill);
			GlassSlot->SetVerticalAlignment(VAlign_Fill);
			Glass->SetContent(Body);
			Brackets(E, Panel);

			UVerticalBoxSlot* PanelSlot = Column0->AddChildToVerticalBox(Width);
			PanelSlot->SetHorizontalAlignment(HAlign_Center);

			UTextBlock* Heading = E.Make<UTextBlock>(TEXT("SignInTitle"), false);
			Heading->SetText(FText::FromString(TEXT("Sign in")));
			UVerticalBoxSlot* HeadingSlot = Body->AddChildToVerticalBox(Heading);
			HeadingSlot->SetHorizontalAlignment(HAlign_Center);
			HeadingSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 30.0f));

			auto Add = [Body](UWidget* Widget, const FMargin& Padding, const EHorizontalAlignment H = HAlign_Fill)
			{
				if (Widget != nullptr)
				{
					Detach(Widget);
					UVerticalBoxSlot* At = Body->AddChildToVerticalBox(Widget);
					At->SetPadding(Padding);
					At->SetHorizontalAlignment(H);
				}
			};

			// Each label above its field rather than beside it.
			Add(E.Find<UWidget>(TEXT("TextBlock_154")), FMargin(0.0f, 0.0f, 0.0f, 8.0f));
			Add(E.Find<UWidget>(TEXT("SizeBox_0")), FMargin(0.0f, 0.0f, 0.0f, 20.0f));
			Add(E.Find<UWidget>(TEXT("TextBlock")), FMargin(0.0f, 0.0f, 0.0f, 8.0f));
			Add(E.Find<UWidget>(TEXT("SizeBox_1")), FMargin(0.0f, 0.0f, 0.0f, 28.0f));

			// Remember me on the left, Sign in -- the primary action -- on the right.
			UHorizontalBox* Foot = E.Make<UHorizontalBox>(TEXT("SignInFoot"), false);
			Body->AddChildToVerticalBox(Foot);

			UWidget* Remember = E.Find<UWidget>(TEXT("RememberMeBox"));
			UWidget* RememberWords = E.Find<UWidget>(TEXT("TextBlock_947"));
			UWidget* SignIn = E.Find<UWidget>(TEXT("SignInButton"));

			for (UWidget* Part : {Remember, RememberWords, SignIn})
			{
				Detach(Part);
			}

			if (Remember != nullptr)
			{
				Foot->AddChildToHorizontalBox(Remember)->SetVerticalAlignment(VAlign_Center);
			}

			if (RememberWords != nullptr)
			{
				UHorizontalBoxSlot* At = Foot->AddChildToHorizontalBox(RememberWords);
				At->SetVerticalAlignment(VAlign_Center);
				At->SetPadding(FMargin(10.0f, 0.0f, 0.0f, 0.0f));
				At->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			}

			if (SignIn != nullptr)
			{
				Foot->AddChildToHorizontalBox(SignIn)->SetVerticalAlignment(VAlign_Center);
			}

			// The refusal keeps its own border, whose visibility is bound to whether there is one.
			Add(E.Find<UWidget>(TEXT("Border_122")), FMargin(0.0f, 22.0f, 0.0f, 0.0f), HAlign_Center);

			// What held the old layout is empty now.
			for (const TCHAR* Empty : {TEXT("HorizontalBox_120"), TEXT("Border_123"), TEXT("HorizontalBox_175"),
					 TEXT("Border_124"), TEXT("HorizontalBox_290"), TEXT("Border_125"), TEXT("VerticalBox_102")})
			{
				if (UPanelWidget* Old = E.Find<UPanelWidget>(Empty))
				{
					// Removed in place, not detached first: the tree finds a widget through its parent, and one
					// already taken out is left behind without a GUID -- which the compiler reports as errors
					// and saves anyway (3 October).
					if (Old->GetChildrenCount() == 0)
					{
						E.Remove(Old);
					}
				}
			}
		}

		Width->SetWidthOverride(640.0f);

		if (UBorder* Glass = E.Find<UBorder>(TEXT("SignInGlass")))
		{
			Glass->SetBrush(SpaceMMO::Style::Glass());
			Glass->SetPadding(FMargin(60.0f, 46.0f, 60.0f, 44.0f));
		}

		Text(E.Find<UTextBlock>(TEXT("SignInTitle")), ETextRole::Title);

		if (UTextBlock* Email = E.Find<UTextBlock>(TEXT("TextBlock_154")))
		{
			Email->SetText(FText::FromString(TEXT("Email")));
			Text(Email, ETextRole::Column);
		}

		if (UTextBlock* Password = E.Find<UTextBlock>(TEXT("TextBlock")))
		{
			Password->SetText(FText::FromString(TEXT("Password")));
			Text(Password, ETextRole::Column);
		}

		for (const TCHAR* Box : {TEXT("SizeBox_0"), TEXT("SizeBox_1")})
		{
			if (USizeBox* Size = E.Find<USizeBox>(Box))
			{
				Size->ClearWidthOverride();
			}
		}

		Field(E.Find<UEditableTextBox>(TEXT("EmailBox")));
		Field(E.Find<UEditableTextBox>(TEXT("PasswordBox")));

		Text(E.Find<UTextBlock>(TEXT("TextBlock_947")), ETextRole::Note);
		Button(E.Find<UButton>(TEXT("SignInButton")), true);

		if (UBorder* Refusal = E.Find<UBorder>(TEXT("Border_122")))
		{
			Refusal->SetBrush(NoBrush());
		}

		Text(E.Find<UTextBlock>(TEXT("LoginFailureText")), ETextRole::Note, SpaceMMO::Style::ErrorRed());
	}

	// ------------------------------------------------------------------------------------------------
	// Checking

	/** The names of every widget its C++ class binds that are in the tree now. */
	TSet<FName> BoundParts(const UWidgetBlueprint* Blueprint)
	{
		TSet<FName> Found;

		for (TFieldIterator<FObjectProperty> It(Blueprint->ParentClass); It; ++It)
		{
			if ((It->HasMetaData(TEXT("BindWidget")) || It->HasMetaData(TEXT("BindWidgetOptional")))
				&& Blueprint->WidgetTree->FindWidget(It->GetFName()) != nullptr)
			{
				Found.Add(It->GetFName());
			}
		}

		return Found;
	}

	/**
	 * Counts the errors anything logs while one Blueprint is edited and compiled.
	 *
	 * The compiler can report a broken tree -- a widget with no GUID, a variable deleted but still
	 * referenced -- as logged errors and ensures while still calling the Blueprint good, and this
	 * commandlet once saved one that way with "Result: OK" (3 October). Any error now stops the save.
	 */
	struct FErrorCount : public FOutputDevice
	{
		FThreadSafeCounter Errors;

		virtual void Serialize(const TCHAR* Message, const ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			if (Verbosity == ELogVerbosity::Error || Verbosity == ELogVerbosity::Fatal)
			{
				Errors.Increment();
			}
		}
	};

	struct FTarget
	{
		const TCHAR* Asset;
		void (*Style)(FEdit&);
		TArray<const TCHAR*> Needs;

		/** Class defaults to set once it has compiled. Returns the problems it met. */
		int32 (*Defaults)(UWidgetBlueprint*) = nullptr;
	};
}

int32 USpaceMMOStylePanelsCommandlet::Main(const FString& Params)
{
	using namespace SpaceMMOStylePanels;

	const FTarget Targets[] = {
		{TEXT("/Game/UI/WBP_InventoryRow"), &InventoryRow, {TEXT("RowFrame")}},
		{TEXT("/Game/UI/WBP_MarketRow"), &MarketRow, {TEXT("RowFrame")}},
		{TEXT("/Game/UI/WBP_BookRow"), &BookRow, {TEXT("RowFrame")}},
		{TEXT("/Game/UI/WBP_ShipRow"), &ShipRow, {TEXT("RowFrame")}},
		{TEXT("/Game/UI/WBP_MyOrderRow"), &MyOrderRow, {TEXT("RowFrame")}},
		{TEXT("/Game/UI/WBP_TextRow"), &TextRow, {}},
		{TEXT("/Game/UI/WBP_SkillRow"), &SkillRow, {TEXT("RowFrame")}},
		{TEXT("/Game/UI/WBP_SkillsScreen"), &SkillsScreen, {TEXT("WorldDim")}},
		{TEXT("/Game/UI/WBP_InventoryScreen"), &InventoryScreen, {TEXT("WorldDim"), TEXT("PanelRoot")}},
		{TEXT("/Game/UI/WBP_StationOverlay"), &StationOverlay,
			{TEXT("WorldDim"), TEXT("PanelRoot"), TEXT("MarketTabFrame"), TEXT("ShipsTabFrame"), TEXT("MarketTabText"),
				TEXT("ShipsTabText"), TEXT("StartBar"), TEXT("StartButton"), TEXT("RunsText")},
			&StationOverlayDefaults},
		{TEXT("/Game/UI/WBP_FlightReadout"), &FlightReadout, {TEXT("ReadoutCard"), TEXT("ProximityChip"), TEXT("ReadoutRule")}},
		{TEXT("/Game/UI/WBP_OnFootReadout"), &OnFootReadout, {TEXT("ReadoutCard"), TEXT("CreditsLine"), TEXT("ReadoutRule")}},
		{TEXT("/Game/UI/WBP_DepositPrompt"), &DepositPrompt, {TEXT("PromptCard"), TEXT("KeyCap")}},
		{TEXT("/Game/UI/WBP_TransientMessageRow"), &TransientMessageRow, {TEXT("MessageFrame"), TEXT("ToneEdge")}},
		{TEXT("/Game/UI/WBP_LoginScreen"), &LoginScreen, {TEXT("SignInGlass"), TEXT("SignInFoot")}},
	};

	int32 Problems = 0;

	for (const FTarget& Target : Targets)
	{
		const FString PackageName = Target.Asset;
		const FString ObjectPath = PackageName + TEXT(".") + FPackageName::GetShortName(PackageName);

		UWidgetBlueprint* Blueprint = LoadObject<UWidgetBlueprint>(nullptr, *ObjectPath);

		if (Blueprint == nullptr || Blueprint->WidgetTree == nullptr)
		{
			UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Panels: %s did not load."), *PackageName);

			++Problems;

			continue;
		}

		const TSet<FName> Before = BoundParts(Blueprint);

		FEdit Edit;
		Edit.Blueprint = Blueprint;
		Edit.Blueprint->Modify();

		FErrorCount Logged;
		GLog->AddOutputDevice(&Logged);

		Target.Style(Edit);

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);

		GLog->RemoveOutputDevice(&Logged);

		if (Logged.Errors.GetValue() > 0)
		{
			UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Panels: %s logged %d error(s) while it was edited and compiled."),
				*PackageName, Logged.Errors.GetValue());

			++Edit.Problems;
		}

		if (Blueprint->Status == BS_Error)
		{
			UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Panels: %s did not compile."), *PackageName);

			++Edit.Problems;
		}

		if (Target.Defaults != nullptr)
		{
			Edit.Problems += Target.Defaults(Blueprint);
		}

		// Nothing the C++ binds may have gone missing, and what the new styling needs must be there.
		const TSet<FName> After = BoundParts(Blueprint);

		for (const FName& Part : Before)
		{
			if (!After.Contains(Part))
			{
				UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Panels: %s lost its %s."), *PackageName, *Part.ToString());

				++Edit.Problems;
			}
		}

		for (const TCHAR* Need : Target.Needs)
		{
			if (Blueprint->WidgetTree->FindWidget(FName(Need)) == nullptr)
			{
				UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Panels: %s has no %s after restyling."), *PackageName, Need);

				++Edit.Problems;
			}
		}

		if (Edit.Problems > 0)
		{
			UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Panels: %s NOT saved: %d problem(s)."), *PackageName, Edit.Problems);

			Problems += Edit.Problems;

			continue;
		}

		UPackage* Package = Blueprint->GetOutermost();
		Package->MarkPackageDirty();

		const FString Filename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.Error = GWarn;

		if (!UPackage::SavePackage(Package, Blueprint, *Filename, SaveArgs))
		{
			UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Panels: could not save %s."), *Filename);

			++Problems;

			continue;
		}

		UE_LOG(LogSpaceMMOAuthoring, Display, TEXT("Panels: restyled and saved %s; %d bound part(s) kept."),
			*PackageName, After.Num());
	}

	UE_LOG(LogSpaceMMOAuthoring, Display, TEXT("Panels: %d problem(s). Result: %s"), Problems,
		Problems == 0 ? TEXT("OK") : TEXT("FAILED"));

	return Problems == 0 ? 0 : 1;
}
