#include "SpaceMMOBuildMenusCommandlet.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/WidgetTree.h"
#include "Components/BackgroundBlur.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/Slider.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "SpaceMMOAuthoringLog.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintOperationUtils.h"

namespace SpaceMMOBuildMenus
{
	const TCHAR* const Folder = TEXT("/Game/UI/Menus");

	// ------------------------------------------------------------------------------------------------
	// The style guide (task 110), as numbers. One rounded sans (the engine's Roboto until a font is
	// chosen), white and grey text, dark translucent glass with a hairline, an ice-blue selection, and
	// red for errors only. Colours are written as the sRGB values a designer would pick.

	FLinearColor Srgb(const uint8 R, const uint8 G, const uint8 B, const float Alpha = 1.0f)
	{
		FLinearColor Colour = FLinearColor::FromSRGBColor(FColor(R, G, B));
		Colour.A = Alpha;

		return Colour;
	}

	FLinearColor White(const float Alpha) { return FLinearColor(1.0f, 1.0f, 1.0f, Alpha); }

	const FLinearColor TextPrimary = Srgb(242, 245, 250);
	const FLinearColor TextSecondary = Srgb(140, 149, 162);
	const FLinearColor Ice = Srgb(143, 216, 255);
	const FLinearColor GlassFill = Srgb(10, 15, 22, 0.84f);
	const FLinearColor ErrorRed = Srgb(255, 96, 96);

	/** Top left, top right, bottom right, bottom left. Buttons have one corner cut; panels are square. */
	const FVector4 ButtonCorners(0.0, 0.0, 12.0, 0.0);
	const FVector4 RowCorners(3.0, 3.0, 3.0, 3.0);

	FSlateBrush Rounded(const FLinearColor& Fill, const FLinearColor& Outline, const float Width, const FVector4& Radii)
	{
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		Brush.TintColor = FSlateColor(Fill);
		Brush.OutlineSettings = FSlateBrushOutlineSettings(Radii, FSlateColor(Outline), Width);
		Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;

		return Brush;
	}

	/** A flat block of colour: an Image with no texture draws its tint. */
	FSlateBrush Solid(const FLinearColor& Colour, const FVector2D& Size)
	{
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::Image;
		Brush.TintColor = FSlateColor(Colour);
		Brush.ImageSize = Size;

		return Brush;
	}

	FButtonStyle ButtonStyle(const bool bPrimary)
	{
		const FLinearColor Fill = bPrimary ? FLinearColor(Ice.R, Ice.G, Ice.B, 0.16f) : White(0.04f);
		const FLinearColor Edge = bPrimary ? Ice : White(0.30f);

		FButtonStyle Style;
		Style.SetNormal(Rounded(Fill, Edge, 1.0f, ButtonCorners));
		Style.SetHovered(Rounded(
			bPrimary ? FLinearColor(Ice.R, Ice.G, Ice.B, 0.28f) : White(0.09f), Ice, 1.5f, ButtonCorners));
		Style.SetPressed(Rounded(FLinearColor(Ice.R, Ice.G, Ice.B, 0.36f), Ice, 1.5f, ButtonCorners));
		Style.SetDisabled(Rounded(White(0.02f), White(0.10f), 1.0f, ButtonCorners));
		Style.SetNormalPadding(FMargin(30.0f, 11.0f));
		Style.SetPressedPadding(FMargin(30.0f, 12.0f, 30.0f, 10.0f));

		return Style;
	}

	/** A whole-row button: quiet until hovered, so the selection outline is what stands out. */
	FButtonStyle RowStyle()
	{
		FButtonStyle Style;
		Style.SetNormal(Rounded(White(0.035f), White(0.12f), 1.0f, RowCorners));
		Style.SetHovered(Rounded(White(0.07f), White(0.28f), 1.0f, RowCorners));
		Style.SetPressed(Rounded(White(0.10f), White(0.30f), 1.0f, RowCorners));
		Style.SetDisabled(Rounded(White(0.02f), White(0.08f), 1.0f, RowCorners));
		Style.SetNormalPadding(FMargin(26.0f, 16.0f));
		Style.SetPressedPadding(FMargin(26.0f, 16.0f));

		return Style;
	}

	// ------------------------------------------------------------------------------------------------
	// Building a tree

	struct FBuilder
	{
		UWidgetBlueprint* Blueprint = nullptr;

		/**
		 * One widget. A part is one the C++ class binds by name, so it is a variable; everything else
		 * is decoration. Every widget is registered with the Blueprint as the designer would, or the
		 * compiler meets a widget it has no record of.
		 */
		template <typename WidgetType>
		WidgetType* Make(const TCHAR* Name, const bool bPart = false) const
		{
			WidgetType* Widget = Blueprint->WidgetTree->ConstructWidget<WidgetType>(WidgetType::StaticClass(), FName(Name));
			Widget->bIsVariable = bPart;
			Blueprint->OnVariableAdded(Widget->GetFName());

			return Widget;
		}

