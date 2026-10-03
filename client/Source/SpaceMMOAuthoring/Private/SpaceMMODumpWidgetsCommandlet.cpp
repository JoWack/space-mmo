#include "SpaceMMODumpWidgetsCommandlet.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/BorderSlot.h"
#include "Components/Button.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/OverlaySlot.h"
#include "Components/PanelWidget.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBoxSlot.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "SpaceMMOAuthoringLog.h"
#include "WidgetBlueprint.h"

USpaceMMODumpWidgetsCommandlet::USpaceMMODumpWidgetsCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

namespace SpaceMMODumpWidgets
{
	FString Colour(const FLinearColor& C)
	{
		const FColor S = C.ToFColorSRGB();

		return FString::Printf(TEXT("#%02X%02X%02X@%.2f"), S.R, S.G, S.B, C.A);
	}

	FString Brush(const FSlateBrush& B)
	{
		return FString::Printf(TEXT("brush{draw %d, tint %s, res %s, size %s, outline %s w%.1f}"),
			static_cast<int32>(B.DrawAs.GetValue()), *Colour(B.TintColor.GetSpecifiedColor()),
			*GetNameSafe(B.GetResourceObject()), *B.ImageSize.ToString(),
			*Colour(B.OutlineSettings.Color.GetSpecifiedColor()), B.OutlineSettings.Width);
	}

	FString Margin(const FMargin& M)
	{
		return FString::Printf(TEXT("(%.0f,%.0f,%.0f,%.0f)"), M.Left, M.Top, M.Right, M.Bottom);
	}

	FString SlotText(const UPanelSlot* Slot)
	{
		if (Slot == nullptr)
		{
			return TEXT("");
		}

		if (const UCanvasPanelSlot* C = Cast<UCanvasPanelSlot>(Slot))
		{
			const FAnchorData L = C->GetLayout();

			return FString::Printf(TEXT(" [canvas anchors %s-%s offsets %s align %s auto %d]"),
				*L.Anchors.Minimum.ToString(), *L.Anchors.Maximum.ToString(), *Margin(L.Offsets),
				*L.Alignment.ToString(), C->GetAutoSize() ? 1 : 0);
		}

		if (const UVerticalBoxSlot* V = Cast<UVerticalBoxSlot>(Slot))
		{
			return FString::Printf(TEXT(" [vbox pad %s fill %d h%d v%d]"), *Margin(V->GetPadding()),
				V->GetSize().SizeRule == ESlateSizeRule::Fill ? 1 : 0, (int32)V->GetHorizontalAlignment(),
				(int32)V->GetVerticalAlignment());
		}

		if (const UHorizontalBoxSlot* H = Cast<UHorizontalBoxSlot>(Slot))
		{
			return FString::Printf(TEXT(" [hbox pad %s fill %d h%d v%d]"), *Margin(H->GetPadding()),
				H->GetSize().SizeRule == ESlateSizeRule::Fill ? 1 : 0, (int32)H->GetHorizontalAlignment(),
				(int32)H->GetVerticalAlignment());
		}

		if (const UOverlaySlot* O = Cast<UOverlaySlot>(Slot))
		{
			return FString::Printf(TEXT(" [overlay pad %s h%d v%d]"), *Margin(O->GetPadding()),
				(int32)O->GetHorizontalAlignment(), (int32)O->GetVerticalAlignment());
		}

		if (const UBorderSlot* B = Cast<UBorderSlot>(Slot))
		{
			return FString::Printf(TEXT(" [border pad %s]"), *Margin(B->GetPadding()));
		}

		if (const UScrollBoxSlot* S = Cast<UScrollBoxSlot>(Slot))
		{
			return FString::Printf(TEXT(" [scroll pad %s]"), *Margin(S->GetPadding()));
		}

		return FString::Printf(TEXT(" [%s]"), *Slot->GetClass()->GetName());
	}

	FString Details(const UWidget* W)
	{
		if (const UTextBlock* T = Cast<UTextBlock>(W))
		{
			const FSlateFontInfo F = T->GetFont();

			return FString::Printf(TEXT(" text \"%s\" font %s/%s %.0f colour %s"),
				*T->GetText().ToString().Left(60), *GetNameSafe(F.FontObject), *F.TypefaceFontName.ToString(),
				F.Size, *Colour(T->GetColorAndOpacity().GetSpecifiedColor()));
		}

		if (const UBorder* B = Cast<UBorder>(W))
		{
			return FString::Printf(TEXT(" %s pad %s content %s"), *Brush(B->Background), *Margin(B->GetPadding()),
				*Colour(B->GetContentColorAndOpacity()));
		}

		if (const UImage* I = Cast<UImage>(W))
		{
			return FString::Printf(TEXT(" %s colour %s"), *Brush(I->GetBrush()), *Colour(I->GetColorAndOpacity()));
		}

		if (const UButton* Btn = Cast<UButton>(W))
		{
			return FString::Printf(TEXT(" normal %s"), *Brush(Btn->GetStyle().Normal));
		}

		if (const USizeBox* S = Cast<USizeBox>(W))
		{
			return FString::Printf(TEXT(" size w%.0f h%.0f minw%.0f maxh%.0f"), S->GetWidthOverride(),
				S->GetHeightOverride(), S->GetMinDesiredWidth(), S->GetMaxDesiredHeight());
		}

		return TEXT("");
	}

