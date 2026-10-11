#include "SpaceMMOItemStyleTag.h"

#include "Components/TextBlock.h"
#include "SpaceMMOPanelRow.h"
#include "SpaceMMOStyle.h"

FString SpaceMMO::ItemStyleTag::Text(const EBackendItemStyle World)
{
	switch (World)
	{
	case EBackendItemStyle::Terran:
		return TEXT("[TER]");
	case EBackendItemStyle::Martian:
		return TEXT("[MAR]");
	case EBackendItemStyle::Verdant:
		return TEXT("[VER]");
	case EBackendItemStyle::Grimhold:
		return TEXT("[GRM]");
	default:
		return FString();
	}
}

FLinearColor SpaceMMO::ItemStyleTag::Colour(const EBackendItemStyle World)
{
	switch (World)
	{
	case EBackendItemStyle::Terran:
		return Style::Srgb(0x7F, 0xB8, 0xD8);
	case EBackendItemStyle::Martian:
		return Style::Srgb(0xD0, 0x58, 0x3A);
	case EBackendItemStyle::Verdant:
		return Style::Srgb(0x8F, 0xF0, 0xD0);
	case EBackendItemStyle::Grimhold:
		return Style::Srgb(0xD2, 0x69, 0x1E);
	default:
		return Style::TextPrimary();
	}
}

void SpaceMMO::ItemStyleTag::Apply(UTextBlock* const Tag, const EBackendItemStyle World)
{
	if (Tag == nullptr)
	{
		return;
	}

	Tag->SetText(FText::FromString(Text(World)));

	// A note's size beside a body-sized name, so the tag reads as a label on the item rather than as a
	// second word of its name.
	PanelLook::Apply(Tag, Style::ETextRole::Note, Colour(World));

	// Collapsed rather than emptied, so a Common row closes up instead of keeping the tag's gap.
	Tag->SetVisibility(IsShown(World) ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
}