		UTextBlock* Text(
			const TCHAR* Name,
			const FString& Value,
			const float Size,
			const FLinearColor& Colour,
			const FName Typeface = TEXT("Regular"),
			const int32 LetterSpacing = 0,
			const bool bPart = false) const
		{
			UTextBlock* Block = Make<UTextBlock>(Name, bPart);
			Block->SetText(FText::FromString(Value));
			Block->SetColorAndOpacity(FSlateColor(Colour));

			FSlateFontInfo Font = Block->GetFont();
			Font.Size = Size;
			Font.TypefaceFontName = Typeface;
			Font.LetterSpacing = LetterSpacing;
			Block->SetFont(Font);

			return Block;
		}

		UButton* Button(const TCHAR* Name, const FString& Label, const bool bPrimary) const
		{
			UButton* Made = Make<UButton>(Name, true);
			Made->SetStyle(ButtonStyle(bPrimary));
			Made->AddChild(Text(*(FString(Name) + TEXT("Label")), Label, 18.0f, TextPrimary));

			return Made;
		}
	};

	UVerticalBoxSlot* AddV(
		UVerticalBox* Box, UWidget* Widget, const FMargin& Padding = FMargin(), const EHorizontalAlignment Align = HAlign_Fill)
	{
		UVerticalBoxSlot* Slot = Box->AddChildToVerticalBox(Widget);
		Slot->SetPadding(Padding);
		Slot->SetHorizontalAlignment(Align);

		return Slot;
	}

	UHorizontalBoxSlot* AddH(
		UHorizontalBox* Box, UWidget* Widget, const bool bFill, const FMargin& Padding = FMargin(),
		const EVerticalAlignment Align = VAlign_Center)
	{
		UHorizontalBoxSlot* Slot = Box->AddChildToHorizontalBox(Widget);
		Slot->SetSize(FSlateChildSize(bFill ? ESlateSizeRule::Fill : ESlateSizeRule::Automatic));
		Slot->SetPadding(Padding);
		Slot->SetVerticalAlignment(Align);

		return Slot;
	}

	UOverlaySlot* AddO(
		UOverlay* Overlay, UWidget* Widget, const EHorizontalAlignment H = HAlign_Fill,
		const EVerticalAlignment V = VAlign_Fill, const FMargin& Padding = FMargin())
	{
		UOverlaySlot* Slot = Overlay->AddChildToOverlay(Widget);
		Slot->SetHorizontalAlignment(H);
		Slot->SetVerticalAlignment(V);
		Slot->SetPadding(Padding);

		return Slot;
	}