	void Walk(const UWidget* W, const int32 Depth)
	{
		if (W == nullptr)
		{
			return;
		}

		UE_LOG(LogSpaceMMOAuthoring, Display, TEXT("Dump: %s%s %s%s%s vis %d%s"),
			*FString::ChrN(Depth * 2, TEXT(' ')), *W->GetClass()->GetName(), *W->GetName(),
			W->bIsVariable ? TEXT(" (var)") : TEXT(""), *SlotText(W->Slot), (int32)W->GetVisibility(), *Details(W));

		if (const UPanelWidget* P = Cast<UPanelWidget>(W))
		{
			for (int32 Index = 0; Index < P->GetChildrenCount(); ++Index)
			{
				Walk(P->GetChildAt(Index), Depth + 1);
			}
		}
	}

	void Graphs(const TArray<TObjectPtr<UEdGraph>>& List, const TCHAR* Kind)
	{
		for (const UEdGraph* Graph : List)
		{
			if (Graph == nullptr || Graph->Nodes.Num() == 0)
			{
				continue;
			}

			UE_LOG(LogSpaceMMOAuthoring, Display, TEXT("Dump: %s graph %s, %d node(s):"), Kind, *Graph->GetName(),
				Graph->Nodes.Num());

			for (const UEdGraphNode* Node : Graph->Nodes)
			{
				if (Node == nullptr)
				{
					continue;
				}

				// Literal inputs too, which is where a bound colour's values live: a Make Slate Color's
				// SpecifiedColor, a Select's options.
				FString Literals;

				for (const UEdGraphPin* Pin : Node->Pins)
				{
					if (Pin != nullptr && Pin->Direction == EGPD_Input && Pin->LinkedTo.Num() == 0
						&& !Pin->DefaultValue.IsEmpty() && Pin->PinType.PinCategory != TEXT("exec"))
					{
						Literals += FString::Printf(TEXT(" %s=%s"), *Pin->PinName.ToString(), *Pin->DefaultValue);
					}
				}

				UE_LOG(LogSpaceMMOAuthoring, Display, TEXT("Dump:     %s%s"),
					*Node->GetNodeTitle(ENodeTitleType::ListView).ToString().Replace(TEXT("\n"), TEXT(" / ")),
					*Literals);
			}
		}
	}
}

int32 USpaceMMODumpWidgetsCommandlet::Main(const FString& Params)
{
	using namespace SpaceMMODumpWidgets;

	FString Path = TEXT("/Game/UI");
	FParse::Value(*Params, TEXT("Path="), Path);

	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	Registry.SearchAllAssets(true);

	TArray<FAssetData> Found;
	Registry.GetAssetsByPath(FName(*Path), Found, true);

	Found.Sort([](const FAssetData& A, const FAssetData& B) { return A.PackageName.LexicalLess(B.PackageName); });

	int32 Dumped = 0;

	for (const FAssetData& Asset : Found)
	{
		const UWidgetBlueprint* const Blueprint = Cast<UWidgetBlueprint>(Asset.GetAsset());

		if (Blueprint == nullptr)
		{
			continue;
		}

		++Dumped;

		UE_LOG(LogSpaceMMOAuthoring, Display, TEXT("Dump: ===== %s (parent %s)"), *Asset.PackageName.ToString(),
			*GetNameSafe(Blueprint->ParentClass));

		if (Blueprint->WidgetTree != nullptr)
		{
			Walk(Blueprint->WidgetTree->RootWidget, 1);
		}

		for (const FDelegateEditorBinding& Binding : Blueprint->Bindings)
		{
			UE_LOG(LogSpaceMMOAuthoring, Display, TEXT("Dump: binding %s.%s <- %s"), *Binding.ObjectName,
				*Binding.PropertyName.ToString(), *Binding.FunctionName.ToString());
		}

		Graphs(Blueprint->UbergraphPages, TEXT("event"));
		Graphs(Blueprint->FunctionGraphs, TEXT("function"));
		Graphs(Blueprint->MacroGraphs, TEXT("macro"));
	}

	UE_LOG(LogSpaceMMOAuthoring, Display, TEXT("Dump: %d Widget Blueprint(s) under %s."), Dumped, *Path);

	return 0;
}
