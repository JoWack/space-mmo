#include "SpaceMMOPanelRow.h"

#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBoxSlot.h"

void SpaceMMO::PanelLook::Apply(UTextBlock* Text, const Style::ETextRole Role, const TOptional<FLinearColor> Colour)
{
	if (Text == nullptr)
	{
		return;
	}

	const Style::FTextSpec Spec = Style::TextSpec(Role);

	// The font the text already has -- the engine's Roboto -- with the face, size and spacing changed. A
	// different font object would be a different family, which is a decision the style guide left open.
	FSlateFontInfo Font = Text->GetFont();
	Font.TypefaceFontName = Spec.Typeface;
	Font.Size = Spec.Size;
	Font.LetterSpacing = Spec.LetterSpacing;

	Text->SetFont(Font);
	Text->SetColorAndOpacity(FSlateColor(Colour.Get(Spec.Colour)));
	Text->SetTextTransformPolicy(Spec.bUpper ? ETextTransformPolicy::ToUpper : ETextTransformPolicy::None);
}

void SpaceMMO::PanelLook::ApplyFigure(UTextBlock* Text, const Style::ETextRole Role, const float Width)
{
	Apply(Text, Role);

	if (Text != nullptr)
	{
		Text->SetMinDesiredWidth(Width);
		Text->SetJustification(ETextJustify::Right);
	}
}

void USpaceMMOPanelRow::NativeConstruct()
{
	Super::NativeConstruct();

	// Styled on the first tick rather than here. A row is constructed while the list adding it is still
	// building the slot it goes in, and setting that slot's padding now asserts in Slate ("Slot Attributes
	// has to be registered after the FSlot is constructed") -- which is how the first look at these panels
	// ended, 3 October. The Blueprint already carries the row's normal look, so there is nothing to flash.
	RequestRestyle();
}

void USpaceMMOPanelRow::NativeTick(const FGeometry& Geometry, const float DeltaSeconds)
{
	Super::NativeTick(Geometry, DeltaSeconds);

	ApplyLook();
}

void USpaceMMOPanelRow::NativeOnMouseEnter(const FGeometry& Geometry, const FPointerEvent& Event)
{
	Super::NativeOnMouseEnter(Geometry, Event);

	bHovered = true;
}

void USpaceMMOPanelRow::NativeOnMouseLeave(const FPointerEvent& Event)
{
	Super::NativeOnMouseLeave(Event);

	bHovered = false;
}

SpaceMMO::Style::ERowLook USpaceMMOPanelRow::Look() const
{
	return bHovered ? SpaceMMO::Style::ERowLook::Hovered : SpaceMMO::Style::ERowLook::Normal;
}

void USpaceMMOPanelRow::ApplyLook()
{
	const SpaceMMO::Style::ERowLook Now = Look();

	if (!bRestyle && AppliedLook.IsSet() && AppliedLook.GetValue() == Now)
	{
		return;
	}

	bRestyle = false;
	AppliedLook = Now;

	if (RowFrame != nullptr)
	{
		RowFrame->SetBrush(SpaceMMO::Style::RowBrush(Now));

		// A heading sits on the panel with air above it rather than in a box of its own.
		RowFrame->SetPadding(Now == SpaceMMO::Style::ERowLook::Heading ? FMargin(4.0f, 18.0f, 4.0f, 6.0f)
			: FMargin(20.0f, 12.0f));
	}

	// Apart from the next row, whatever list it was added to: rows are added at run time, and a slot made
	// that way has no padding of its own.
	if (UVerticalBoxSlot* const InList = Cast<UVerticalBoxSlot>(Slot))
	{
		InList->SetPadding(FMargin(0.0f, 0.0f, 0.0f, Now == SpaceMMO::Style::ERowLook::Heading ? 0.0f : 6.0f));
	}

	StyleTexts(Now);
}