	/** The corner brackets: two thin bars at each corner of the panel. */
	void Brackets(const FBuilder& B, UOverlay* Panel)
	{
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
			UImage* Across = B.Make<UImage>(*FString::Printf(TEXT("Bracket%sAcross"), Corner.Name));
			Across->SetBrush(Solid(White(0.55f), FVector2D(20.0, 2.0)));
			AddO(Panel, Across, Corner.H, Corner.V, FMargin(12.0f));

			UImage* Down = B.Make<UImage>(*FString::Printf(TEXT("Bracket%sDown"), Corner.Name));
			Down->SetBrush(Solid(White(0.55f), FVector2D(2.0, 20.0)));
			AddO(Panel, Down, Corner.H, Corner.V, FMargin(12.0f));
		}
	}

	/**
	 * Every screen's frame: the live world blurred and darkened behind a centred glass panel. Returns
	 * the panel's body, which the screen fills top to bottom.
	 */
	UVerticalBox* Shell(const FBuilder& B, const float Width, const FString& Title)
	{
		UCanvasPanel* Root = B.Make<UCanvasPanel>(TEXT("Root"));
		B.Blueprint->WidgetTree->RootWidget = Root;

		auto Fill = [Root](UWidget* Widget)
		{
			UCanvasPanelSlot* Slot = Root->AddChildToCanvas(Widget);
			Slot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
			Slot->SetOffsets(FMargin(0.0f));
		};

		UBackgroundBlur* Blur = B.Make<UBackgroundBlur>(TEXT("Backdrop"));
		Blur->SetBlurStrength(8.0f);
		Fill(Blur);

		UImage* Dim = B.Make<UImage>(TEXT("Dim"));
		Dim->SetBrush(Solid(FLinearColor(0.0f, 0.0f, 0.0f, 0.45f), FVector2D(32.0, 32.0)));
		Fill(Dim);

		USizeBox* Size = B.Make<USizeBox>(TEXT("PanelSize"));
		Size->SetWidthOverride(Width);

		UCanvasPanelSlot* SizeSlot = Root->AddChildToCanvas(Size);
		SizeSlot->SetAnchors(FAnchors(0.5f, 0.5f));
		SizeSlot->SetAlignment(FVector2D(0.5, 0.5));
		SizeSlot->SetAutoSize(true);

		UOverlay* Panel = B.Make<UOverlay>(TEXT("Panel"));
		Size->AddChild(Panel);

		UBorder* Glass = B.Make<UBorder>(TEXT("Glass"));
		Glass->SetBrush(Rounded(GlassFill, White(0.16f), 1.0f, FVector4(4.0, 4.0, 4.0, 4.0)));
		Glass->SetPadding(FMargin(56.0f, 44.0f));
		AddO(Panel, Glass);

		Brackets(B, Panel);

		UVerticalBox* Body = B.Make<UVerticalBox>(TEXT("Body"));
		Glass->AddChild(Body);

		if (!Title.IsEmpty())
		{
			AddV(Body, B.Text(TEXT("Title"), Title, 32.0f, TextPrimary, TEXT("Regular"), 120), FMargin(0, 0, 0, 30), HAlign_Center);
		}

		return Body;
	}

	/** Back on the left, the primary action on the right, anything else beside the primary. */
	void ButtonRow(const FBuilder& B, UVerticalBox* Body, UWidget* Left, const TArray<UWidget*>& Right)
	{
		UHorizontalBox* Row = B.Make<UHorizontalBox>(TEXT("ButtonRow"));
		AddV(Body, Row, FMargin(0, 32, 0, 0));

		AddH(Row, Left, false);
		AddH(Row, B.Make<USpacer>(TEXT("ButtonGap")), true);

		for (int32 Index = 0; Index < Right.Num(); ++Index)
		{
			AddH(Row, Right[Index], false, FMargin(Index == 0 ? 0.0f : 16.0f, 0, 0, 0));
		}
	}

	/** A row Blueprint's frame: a button covering the row, and the selection outline over it. */
	UVerticalBox* RowShell(const FBuilder& B)
	{
		UOverlay* Root = B.Make<UOverlay>(TEXT("RowRoot"));
		B.Blueprint->WidgetTree->RootWidget = Root;

		UButton* Select = B.Make<UButton>(TEXT("SelectButton"), true);
		Select->SetStyle(RowStyle());
		AddO(Root, Select, HAlign_Fill, VAlign_Fill, FMargin(0, 0, 0, 10));

		UVerticalBox* Content = B.Make<UVerticalBox>(TEXT("RowContent"));
		Select->AddChild(Content);

		if (UButtonSlot* ContentSlot = Cast<UButtonSlot>(Content->Slot))
		{
			ContentSlot->SetHorizontalAlignment(HAlign_Fill);
			ContentSlot->SetVerticalAlignment(VAlign_Center);
		}

		// Collapsed until the C++ row says it is selected. Two outlines, a soft wide one and a crisp
		// thin one, for the glow in the style reference.
		UOverlay* Frame = B.Make<UOverlay>(TEXT("SelectionFrame"), true);
		Frame->SetVisibility(ESlateVisibility::Collapsed);
		AddO(Root, Frame, HAlign_Fill, VAlign_Fill, FMargin(0, 0, 0, 10));

		UImage* Glow = B.Make<UImage>(TEXT("SelectionGlow"));
		Glow->SetBrush(Rounded(FLinearColor::Transparent, FLinearColor(Ice.R, Ice.G, Ice.B, 0.28f), 6.0f, RowCorners));
		AddO(Frame, Glow);

		UImage* Line = B.Make<UImage>(TEXT("SelectionLine"));
		Line->SetBrush(Rounded(FLinearColor::Transparent, Ice, 1.5f, RowCorners));
		AddO(Frame, Line);

		return Content;
	}

	// ------------------------------------------------------------------------------------------------
	// The six

	void BuildCharacterRow(const FBuilder& B)
	{
		UVerticalBox* Content = RowShell(B);

		UHorizontalBox* Top = B.Make<UHorizontalBox>(TEXT("TopLine"));
		AddV(Content, Top);

		AddH(Top, B.Text(TEXT("NameText"), TEXT("Kestrel"), 26.0f, TextPrimary, TEXT("Bold"), 0, true), true);
		AddH(Top, B.Text(TEXT("LineageText"), TEXT("Martian · Humanity United"), 18.0f, TextPrimary, TEXT("Regular"), 0, true), true, FMargin(16, 0, 16, 0));
		AddH(Top, B.Text(TEXT("CreditsText"), TEXT("1,250.00 cr"), 18.0f, TextPrimary, TEXT("Regular"), 0, true), false);

		AddV(Content, B.Text(TEXT("LastSeenText"), TEXT("Last seen: Ares, on foot"), 15.0f, TextSecondary, TEXT("Regular"), 0, true), FMargin(0, 8, 0, 0));
	}

	void BuildRaceRow(const FBuilder& B)
	{
		UVerticalBox* Content = RowShell(B);

		UHorizontalBox* Line = B.Make<UHorizontalBox>(TEXT("RaceLine"));
		AddV(Content, Line);

		AddH(Line, B.Text(TEXT("RaceText"), TEXT("Martian"), 19.0f, TextPrimary, TEXT("Regular"), 0, true), false);
		AddH(Line, B.Text(TEXT("SeparatorOne"), TEXT("—"), 19.0f, TextSecondary), false, FMargin(10, 0));
		AddH(Line, B.Text(TEXT("FactionText"), TEXT("Humanity United"), 19.0f, TextSecondary, TEXT("Regular"), 0, true), false);
		AddH(Line, B.Text(TEXT("SeparatorTwo"), TEXT("—"), 19.0f, TextSecondary), false, FMargin(10, 0));
		AddH(Line, B.Text(TEXT("HomeWorldText"), TEXT("home world Ares"), 19.0f, TextSecondary, TEXT("Regular"), 0, true), true);

		// The home world's palette, low ground, high ground and rock, as three blocks -- in a box of
		// their own with a gap before it, because "home world Grimhold" ran straight into them.
		UHorizontalBox* Palette = B.Make<UHorizontalBox>(TEXT("Palette"));
		AddH(Line, Palette, false, FMargin(20, 0, 0, 0));

		const TCHAR* Swatches[] = {TEXT("LowSwatch"), TEXT("HighSwatch"), TEXT("RockSwatch")};

		for (const TCHAR* Name : Swatches)
		{
			UImage* Swatch = B.Make<UImage>(Name, true);
			Swatch->SetBrush(Solid(FLinearColor::White, FVector2D(34.0, 34.0)));
			AddH(Palette, Swatch, false, FMargin(4, 0, 0, 0));
		}
	}

	void BuildCharacterSelect(const FBuilder& B)
	{
		UVerticalBox* Body = Shell(B, 980.0f, TEXT("CHOOSE A CHARACTER"));

		USizeBox* Area = B.Make<USizeBox>(TEXT("RowArea"));
		Area->SetMaxDesiredHeight(560.0f);
		AddV(Body, Area);

		UScrollBox* Scroll = B.Make<UScrollBox>(TEXT("RowScroll"));
		Area->AddChild(Scroll);

		UVerticalBox* Rows = B.Make<UVerticalBox>(TEXT("CharacterRows"), true);
		Scroll->AddChild(Rows);

		ButtonRow(B, Body, B.Button(TEXT("SignOutButton"), TEXT("Sign out"), false),
			{B.Button(TEXT("NewCharacterButton"), TEXT("New character"), false), B.Button(TEXT("PlayButton"), TEXT("Play"), true)});
	}

	void BuildNewCharacter(const FBuilder& B)
	{
		UVerticalBox* Body = Shell(B, 980.0f, TEXT("NEW CHARACTER"));

		AddV(Body, B.Text(TEXT("NameLabel"), TEXT("Name"), 20.0f, TextPrimary), FMargin(0, 0, 0, 10));

		UEditableTextBox* NameBox = B.Make<UEditableTextBox>(TEXT("NameBox"), true);
		{
			FEditableTextBoxStyle Style = NameBox->GetWidgetStyle();
			Style.SetBackgroundImageNormal(Rounded(White(0.04f), White(0.26f), 1.0f, FVector4(2, 2, 2, 2)));
			Style.SetBackgroundImageHovered(Rounded(White(0.06f), White(0.40f), 1.0f, FVector4(2, 2, 2, 2)));
			Style.SetBackgroundImageFocused(Rounded(White(0.06f), Ice, 1.5f, FVector4(2, 2, 2, 2)));
			Style.SetBackgroundImageReadOnly(Rounded(White(0.02f), White(0.12f), 1.0f, FVector4(2, 2, 2, 2)));
			Style.SetPadding(FMargin(16.0f, 12.0f));
			Style.SetForegroundColor(FSlateColor(TextPrimary));
			Style.SetFocusedForegroundColor(FSlateColor(TextPrimary));
			Style.SetBackgroundColor(FSlateColor(FLinearColor::White));

			FSlateFontInfo Font = Style.TextStyle.Font;
			Font.Size = 20.0f;
			Style.SetFont(Font);

			NameBox->SetWidgetStyle(Style);
		}
		AddV(Body, NameBox);

		AddV(Body, B.Text(TEXT("NameHint"), TEXT("3–20 characters"), 14.0f, TextSecondary), FMargin(0, 6, 0, 0));

		AddV(Body, B.Text(TEXT("RaceLabel"), TEXT("Race"), 20.0f, TextPrimary), FMargin(0, 26, 0, 10));

		AddV(Body, B.Make<UVerticalBox>(TEXT("RaceRows"), true));

		UTextBlock* Failure = B.Text(TEXT("FailureText"), FString(), 16.0f, ErrorRed, TEXT("Regular"), 0, true);
		AddV(Body, Failure, FMargin(0, 4, 0, 0));

		ButtonRow(B, Body, B.Button(TEXT("BackButton"), TEXT("Back"), false), {B.Button(TEXT("CreateButton"), TEXT("Create"), true)});
	}

	void BuildEscapeMenu(const FBuilder& B)
	{
		UVerticalBox* Body = Shell(B, 460.0f, FString());

		const struct
		{
			const TCHAR* Name;
			const TCHAR* Label;
			bool bPrimary;
		} Buttons[] = {
			{TEXT("ResumeButton"), TEXT("Resume"), true},
			{TEXT("SettingsButton"), TEXT("Settings"), false},
			{TEXT("SignOutButton"), TEXT("Sign out"), false},
			{TEXT("QuitButton"), TEXT("Quit game"), false},
		};

		for (const auto& Entry : Buttons)
		{
			AddV(Body, B.Button(Entry.Name, Entry.Label, Entry.bPrimary), FMargin(0, 0, 0, 12));
		}
	}

	/** One settings line: a label on the left, its control in a fixed-width column on the right. */
	USizeBox* SettingsRow(const FBuilder& B, UVerticalBox* Rows, const TCHAR* Key, const FString& Label)
	{
		UBorder* Row = B.Make<UBorder>(*FString::Printf(TEXT("%sRow"), Key));
		Row->SetBrush(Rounded(White(0.025f), White(0.09f), 1.0f, FVector4(0, 0, 0, 0)));
		Row->SetPadding(FMargin(24.0f, 14.0f));
		AddV(Rows, Row);

		UHorizontalBox* Line = B.Make<UHorizontalBox>(*FString::Printf(TEXT("%sLine"), Key));
		Row->AddChild(Line);

		AddH(Line, B.Text(*FString::Printf(TEXT("%sLabel"), Key), Label, 19.0f, TextPrimary), true);

		USizeBox* Control = B.Make<USizeBox>(*FString::Printf(TEXT("%sControl"), Key));
		Control->SetWidthOverride(400.0f);
		Control->SetMinDesiredHeight(36.0f);
		AddH(Line, Control, false);

		return Control;
	}

	UComboBoxString* Combo(const FBuilder& B, const TCHAR* Name)
	{
		UComboBoxString* Made = B.Make<UComboBoxString>(Name, true);

		// Font and text colour through their properties, the way the designer's details panel sets
		// them: the setters are protected and direct access is deprecated, because both are only read
		// when the widget is built -- which for a Blueprint's template is exactly when they should be.
		if (FStructProperty* FontProperty = FindFProperty<FStructProperty>(UComboBoxString::StaticClass(), TEXT("Font")))
		{
			FSlateFontInfo* Font = FontProperty->ContainerPtrToValuePtr<FSlateFontInfo>(Made);
			Font->Size = 16.0f;
			Font->TypefaceFontName = TEXT("Regular");
		}

		if (FStructProperty* ColourProperty = FindFProperty<FStructProperty>(UComboBoxString::StaticClass(), TEXT("ForegroundColor")))
		{
			*ColourProperty->ContainerPtrToValuePtr<FSlateColor>(Made) = FSlateColor(TextPrimary);
		}

		// The engine's own combo is light grey with dark bold text, which was the one thing on the
		// settings screen that looked like a different game. Dark glass like the rest, and an ice edge
		// on hover, the same as the buttons.
		const FVector4 Corners(2.0, 2.0, 2.0, 2.0);

		FComboBoxStyle Style = Made->GetWidgetStyle();
		FButtonStyle Button = Style.ComboButtonStyle.ButtonStyle;
		Button.SetNormal(Rounded(White(0.04f), White(0.26f), 1.0f, Corners));
		Button.SetHovered(Rounded(White(0.07f), Ice, 1.0f, Corners));
		Button.SetPressed(Rounded(White(0.10f), Ice, 1.5f, Corners));
		Button.SetDisabled(Rounded(White(0.02f), White(0.10f), 1.0f, Corners));
		Style.ComboButtonStyle.SetButtonStyle(Button);
		Style.ComboButtonStyle.SetMenuBorderBrush(Rounded(Srgb(14, 20, 28, 0.97f), White(0.20f), 1.0f, Corners));
		Style.ComboButtonStyle.SetContentPadding(FMargin(12.0f, 6.0f));
		Style.SetContentPadding(FMargin(12.0f, 6.0f));
		Made->SetWidgetStyle(Style);

		FTableRowStyle Item = Made->GetItemStyle();
		const FSlateBrush Plain = Rounded(Srgb(14, 20, 28, 0.97f), FLinearColor::Transparent, 0.0f, FVector4(0, 0, 0, 0));
		const FSlateBrush Hover = Rounded(White(0.08f), FLinearColor::Transparent, 0.0f, FVector4(0, 0, 0, 0));
		const FSlateBrush Chosen = Rounded(FLinearColor(Ice.R, Ice.G, Ice.B, 0.22f), FLinearColor::Transparent, 0.0f, FVector4(0, 0, 0, 0));
		Item.SetEvenRowBackgroundBrush(Plain);
		Item.SetOddRowBackgroundBrush(Plain);
		Item.SetEvenRowBackgroundHoveredBrush(Hover);
		Item.SetOddRowBackgroundHoveredBrush(Hover);
		Item.SetActiveBrush(Chosen);
		Item.SetInactiveBrush(Chosen);
		Item.SetActiveHoveredBrush(Chosen);
		Item.SetInactiveHoveredBrush(Chosen);
		Item.SetTextColor(FSlateColor(TextPrimary));
		Item.SetSelectedTextColor(FSlateColor(TextPrimary));
		Made->SetItemStyle(Item);

		return Made;
	}

	/** A box that fills ice blue when ticked, the selection colour, rather than the engine's grey tick. */
	FCheckBoxStyle CheckStyle()
	{
		const FVector4 Corners(3.0, 3.0, 3.0, 3.0);

		auto Box = [&Corners](const FLinearColor& Fill, const FLinearColor& Edge)
		{
			FSlateBrush Brush = Rounded(Fill, Edge, 1.5f, Corners);
			Brush.ImageSize = FVector2D(26.0, 26.0);

			return Brush;
		};

		FCheckBoxStyle Style;
		Style.SetCheckBoxType(ESlateCheckBoxType::CheckBox);
		Style.SetUncheckedImage(Box(White(0.04f), White(0.40f)));
		Style.SetUncheckedHoveredImage(Box(White(0.08f), Ice));
		Style.SetUncheckedPressedImage(Box(White(0.12f), Ice));
		Style.SetCheckedImage(Box(FLinearColor(Ice.R, Ice.G, Ice.B, 0.85f), Ice));
		Style.SetCheckedHoveredImage(Box(Ice, White(0.9f)));
		Style.SetCheckedPressedImage(Box(FLinearColor(Ice.R, Ice.G, Ice.B, 0.6f), Ice));
		Style.SetUndeterminedImage(Box(White(0.2f), White(0.4f)));
		Style.SetUndeterminedHoveredImage(Box(White(0.25f), Ice));
		Style.SetUndeterminedPressedImage(Box(White(0.3f), Ice));
		Style.SetPadding(FMargin(0.0f));

		return Style;
	}

	void BuildSettings(const FBuilder& B)
	{
		UVerticalBox* Body = Shell(B, 960.0f, TEXT("SETTINGS"));

		UVerticalBox* Rows = B.Make<UVerticalBox>(TEXT("Rows"));
		AddV(Body, Rows);

		{
			USizeBox* Control = SettingsRow(B, Rows, TEXT("Sensitivity"), TEXT("Mouse sensitivity"));

			UHorizontalBox* Pair = B.Make<UHorizontalBox>(TEXT("SensitivityPair"));
			Control->AddChild(Pair);

			USlider* Slider = B.Make<USlider>(TEXT("SensitivitySlider"), true);
			Slider->SetSliderBarColor(White(0.25f));
			Slider->SetSliderHandleColor(Ice);
			Slider->SetStepSize(0.01f);
			Slider->SetValue(0.5f);
			AddH(Pair, Slider, true);

			AddH(Pair, B.Text(TEXT("SensitivityText"), TEXT("1.00x"), 17.0f, TextSecondary, TEXT("Regular"), 0, true), false, FMargin(16, 0, 0, 0));
		}

		{
			USizeBox* Control = SettingsRow(B, Rows, TEXT("Invert"), TEXT("Invert look"));

			UCheckBox* Check = B.Make<UCheckBox>(TEXT("InvertLookCheck"), true);
			Check->SetWidgetStyle(CheckStyle());
			Control->AddChild(Check);

			if (USizeBoxSlot* CheckSlot = Cast<USizeBoxSlot>(Check->Slot))
			{
				CheckSlot->SetHorizontalAlignment(HAlign_Right);
				CheckSlot->SetVerticalAlignment(VAlign_Center);
			}
		}

		SettingsRow(B, Rows, TEXT("WindowMode"), TEXT("Window mode"))->AddChild(Combo(B, TEXT("WindowModeCombo")));
		SettingsRow(B, Rows, TEXT("Resolution"), TEXT("Resolution"))->AddChild(Combo(B, TEXT("ResolutionCombo")));
		SettingsRow(B, Rows, TEXT("Quality"), TEXT("Graphics quality"))->AddChild(Combo(B, TEXT("QualityCombo")));

		ButtonRow(B, Body, B.Button(TEXT("BackButton"), TEXT("Back"), false), {B.Button(TEXT("ApplyButton"), TEXT("Apply"), true)});
	}

	// ------------------------------------------------------------------------------------------------
	// Making, saving and checking

	struct FMenuAsset
	{
		const TCHAR* AssetName;
		const TCHAR* ParentClassPath;
		void (*Build)(const FBuilder&);

		/** The row Blueprint this screen's row-class property should name, if any. */
		const TCHAR* RowProperty = nullptr;
		const TCHAR* RowAssetName = nullptr;

		/**
		 * Optional parts left out on purpose, so the check does not report them. Anything not named
		 * here is required: an unlisted absence is a typo until shown otherwise.
		 */
		TArray<FName> Omitted;
	};

	FString PackageNameFor(const TCHAR* AssetName)
	{
		return FString::Printf(TEXT("%s/%s"), Folder, AssetName);
	}

	UWidgetBlueprint* Existing(const TCHAR* AssetName)
	{
		const FString Path = FString::Printf(TEXT("%s.%s"), *PackageNameFor(AssetName), AssetName);

		return LoadObject<UWidgetBlueprint>(nullptr, *Path);
	}

	/** Every BindWidget or BindWidgetOptional part the parent declares, present and of a fitting class. */
	int32 CheckParts(const UWidgetBlueprint* Blueprint, const UClass* ParentClass, const TArray<FName>& Omitted)
	{
		int32 Problems = 0;
		int32 Checked = 0;

		for (TFieldIterator<FObjectPropertyBase> It(ParentClass, EFieldIteratorFlags::ExcludeSuper); It; ++It)
		{
			if (!It->HasMetaData(TEXT("BindWidget")) && !It->HasMetaData(TEXT("BindWidgetOptional")))
			{
				continue;
			}

			if (Omitted.Contains(It->GetFName()))
			{
				UE_LOG(LogSpaceMMOAuthoring, Display,
					TEXT("Menus: %s leaves out '%s' on purpose."), *Blueprint->GetName(), *It->GetName());

				continue;
			}

			++Checked;

			const UWidget* Widget = Blueprint->WidgetTree->FindWidget(It->GetFName());

			if (Widget == nullptr)
			{
				UE_LOG(LogSpaceMMOAuthoring, Error,
					TEXT("Menus: %s has no part named '%s' (%s)."),
					*Blueprint->GetName(), *It->GetName(), *It->PropertyClass->GetName());

				++Problems;
			}
			else if (!Widget->IsA(It->PropertyClass))
			{
				UE_LOG(LogSpaceMMOAuthoring, Error,
					TEXT("Menus: %s's '%s' is a %s; the C++ expects a %s."),
					*Blueprint->GetName(), *It->GetName(), *Widget->GetClass()->GetName(), *It->PropertyClass->GetName());

				++Problems;
			}
		}

		UE_LOG(LogSpaceMMOAuthoring, Display,
			TEXT("Menus: %s -- %d bound part(s) checked, %d problem(s)."), *Blueprint->GetName(), Checked, Problems);

		return Problems;
	}
}

