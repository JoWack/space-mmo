#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateTypes.h"

/**
 * The interface's colours, in one place (tasks 110 and 173).
 *
 * Joe chose the style on 1 October from four renders: one rounded sans-serif in white and muted grey,
 * panels of dark translucent glass with a hairline border and corner brackets, selection as a white-to-ice
 * outline with a soft glow, buttons with one corner rounded, red for errors only. The menus were built in
 * it from code; on 2 October he approved the game's panels -- inventory, station, skills -- following them.
 *
 * <strong>Read by both, so they cannot drift.</strong> The menus' builder and the panels' runtime styling
 * each had their own copy of these numbers on the way here, and two copies of a colour are a colour that
 * changes in one place.
 *
 * Brushes and button styles too, because the same three users need them: the menus' builder and the
 * panels' restyler set them on the Blueprints, and the panels' rows change them at run time as a row is
 * selected or a drop hovers over it. Slate's types come in with Engine, which Core already depends on.
 */
namespace SpaceMMO::Style
{
	inline FLinearColor Srgb(const uint8 R, const uint8 G, const uint8 B, const float Alpha = 1.0f)
	{
		FLinearColor Colour = FLinearColor::FromSRGBColor(FColor(R, G, B));
		Colour.A = Alpha;

		return Colour;
	}

	inline FLinearColor White(const float Alpha) { return FLinearColor(1.0f, 1.0f, 1.0f, Alpha); }

	/** Every word a player reads first. */
	inline FLinearColor TextPrimary() { return Srgb(242, 245, 250); }

	/** Headings, figures beside a name, and anything not happening yet. */
	inline FLinearColor TextSecondary() { return Srgb(140, 149, 162); }

	/** Selection and the primary action. Never a world's colour. */
	inline FLinearColor Ice(const float Alpha = 1.0f) { return Srgb(143, 216, 255, Alpha); }

	/** A panel's glass. */
	inline FLinearColor GlassFill() { return Srgb(10, 15, 22, 0.84f); }

	/** A prompt's glass, near opaque: it sits on a panel's rows, and at 0.94 their figures read through it. */
	inline FLinearColor PromptFill() { return Srgb(10, 15, 22, 0.98f); }

	/** Errors, and nothing else. */
	inline FLinearColor ErrorRed() { return Srgb(255, 96, 96); }

	/**
	 * What the panels lay over the world: a light dim, never the menus' blur (Joe, 2 October: "Clear
	 * everything"). The world stays readable behind a station's market.
	 */
	inline FLinearColor WorldDim() { return FLinearColor(0.0f, 0.0f, 0.0f, 0.12f); }

	/** Top left, top right, bottom right, bottom left. Buttons have one corner rounded; panels are square. */
	inline FVector4 ButtonCorners() { return FVector4(0.0, 0.0, 12.0, 0.0); }
	inline FVector4 RowCorners() { return FVector4(3.0, 3.0, 3.0, 3.0); }
	inline FVector4 PanelCorners() { return FVector4(4.0, 4.0, 4.0, 4.0); }

	inline FSlateBrush Rounded(
		const FLinearColor& Fill, const FLinearColor& Outline, const float Width, const FVector4& Radii)
	{
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		Brush.TintColor = FSlateColor(Fill);
		Brush.OutlineSettings = FSlateBrushOutlineSettings(Radii, FSlateColor(Outline), Width);
		Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;

		return Brush;
	}

	/** A flat block of colour: an Image with no texture draws its tint. */
	inline FSlateBrush Solid(const FLinearColor& Colour, const FVector2D& Size)
	{
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::Image;
		Brush.TintColor = FSlateColor(Colour);
		Brush.ImageSize = Size;

		return Brush;
	}

	/** Nothing at all, for a row that is a heading rather than a thing. */
	inline FSlateBrush NoBrush()
	{
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::NoDrawType;

		return Brush;
	}

	/** A panel's glass, with its hairline. */
	inline FSlateBrush Glass() { return Rounded(GlassFill(), White(0.16f), 1.0f, PanelCorners()); }

	/** A prompt's glass. */
	inline FSlateBrush PromptGlass() { return Rounded(PromptFill(), White(0.16f), 1.0f, PanelCorners()); }

	/** How a row looks in each state it can be in. */
	enum class ERowLook : uint8
	{
		Normal,
		Hovered,

		/** The one the rest of the panel is about: the market row whose book is shown. */
		Selected,

		/** Where a dragged row would land. */
		DropTarget,

		/** A heading between rows, which is not a thing and has no box. */
		Heading,
	};

	inline FSlateBrush RowBrush(const ERowLook Look)
	{
		switch (Look)
		{
		case ERowLook::Hovered:
			return Rounded(White(0.07f), White(0.28f), 1.0f, RowCorners());
		case ERowLook::Selected:
			return Rounded(Ice(0.06f), Ice(), 1.5f, RowCorners());
		case ERowLook::DropTarget:
			return Rounded(Ice(0.10f), Ice(0.80f), 1.5f, RowCorners());
		case ERowLook::Heading:
			return NoBrush();
		default:
			return Rounded(White(0.035f), White(0.12f), 1.0f, RowCorners());
		}
	}

	/** A tab: an ice outline when it is the one showing, nothing when it is not. */
	inline FSlateBrush TabBrush(const bool bActive)
	{
		return bActive ? Rounded(Ice(0.06f), Ice(), 1.5f, RowCorners()) : Rounded(White(0.0f), White(0.0f), 1.0f, RowCorners());
	}