USpaceMMOBuildMenusCommandlet::USpaceMMOBuildMenusCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 USpaceMMOBuildMenusCommandlet::Main(const FString& Params)
{
	using namespace SpaceMMOBuildMenus;

	const bool bForce = FParse::Param(*Params, TEXT("Force"));

	// Rows first: the screens name them as their row classes.
	const FMenuAsset Assets[] = {
		{TEXT("WBP_CharacterRow"), TEXT("/Script/SpaceMMOBackend.SpaceMMOCharacterRow"), &BuildCharacterRow},
		// The three separate parts rather than SummaryText, so the race can be white and the rest grey
		// as in the style reference; one text block can only be one colour.
		{TEXT("WBP_RaceRow"), TEXT("/Script/SpaceMMOBackend.SpaceMMORaceRow"), &BuildRaceRow, nullptr, nullptr, {TEXT("SummaryText")}},
		{TEXT("WBP_CharacterSelect"), TEXT("/Script/SpaceMMOBackend.SpaceMMOCharacterSelectScreen"), &BuildCharacterSelect, TEXT("RowClass"), TEXT("WBP_CharacterRow")},
		{TEXT("WBP_NewCharacter"), TEXT("/Script/SpaceMMOBackend.SpaceMMONewCharacterScreen"), &BuildNewCharacter, TEXT("RaceRowClass"), TEXT("WBP_RaceRow")},
		{TEXT("WBP_EscapeMenu"), TEXT("/Script/SpaceMMOBackend.SpaceMMOEscapeMenu"), &BuildEscapeMenu},
		{TEXT("WBP_Settings"), TEXT("/Script/SpaceMMOBackend.SpaceMMOSettingsScreen"), &BuildSettings},
	};

	int32 Problems = 0;
	int32 Built = 0;
	int32 Skipped = 0;

	TMap<FString, UWidgetBlueprint*> Made;

	for (const FMenuAsset& Asset : Assets)
	{
		UClass* ParentClass = LoadObject<UClass>(nullptr, Asset.ParentClassPath);

		if (ParentClass == nullptr || !ParentClass->IsChildOf(UUserWidget::StaticClass()))
		{
			UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Menus: no widget class at %s; is the game module built?"), Asset.ParentClassPath);

			++Problems;

			continue;
		}

		const FString PackageName = PackageNameFor(Asset.AssetName);

		if (FPackageName::DoesPackageExist(PackageName) && !bForce)
		{
			UE_LOG(LogSpaceMMOAuthoring, Display,
				TEXT("Menus: %s exists; left alone (it may have been edited). -Force rebuilds it."), *PackageName);

			if (UWidgetBlueprint* Kept = Existing(Asset.AssetName))
			{
				Made.Add(Asset.AssetName, Kept);
			}

			++Skipped;

			continue;
		}

		UPackage* Package = CreatePackage(*PackageName);

		// Its whole contents are about to be replaced, so there is nothing left on disk to load. Without
		// this, -Force over an existing asset gets a package the saver calls "partially loaded" and
		// refuses -- harmlessly, but it rebuilds nothing.
		Package->MarkAsFullyLoaded();

		UWidgetBlueprint* Blueprint = FWidgetBlueprintOperationUtils::CreateWidgetBlueprint(
			Package, FName(Asset.AssetName), BPTYPE_Normal, ParentClass, nullptr, NAME_None, /* bRegisterAndCompile */ false);

		if (Blueprint == nullptr)
		{
			UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Menus: could not create %s."), *PackageName);

			++Problems;

			continue;
		}

		FBuilder Builder;
		Builder.Blueprint = Blueprint;

		Asset.Build(Builder);

		FKismetEditorUtilities::CompileBlueprint(Blueprint);

		if (Blueprint->Status == BS_Error)
		{
			UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Menus: %s did not compile."), *PackageName);

			++Problems;
		}

		// The row class, on the compiled class's defaults, which is what Class Defaults edits.
		if (Asset.RowProperty != nullptr)
		{
			UWidgetBlueprint* const* Row = Made.Find(Asset.RowAssetName);

			FClassProperty* Property = FindFProperty<FClassProperty>(Blueprint->GeneratedClass, Asset.RowProperty);

			if (Row == nullptr || *Row == nullptr || Property == nullptr)
			{
				UE_LOG(LogSpaceMMOAuthoring, Error,
					TEXT("Menus: could not set %s.%s to %s."), Asset.AssetName, Asset.RowProperty, Asset.RowAssetName);

				++Problems;
			}
			else
			{
				Property->SetObjectPropertyValue_InContainer(Blueprint->GeneratedClass->GetDefaultObject(), (*Row)->GeneratedClass);
			}
		}

		Problems += CheckParts(Blueprint, ParentClass, Asset.Omitted);

		Blueprint->MarkPackageDirty();
		FAssetRegistryModule::AssetCreated(Blueprint);

		const FString Filename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.Error = GWarn;

		if (!UPackage::SavePackage(Package, Blueprint, *Filename, SaveArgs))
		{
			UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Menus: could not save %s."), *Filename);

			++Problems;

			continue;
		}

		UE_LOG(LogSpaceMMOAuthoring, Display, TEXT("Menus: built and saved %s (%s)."), *PackageName, *Filename);

		Made.Add(Asset.AssetName, Blueprint);

		++Built;
	}

	UE_LOG(LogSpaceMMOAuthoring, Display,
		TEXT("Menus: %d built, %d left alone, %d problem(s). Result: %s"),
		Built, Skipped, Problems, Problems == 0 ? TEXT("OK") : TEXT("FAILED"));

	return Problems == 0 ? 0 : 1;
}