	inline FButtonStyle ButtonStyle(const bool bPrimary, const bool bSmall = false)
	{
		const FLinearColor Fill = bPrimary ? Ice(0.16f) : White(0.04f);
		const FLinearColor Edge = bPrimary ? Ice() : White(0.30f);
		const FMargin Padding = bSmall ? FMargin(18.0f, 6.0f) : FMargin(30.0f, 11.0f);

		FButtonStyle Style;
		Style.SetNormal(Rounded(Fill, Edge, 1.0f, ButtonCorners()));
		Style.SetHovered(Rounded(bPrimary ? Ice(0.28f) : White(0.09f), Ice(), 1.5f, ButtonCorners()));
		Style.SetPressed(Rounded(Ice(0.36f), Ice(), 1.5f, ButtonCorners()));
		Style.SetDisabled(Rounded(White(0.02f), White(0.10f), 1.0f, ButtonCorners()));
		Style.SetNormalPadding(Padding);
		Style.SetPressedPadding(FMargin(Padding.Left, Padding.Top + 1.0f, Padding.Right, Padding.Bottom - 1.0f));

		return Style;
	}

	/** One end of a count stepper (− and +): flat, inside a box that draws the outline for all three parts. */
	inline FButtonStyle StepperButtonStyle()
	{
		FButtonStyle Style;
		Style.SetNormal(Rounded(White(0.04f), White(0.0f), 0.0f, FVector4(0.0, 0.0, 0.0, 0.0)));
		Style.SetHovered(Rounded(White(0.10f), White(0.0f), 0.0f, FVector4(0.0, 0.0, 0.0, 0.0)));
		Style.SetPressed(Rounded(Ice(0.24f), White(0.0f), 0.0f, FVector4(0.0, 0.0, 0.0, 0.0)));
		Style.SetDisabled(Rounded(White(0.0f), White(0.0f), 0.0f, FVector4(0.0, 0.0, 0.0, 0.0)));
		Style.SetNormalPadding(FMargin(14.0f, 4.0f));
		Style.SetPressedPadding(FMargin(14.0f, 5.0f, 14.0f, 3.0f));

		return Style;
	}

	/** A whole-row button: quiet until hovered, so the selection outline is what stands out. */
	inline FButtonStyle RowButtonStyle()
	{
		FButtonStyle Style;
		Style.SetNormal(RowBrush(ERowLook::Normal));
		Style.SetHovered(RowBrush(ERowLook::Hovered));
		Style.SetPressed(Rounded(White(0.10f), White(0.30f), 1.0f, RowCorners()));
		Style.SetDisabled(Rounded(White(0.02f), White(0.08f), 1.0f, RowCorners()));
		Style.SetNormalPadding(FMargin(26.0f, 16.0f));
		Style.SetPressedPadding(FMargin(26.0f, 16.0f));

		return Style;
	}

	/** What a piece of text is, which decides its size, spacing and colour. */
	enum class ETextRole : uint8
	{
		/** A panel's name: "INVENTORY", "BORLASH". */
		Title,

		/** A heading between rows: "CARRIED", "SELLING". Spaced capitals. */
		Group,

		/** A column's name over a list: "ITEM", "SELLING AT". */
		Column,

		/** The thing a row is about: an item, a skill, a ship. */
		Body,

		/** A figure beside it: a quantity, a price, a level. */
		Figure,

		/** Something not happening yet: an item nobody trades here, a skill never used. */
		Dimmed,

		/** A line under a list, a hint, the second line of a skill. */
		Note,

		/** A prompt's question. */
		Prompt,

		Button,
		ButtonSmall,

		/** The number key beside a tab. */
		Key,
	};

	struct FTextSpec
	{
		float Size = 19.0f;
		FName Typeface = TEXT("Regular");
		int32 LetterSpacing = 0;
		FLinearColor Colour = FLinearColor::White;
		bool bUpper = false;
	};

	inline FTextSpec TextSpec(const ETextRole Role)
	{
		FTextSpec Spec;
		Spec.Colour = TextPrimary();

		switch (Role)
		{
		case ETextRole::Title:
			Spec.Size = 32.0f;
			Spec.LetterSpacing = 120;
			Spec.bUpper = true;
			break;
		case ETextRole::Group:
			Spec.Size = 15.0f;
			Spec.LetterSpacing = 120;
			Spec.Colour = TextSecondary();
			Spec.bUpper = true;
			break;
		case ETextRole::Column:
			Spec.Size = 14.0f;
			Spec.LetterSpacing = 100;
			Spec.Colour = TextSecondary();
			Spec.bUpper = true;
			break;
		case ETextRole::Figure:
			Spec.Colour = TextSecondary();
			break;
		case ETextRole::Dimmed:
			Spec.Colour = TextSecondary();
			break;
		case ETextRole::Note:
			Spec.Size = 15.0f;
			Spec.Colour = TextSecondary();
			break;
		case ETextRole::Prompt:
			Spec.Size = 22.0f;
			break;
		case ETextRole::Button:
			Spec.Size = 18.0f;
			break;
		case ETextRole::ButtonSmall:
			Spec.Size = 16.0f;
			break;
		case ETextRole::Key:
			Spec.Size = 13.0f;
			Spec.Colour = TextSecondary();
			break;
		default:
			break;
		}

		return Spec;
	}
}
