#include "SpaceMMOAuthoringPanel.h"

#include "Dom/JsonObject.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "LevelEditorViewport.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Selection.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "SpaceMMOAuthoringLog.h"
#include "SpaceMMOPlanetActor.h"
#include "SpaceMMOPreviewBody.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SHeaderRow.h"

#define LOCTEXT_NAMESPACE "SpaceMMOAuthoring"

namespace
{
	/** The re-seed line, in one place, because being told the wrong command is worse than none. */
	const TCHAR* const ReseedCommand =
		TEXT("dotnet run --project services/SpaceMMO.Api -- --seed");

	/** Keys of one authored array in a content file, e.g. every item key. */
	bool ReadContentKeys(
		const FString& RelativePath,
		const TCHAR* ArrayField,
		TSet<FString>& OutKeys,
		TSet<FString>* const OutPlanetLocked = nullptr)
	{
		const FString Path = FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("data"), RelativePath));

		FString Contents;

		if (!FFileHelper::LoadFileToString(Contents, *Path))
		{
			return false;
		}

		TSharedPtr<FJsonObject> Root;

		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Contents);

		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;

		if (!Root->TryGetArrayField(ArrayField, Values) || Values == nullptr)
		{
			return false;
		}

		for (const TSharedPtr<FJsonValue>& Value : *Values)
		{
			const TSharedPtr<FJsonObject> Object = Value.IsValid() ? Value->AsObject() : nullptr;

			if (!Object.IsValid())
			{
				continue;
			}

			FString Key;

			if (!Object->TryGetStringField(TEXT("key"), Key) || Key.IsEmpty())
			{
				continue;
			}

			OutKeys.Add(Key);

			bool bLocked = false;

			if (OutPlanetLocked != nullptr
				&& Object->TryGetBoolField(TEXT("planetLocked"), bLocked)
				&& bLocked)
			{
				OutPlanetLocked->Add(Key);
			}
		}

		return true;
	}

	FText StatusLabel(const ESpaceMMOMarkerStatus Status)
	{
		switch (Status)
		{
		case ESpaceMMOMarkerStatus::Moved:
			return LOCTEXT("StatusChanged", "changed");

		case ESpaceMMOMarkerStatus::Added:
			return LOCTEXT("StatusNew", "new");

		case ESpaceMMOMarkerStatus::Removed:
			return LOCTEXT("StatusRemoved", "removed");

		default:
			return LOCTEXT("StatusUnchanged", "·");
		}
	}

	FSlateColor StatusColour(const ESpaceMMOMarkerStatus Status)
	{
		switch (Status)
		{
		case ESpaceMMOMarkerStatus::Moved:
			return FSlateColor(FLinearColor(1.0f, 0.75f, 0.1f));

		case ESpaceMMOMarkerStatus::Added:
			return FSlateColor(FLinearColor(0.3f, 1.0f, 0.4f));

		case ESpaceMMOMarkerStatus::Removed:
			return FSlateColor(FLinearColor(1.0f, 0.25f, 0.2f));

		default:
			return FSlateColor::UseSubduedForeground();
		}
	}

	/**
	 * One row: key, what it is, and what has happened to it.
	 *
	 * Everything is read from the marker through a lambda rather than copied in, so dragging
	 * something in the viewport updates the list without anything having to notice the drag.
	 */
	class SSpaceMMOEntryRow : public SMultiColumnTableRow<TSharedPtr<FSpaceMMOAuthoringRow>>
	{
	public:
		SLATE_BEGIN_ARGS(SSpaceMMOEntryRow) {}
			SLATE_ARGUMENT(TSharedPtr<FSpaceMMOAuthoringRow>, Row)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwner)
		{
			Entry = InArgs._Row;

			SMultiColumnTableRow<TSharedPtr<FSpaceMMOAuthoringRow>>::Construct(
				FSuperRowType::FArguments(), InOwner);
		}

		virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& Column) override
		{
			if (Column == TEXT("Key"))
			{
				return SNew(STextBlock)
					.Text_Lambda(
						[this]()
						{
							const ASpaceMMOPreviewMarker* const Marker = Get();

							return Marker != nullptr
								? FText::FromString(Marker->Key)
								: LOCTEXT("RowGone", "(marker deleted)");
						});
			}

			if (Column == TEXT("What"))
			{
				return SNew(STextBlock)
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Text_Lambda(
						[this]()
						{
							const ASpaceMMOPreviewMarker* const Marker = Get();

							if (Marker == nullptr)
							{
								return FText::GetEmpty();
							}

							// What the thing is, which is the only way to tell two deposits of
							// different ores apart in a list of keys.
							return FText::FromString(Marker->bIsDeposit
								? (Marker->Item.IsEmpty()
									? FString(TEXT("deposit - no item yet"))
									: FString::Printf(TEXT("deposit - %s"), *Marker->Item))
								: FString::Printf(TEXT("station - %s"), *Marker->StationKind));
						});
			}

			return SNew(STextBlock)
				.Text_Lambda(
					[this]()
					{
						const ASpaceMMOPreviewMarker* const Marker = Get();

						if (Marker == nullptr)
						{
							return FText::GetEmpty();
						}

						const ESpaceMMOMarkerStatus State = Marker->GetStatus();

						// How far, rather than a bare "changed": the distance is the thing worth
						// knowing before writing, and it is given in the kilometres of the planet
						// the game draws, which is ground somebody has to walk.
						if (State == ESpaceMMOMarkerStatus::Moved)
						{
							const double Moved = Marker->MovedKilometres();

							if (Moved > 0.001)
							{
								return FText::FromString(
									FString::Printf(TEXT("moved %.2f km"), Moved));
							}

							return LOCTEXT("StatusEdited", "edited");
						}

						return StatusLabel(State);
					})
				.ColorAndOpacity_Lambda(
					[this]()
					{
						const ASpaceMMOPreviewMarker* const Marker = Get();

						return StatusColour(
							Marker != nullptr
								? Marker->GetStatus()
								: ESpaceMMOMarkerStatus::Unchanged);
					});
		}

	private:
		const ASpaceMMOPreviewMarker* Get() const
		{
			return Entry.IsValid() ? Entry->Marker.Get() : nullptr;
		}

		TSharedPtr<FSpaceMMOAuthoringRow> Entry;
	};
}

void SSpaceMMOAuthoringPanel::Construct(const FArguments& InArgs)
{
	ReloadDocument();

	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 8.0f, 8.0f, 2.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text_Lambda([this]() { return FText::FromString(Document.GetPath()); })
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("Reload", "Reload"))
				.ToolTipText(LOCTEXT(
					"ReloadTip",
					"Re-read origin.json from disk. Any preview is cleared, because markers "
					"standing for entries that may have changed underneath them would be lying."))
				.OnClicked(this, &SSpaceMMOAuthoringPanel::OnReloadClicked)
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 6.0f, 8.0f, 2.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(STextBlock).Text(LOCTEXT("Body", "Body"))
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SAssignNew(BodyCombo, SComboBox<TSharedPtr<FString>>)
				.OptionsSource(&BodyOptions)
				.OnGenerateWidget(this, &SSpaceMMOAuthoringPanel::GenerateBodyOption)
				.OnSelectionChanged_Lambda(
					[this](TSharedPtr<FString> Chosen, ESelectInfo::Type)
					{
						if (Chosen.IsValid())
						{
							SelectedBodyKey = *Chosen;
						}
					})
				[
					SNew(STextBlock)
					.Text(this, &SSpaceMMOAuthoringPanel::SelectedBodyText)
				]
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 2.0f, 8.0f, 6.0f)
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Text(this, &SSpaceMMOAuthoringPanel::BodyFactsText)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 2.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("BuildPreview", "Build preview"))
				.ToolTipText(LOCTEXT(
					"BuildPreviewTip",
					"Draw the body and stand a marker on everything authored on it. The globe is a "
					"scale model: the same terrain function, at the proportions the game draws."))
				.OnClicked(this, &SSpaceMMOAuthoringPanel::OnBuildPreviewClicked)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("ClearPreview", "Clear preview"))
				.OnClicked(this, &SSpaceMMOAuthoringPanel::OnClearPreviewClicked)
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 6.0f, 8.0f, 2.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("NewDeposit", "New deposit"))
				.ToolTipText(LOCTEXT(
					"NewDepositTip",
					"Drop a deposit on the ground the viewport is looking at. Its item, skill and "
					"the rest are edited in the marker's Details panel."))
				.OnClicked(this, &SSpaceMMOAuthoringPanel::OnNewDepositClicked)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("NewStation", "New station"))
				.OnClicked(this, &SSpaceMMOAuthoringPanel::OnNewStationClicked)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("RemoveSelected", "Remove selected"))
				.ToolTipText(LOCTEXT(
					"RemoveSelectedTip",
					"Mark the selected markers for removal. Nothing is cut from the file until "
					"Write, and Discard brings them back."))
				.OnClicked(this, &SSpaceMMOAuthoringPanel::OnRemoveSelectedClicked)
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 6.0f, 8.0f, 2.0f)
		[
			SNew(SSeparator)
		]

		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(8.0f, 2.0f)
		[
			SAssignNew(ListView, SListView<TSharedPtr<FSpaceMMOAuthoringRow>>)
			.ListItemsSource(&Rows)
			.OnGenerateRow(this, &SSpaceMMOAuthoringPanel::GenerateRow)
			.SelectionMode(ESelectionMode::Multi)
			.HeaderRow(
				SNew(SHeaderRow)

				+ SHeaderRow::Column(TEXT("Key"))
				.DefaultLabel(LOCTEXT("ColumnKey", "Key"))
				.FillWidth(0.42f)

				+ SHeaderRow::Column(TEXT("What"))
				.DefaultLabel(LOCTEXT("ColumnWhat", "What"))
				.FillWidth(0.33f)

				+ SHeaderRow::Column(TEXT("State"))
				.DefaultLabel(LOCTEXT("ColumnState", "State"))
				.FillWidth(0.25f))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 4.0f)
		[
			SNew(STextBlock)
			.Text(this, &SSpaceMMOAuthoringPanel::SummaryText)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 2.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("Write", "Write to origin.json"))
				.ToolTipText(LOCTEXT(
					"WriteTip",
					"Rewrite only what changed: the direction of anything moved, the fields of "
					"anything edited, and whole entries added or removed. Every other byte of the "
					"file, comments included, is left exactly as it was."))
				.OnClicked(this, &SSpaceMMOAuthoringPanel::OnWriteClicked)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("Discard", "Discard changes"))
				.ToolTipText(LOCTEXT(
					"DiscardTip",
					"Rebuild the preview from the file, throwing away everything not written."))
				.OnClicked(this, &SSpaceMMOAuthoringPanel::OnDiscardClicked)
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 4.0f, 8.0f, 8.0f)
		[
			SNew(SBorder)
			.Padding(6.0f)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text_Lambda(
					[this]()
					{
						return Status.IsEmpty()
							? LOCTEXT("Idle", "Nothing written this session.")
							: FText::FromString(Status);
					})
			]
		]
	];
}

SSpaceMMOAuthoringPanel::~SSpaceMMOAuthoringPanel()
{
	// The preview belongs to the panel, not to the level. Leaving markers standing in a world
	// nothing is driving any more would let somebody drag one and believe it meant something.
	ClearPreview();
}

UWorld* SSpaceMMOAuthoringPanel::EditorWorld() const
{
	return GEditor != nullptr ? GEditor->GetEditorWorldContext().World() : nullptr;
}

void SSpaceMMOAuthoringPanel::ReloadDocument()
{
	FString Error;

	if (!Document.Load(FSpaceMMOWorldDocument::DefaultPath(), Error))
	{
		Status = Error;

		UE_LOG(LogSpaceMMOAuthoring, Warning, TEXT("%s"), *Error);
	}

	BodyOptions.Reset();

	for (const FSpaceMMOAuthoredBody& Body : Document.GetBodies())
	{
		BodyOptions.Add(MakeShared<FString>(Body.Key));
	}

	if (SelectedBodyKey.IsEmpty() || Document.FindBody(SelectedBodyKey) == nullptr)
	{
		// The body the client is configured to draw, so the tool opens on the world being looked
		// at rather than on whichever one happens to be first in the file.
		const FString Configured = GetDefault<ASpaceMMOPlanetActor>()->BodyKey;

		SelectedBodyKey = Document.FindBody(Configured) != nullptr
			? Configured
			: (Document.GetBodies().Num() > 0 ? Document.GetBodies()[0].Key : FString());
	}

	// The list the combo is showing is the one that was just replaced.
	if (BodyCombo.IsValid())
	{
		BodyCombo->RefreshOptions();
	}
}

FText SSpaceMMOAuthoringPanel::SelectedBodyText() const
{
	return SelectedBodyKey.IsEmpty()
		? LOCTEXT("NoBody", "(no bodies authored)")
		: FText::FromString(SelectedBodyKey);
}

TSharedRef<SWidget> SSpaceMMOAuthoringPanel::GenerateBodyOption(TSharedPtr<FString> Option) const
{
	return SNew(STextBlock)
		.Text(FText::FromString(Option.IsValid() ? *Option : FString()));
}

FText SSpaceMMOAuthoringPanel::BodyFactsText() const
{
	const FSpaceMMOAuthoredBody* const Body = Document.FindBody(SelectedBodyKey);

	if (Body == nullptr)
	{
		return FText::GetEmpty();
	}

	// The authored radius and the drawn one are both named, because they differ and that is a
	// known gap (task 123) rather than a bug in this tool. Somebody reading 339 authored and 20
	// drawn should be able to see which number the preview is a model of.
	return FText::FromString(FString::Printf(
		TEXT("%s · authored %.1f km, drawn at %.1f km · %s"),
		*Body->Name,
		Body->RadiusKilometres,
		FSpaceMMOPreviewScale::DrawnRadiusKilometres(),
		Body->bHasTerrain
			? *FString::Printf(
				TEXT("seed %lld, relief %.2f km, frequency %.1f"),
				Body->TerrainSeed, Body->MaxElevationKilometres, Body->BaseFrequency)
			: TEXT("no authored terrain: previewed as a smooth sphere")));
}

TArray<ASpaceMMOPreviewMarker*> SSpaceMMOAuthoringPanel::LiveMarkers() const
{
	TArray<ASpaceMMOPreviewMarker*> Found;

	UWorld* const World = EditorWorld();

	if (World == nullptr)
	{
		return Found;
	}

	for (TActorIterator<ASpaceMMOPreviewMarker> It(World); It; ++It)
	{
		if (ASpaceMMOPreviewMarker* const Marker = *It; IsValid(Marker))
		{
			Found.Add(Marker);
		}
	}

	return Found;
}

void SSpaceMMOAuthoringPanel::ClearPreview()
{
	UWorld* const World = EditorWorld();

	if (World == nullptr)
	{
		return;
	}

	int32 Destroyed = 0;

	for (TActorIterator<ASpaceMMOPreviewMarker> It(World); It; ++It)
	{
		if (IsValid(*It))
		{
			It->Destroy();
			++Destroyed;
		}
	}

	for (TActorIterator<ASpaceMMOPreviewBody> It(World); It; ++It)
	{
		if (IsValid(*It))
		{
			It->Destroy();
			++Destroyed;
		}
	}

	PreviewBody.Reset();
	Rows.Reset();

	if (ListView.IsValid())
	{
		ListView->RequestListRefresh();
	}

	if (Destroyed > 0)
	{
		UE_LOG(LogSpaceMMOAuthoring, Log, TEXT("Cleared %d preview actors."), Destroyed);
	}
}

ASpaceMMOPreviewMarker* SSpaceMMOAuthoringPanel::SpawnMarker(const FSpaceMMOAuthoredPlaceable& Entry)
{
	UWorld* const World = EditorWorld();

	ASpaceMMOPreviewBody* const Body = PreviewBody.Get();

	if (World == nullptr || Body == nullptr)
	{
		return nullptr;
	}

	FActorSpawnParameters Parameters;

	// Transient, so no amount of saving the level can put a copy of the world's content into an
	// asset. A .umap holding deposits would be the second source of truth this tool exists to
	// avoid, and it would be invisible until somebody wondered why the game disagreed.
	Parameters.ObjectFlags |= RF_Transient;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ASpaceMMOPreviewMarker* const Marker =
		World->SpawnActor<ASpaceMMOPreviewMarker>(
			ASpaceMMOPreviewMarker::StaticClass(), FTransform::Identity, Parameters);

	if (Marker == nullptr)
	{
		return nullptr;
	}

	Marker->Setup(Entry, Body);

#if WITH_EDITOR
	Marker->SetActorLabel(Entry.Key);
#endif

	Rows.Add(MakeShared<FSpaceMMOAuthoringRow>(FSpaceMMOAuthoringRow{ Marker }));

	return Marker;
}

void SSpaceMMOAuthoringPanel::BuildPreview()
{
	ClearPreview();

	UWorld* const World = EditorWorld();

	const FSpaceMMOAuthoredBody* const Body = Document.FindBody(SelectedBodyKey);

	if (World == nullptr || Body == nullptr)
	{
		Status = TEXT("No editor world, or no body chosen.");

		return;
	}

	FActorSpawnParameters Parameters;

	Parameters.ObjectFlags |= RF_Transient;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ASpaceMMOPreviewBody* const Preview = World->SpawnActor<ASpaceMMOPreviewBody>(
		ASpaceMMOPreviewBody::StaticClass(), FTransform::Identity, Parameters);

	if (Preview == nullptr)
	{
		Status = TEXT("The preview body would not spawn.");

		return;
	}

#if WITH_EDITOR
	Preview->SetActorLabel(Body->Key);
#endif

	Preview->Build(*Body, FSpaceMMOPreviewScale::DefaultPreviewRadiusCentimetres);

	PreviewBody = Preview;

	const TArray<FSpaceMMOAuthoredPlaceable> OnBody = Document.PlaceablesOn(SelectedBodyKey);

	for (const FSpaceMMOAuthoredPlaceable& Entry : OnBody)
	{
		SpawnMarker(Entry);
	}

	if (ListView.IsValid())
	{
		ListView->RequestListRefresh();
	}

	Status = FString::Printf(
		TEXT("Previewing %s: %d authored things standing on it."), *Body->Key, OnBody.Num());

	UE_LOG(LogSpaceMMOAuthoring, Log, TEXT("%s"), *Status);
}

FReply SSpaceMMOAuthoringPanel::OnReloadClicked()
{
	ClearPreview();
	ReloadDocument();

	Status = FString::Printf(
		TEXT("Read %d bodies and %d placeable things from the file."),
		Document.GetBodies().Num(),
		Document.GetPlaceables().Num());

	return FReply::Handled();
}

FReply SSpaceMMOAuthoringPanel::OnBuildPreviewClicked()
{
	BuildPreview();

	return FReply::Handled();
}

FReply SSpaceMMOAuthoringPanel::OnClearPreviewClicked()
{
	ClearPreview();

	Status = TEXT("Preview cleared. Nothing was written.");

	return FReply::Handled();
}

bool SSpaceMMOAuthoringPanel::IsKeyTaken(const FString& Key) const
{
	// The file <em>and</em> what is standing in the viewport. Asking only the file would hand the
	// same generated key to two deposits added in one session, and the write would then refuse
	// them both for colliding with each other.
	if (FSpaceMMOWorldDocument::HasEntry(Document.GetText(), Key))
	{
		return true;
	}

	for (const ASpaceMMOPreviewMarker* const Marker : LiveMarkers())
	{
		if (Marker->Key == Key)
		{
			return true;
		}
	}

	return false;
}

FVector SSpaceMMOAuthoringPanel::DirectionUnderCamera() const
{
	const ASpaceMMOPreviewBody* const Body = PreviewBody.Get();

	if (Body == nullptr)
	{
		return FVector::UpVector;
	}

#if WITH_EDITOR
	if (GCurrentLevelEditingViewportClient != nullptr)
	{
		const FVector Camera = GCurrentLevelEditingViewportClient->GetViewLocation();
		const FVector Forward = GCurrentLevelEditingViewportClient->GetViewRotation().Vector();

		// A point the camera's own distance ahead of it, which is on or near the globe whenever
		// the globe is what is being looked at. A ray-sphere intersection would be exact and would
		// also have to answer what to do when the ray misses, which is a worse failure than a
		// marker landing slightly off-centre and being dragged.
		const FVector Ahead = Camera + Forward * (Camera - Body->GetActorLocation()).Size();

		const FVector Direction = Body->DirectionOf(Ahead);

		if (!Direction.IsNearlyZero())
		{
			return Direction;
		}
	}
#endif

	return FVector::UpVector;
}

FReply SSpaceMMOAuthoringPanel::OnNewDepositClicked()
{
	if (!PreviewBody.IsValid())
	{
		Status = TEXT("Build a preview first: a new deposit needs a body to stand on.");

		return FReply::Handled();
	}

	FSpaceMMOAuthoredPlaceable Entry;

	Entry.Kind = ESpaceMMOPlaceableKind::Deposit;
	Entry.BodyKey = SelectedBodyKey;
	Entry.Direction = DirectionUnderCamera();

	// A short key naming the body, and a number that steps until it is free. Keys are permanent
	// once seeded, so this is a starting point to be edited rather than a name anyone should keep.
	FString Stem = SelectedBodyKey;

	Stem.RemoveFromStart(TEXT("body_"));

	int32 Suffix = 1;

	do
	{
		Entry.Key = FString::Printf(TEXT("node_%s_new_%d"), *Stem, Suffix);
		++Suffix;
	}
	while (IsKeyTaken(Entry.Key));

	// Deliberately blank. There is no sensible default material, and a deposit that quietly
	// authored itself as ferrite would be a lie that the seeder would happily accept.
	Entry.Item = FString();
	Entry.Skill = TEXT("mining");
	Entry.RequiredLevel = 1;
	Entry.QuantityMax = 100;
	Entry.RespawnSeconds = 600;

	if (ASpaceMMOPreviewMarker* const Marker = SpawnMarker(Entry))
	{
		Marker->SetAdded();

		if (GEditor != nullptr)
		{
			GEditor->SelectNone(false, true);
			GEditor->SelectActor(Marker, true, true);
		}

		Status = FString::Printf(
			TEXT("Added '%s'. Set its item and skill in the Details panel before writing."),
			*Entry.Key);
	}

	if (ListView.IsValid())
	{
		ListView->RequestListRefresh();
	}

	return FReply::Handled();
}

FReply SSpaceMMOAuthoringPanel::OnNewStationClicked()
{
	if (!PreviewBody.IsValid())
	{
		Status = TEXT("Build a preview first: a new station needs a body to stand on.");

		return FReply::Handled();
	}

	const FSpaceMMOAuthoredBody* const Body = Document.FindBody(SelectedBodyKey);

	FSpaceMMOAuthoredPlaceable Entry;

	Entry.Kind = ESpaceMMOPlaceableKind::Station;
	Entry.BodyKey = SelectedBodyKey;
	Entry.SystemKey = Body != nullptr ? Body->SystemKey : FString();
	Entry.Direction = DirectionUnderCamera();
	Entry.StationKind = TEXT("TradingHub");
	Entry.DockingRangeKilometres = 5.0;

	FString Stem = SelectedBodyKey;

	Stem.RemoveFromStart(TEXT("body_"));

	int32 Suffix = 1;

	do
	{
		Entry.Key = FString::Printf(TEXT("station_%s_new_%d"), *Stem, Suffix);
		++Suffix;
	}
	while (IsKeyTaken(Entry.Key));

	Entry.Name = TEXT("New Outpost");

	if (ASpaceMMOPreviewMarker* const Marker = SpawnMarker(Entry))
	{
		Marker->SetAdded();

		if (GEditor != nullptr)
		{
			GEditor->SelectNone(false, true);
			GEditor->SelectActor(Marker, true, true);
		}

		Status = FString::Printf(TEXT("Added '%s'."), *Entry.Key);
	}

	if (ListView.IsValid())
	{
		ListView->RequestListRefresh();
	}

	return FReply::Handled();
}

FReply SSpaceMMOAuthoringPanel::OnRemoveSelectedClicked()
{
	int32 Marked = 0;

	// The viewport selection first, because picking the thing on the planet is how somebody would
	// say which one they mean. The list's own selection is the fallback for a marker that is
	// off screen.
	if (GEditor != nullptr)
	{
		if (USelection* const Selected = GEditor->GetSelectedActors())
		{
			for (FSelectionIterator It(*Selected); It; ++It)
			{
				if (ASpaceMMOPreviewMarker* const Marker = Cast<ASpaceMMOPreviewMarker>(*It))
				{
					Marker->SetRemoved(!Marker->IsRemoved());
					++Marked;
				}
			}
		}
	}

	if (Marked == 0 && ListView.IsValid())
	{
		for (const TSharedPtr<FSpaceMMOAuthoringRow>& Row : ListView->GetSelectedItems())
		{
			if (Row.IsValid())
			{
				if (ASpaceMMOPreviewMarker* const Marker = Row->Marker.Get())
				{
					Marker->SetRemoved(!Marker->IsRemoved());
					++Marked;
				}
			}
		}
	}

	Status = Marked > 0
		? FString::Printf(
			TEXT("%d entr%s marked. Nothing is cut from the file until Write."),
			Marked, Marked == 1 ? TEXT("y") : TEXT("ies"))
		: FString(TEXT("Nothing selected. Pick a marker in the viewport or a row in the list."));

	return FReply::Handled();
}

FReply SSpaceMMOAuthoringPanel::OnDiscardClicked()
{
	BuildPreview();

	Status = TEXT("Discarded. The preview is the file again.");

	return FReply::Handled();
}

bool SSpaceMMOAuthoringPanel::BuildEdits(
	TArray<FSpaceMMOWorldEdit>& OutEdits, TArray<FString>& OutProblems) const
{
	TSet<FString> ItemKeys;
	TSet<FString> PlanetLocked;
	TSet<FString> SkillKeys;

	const bool bKnowItems =
		ReadContentKeys(TEXT("items/core.json"), TEXT("items"), ItemKeys, &PlanetLocked);

	const bool bKnowSkills = ReadContentKeys(TEXT("skills/core.json"), TEXT("skills"), SkillKeys);

	// A marker deleted with the Delete key is not the same as one marked for removal: the actor is
	// gone, so the write would simply not mention it and the entry would stay in the file. That is
	// a deletion that silently does nothing, so it is refused and explained instead.
	for (const TSharedPtr<FSpaceMMOAuthoringRow>& Row : Rows)
	{
		if (Row.IsValid() && !Row->Marker.IsValid())
		{
			OutProblems.Add(TEXT(
				"A marker was deleted from the level. Deleting does not remove an entry: press "
				"Discard, then use Remove selected."));

			break;
		}
	}

	TSet<FString> UsedKeys;

	// Keys of entries on other bodies, which this session cannot see and must not collide with.
	for (const FSpaceMMOAuthoredPlaceable& Entry : Document.GetPlaceables())
	{
		if (Entry.BodyKey != SelectedBodyKey)
		{
			UsedKeys.Add(Entry.Key);
		}
	}

	for (const ASpaceMMOPreviewMarker* const Marker : LiveMarkers())
	{
		const FSpaceMMOAuthoredPlaceable Entry = Marker->ToPlaceable();

		if (Marker->IsRemoved())
		{
			if (!Marker->IsAdded())
			{
				FSpaceMMOWorldEdit Edit;

				Edit.Kind = ESpaceMMOWorldEditKind::Remove;
				Edit.ExistingKey = Marker->OriginalKey;
				Edit.Entry = Entry;

				OutEdits.Add(Edit);
			}

			// An entry added and then removed in the same session never existed. Nothing to write,
			// and nothing to complain about if it was never filled in.
			continue;
		}

		if (Entry.Key.IsEmpty())
		{
			OutProblems.Add(FString::Printf(
				TEXT("An entry has no key (it was read as '%s')."), *Marker->OriginalKey));

			continue;
		}

		if (UsedKeys.Contains(Entry.Key))
		{
			OutProblems.Add(FString::Printf(
				TEXT("'%s' is used twice. Keys are permanent and identify the thing forever."),
				*Entry.Key));
		}

		UsedKeys.Add(Entry.Key);

		if (Entry.Direction.IsNearlyZero())
		{
			OutProblems.Add(FString::Printf(
				TEXT("'%s' has no direction: it is at the body's centre."), *Entry.Key));
		}

		if (Entry.Kind == ESpaceMMOPlaceableKind::Deposit)
		{
			if (Entry.Item.IsEmpty())
			{
				OutProblems.Add(FString::Printf(
					TEXT("'%s' has no item. A deposit of nothing cannot be seeded."), *Entry.Key));
			}
			else if (bKnowItems && !ItemKeys.Contains(Entry.Item))
			{
				OutProblems.Add(FString::Printf(
					TEXT("'%s' names item '%s', which is not in data/items/core.json."),
					*Entry.Key, *Entry.Item));
			}
			else if (bKnowItems && PlanetLocked.Contains(Entry.Item))
			{
				// ADR-0008: a planet-locked material occurs on exactly one body, and the validator
				// rejects the pack if it does not. Catching it here names the deposit that would
				// break it, rather than leaving somebody to work that out from a failed seed.
				for (const FSpaceMMOAuthoredPlaceable& Other : Document.GetPlaceables())
				{
					if (Other.Item == Entry.Item
						&& Other.BodyKey != Entry.BodyKey
						&& Other.Key != Marker->OriginalKey)
					{
						OutProblems.Add(FString::Printf(
							TEXT("'%s' would put planet-locked '%s' on %s as well as %s "
								"(ADR-0008 allows one body)."),
							*Entry.Key, *Entry.Item, *Entry.BodyKey, *Other.BodyKey));

						break;
					}
				}
			}

			if (Entry.Skill.IsEmpty())
			{
				OutProblems.Add(FString::Printf(TEXT("'%s' has no skill."), *Entry.Key));
			}
			else if (bKnowSkills && !SkillKeys.Contains(Entry.Skill))
			{
				OutProblems.Add(FString::Printf(
					TEXT("'%s' names skill '%s', which is not in data/skills/core.json."),
					*Entry.Key, *Entry.Skill));
			}

			if (bKnowItems && !Entry.RequiredTool.IsEmpty()
				&& !ItemKeys.Contains(Entry.RequiredTool))
			{
				OutProblems.Add(FString::Printf(
					TEXT("'%s' requires tool '%s', which is not an item."),
					*Entry.Key, *Entry.RequiredTool));
			}

			if (Entry.QuantityMax <= 0 || Entry.RespawnSeconds <= 0 || Entry.RequiredLevel <= 0)
			{
				OutProblems.Add(FString::Printf(
					TEXT("'%s' needs a positive quantity, respawn and level."), *Entry.Key));
			}
		}
		else
		{
			if (Entry.Name.IsEmpty())
			{
				OutProblems.Add(FString::Printf(TEXT("'%s' has no name."), *Entry.Key));
			}

			if (Entry.StationKind.IsEmpty())
			{
				OutProblems.Add(FString::Printf(TEXT("'%s' has no kind."), *Entry.Key));
			}

			if (Entry.SystemKey.IsEmpty())
			{
				OutProblems.Add(FString::Printf(TEXT("'%s' names no system."), *Entry.Key));
			}

			if (Entry.DockingRangeKilometres <= 0.0)
			{
				OutProblems.Add(FString::Printf(
					TEXT("'%s' has no docking range, which reads as a broken station."),
					*Entry.Key));
			}
		}

		if (Marker->IsAdded())
		{
			FSpaceMMOWorldEdit Edit;

			Edit.Kind = ESpaceMMOWorldEditKind::Append;
			Edit.Entry = Entry;

			OutEdits.Add(Edit);

			continue;
		}

		if (Marker->GetStatus() == ESpaceMMOMarkerStatus::Unchanged)
		{
			continue;
		}

		const FSpaceMMOAuthoredPlaceable& Was = Marker->GetOriginal();

		const auto AddField = [&OutEdits, &Marker, &Entry](const TCHAR* Field, const FString& Raw)
		{
			FSpaceMMOWorldEdit Edit;

			Edit.Kind = ESpaceMMOWorldEditKind::SetField;
			Edit.ExistingKey = Marker->OriginalKey;
			Edit.Entry = Entry;
			Edit.Field = Field;
			Edit.RawValue = Raw;

			OutEdits.Add(Edit);
		};

		if (!Entry.Direction.Equals(Was.Direction.GetSafeNormal(), 1e-9))
		{
			AddField(TEXT("direction"), FSpaceMMOWorldDocument::FormatDirection(Entry.Direction));
		}

		if (Entry.Kind == ESpaceMMOPlaceableKind::Deposit)
		{
			if (Entry.Item != Was.Item)
			{
				AddField(TEXT("item"), FSpaceMMOWorldDocument::QuotedOrEmpty(Entry.Item));
			}

			if (Entry.Skill != Was.Skill)
			{
				AddField(TEXT("skill"), FSpaceMMOWorldDocument::QuotedOrEmpty(Entry.Skill));
			}

			if (Entry.RequiredTool != Was.RequiredTool)
			{
				AddField(
					TEXT("requiredTool"),
					FSpaceMMOWorldDocument::QuotedOrEmpty(Entry.RequiredTool));
			}

			if (Entry.RequiredLevel != Was.RequiredLevel)
			{
				AddField(TEXT("requiredLevel"), FString::FromInt(Entry.RequiredLevel));
			}

			if (Entry.QuantityMax != Was.QuantityMax)
			{
				AddField(TEXT("quantityMax"), FString::FromInt(Entry.QuantityMax));
			}

			if (Entry.RespawnSeconds != Was.RespawnSeconds)
			{
				AddField(TEXT("respawnSeconds"), FString::FromInt(Entry.RespawnSeconds));
			}
		}
		else
		{
			if (Entry.Name != Was.Name)
			{
				AddField(TEXT("name"), FSpaceMMOWorldDocument::QuotedOrEmpty(Entry.Name));
			}

			if (Entry.StationKind != Was.StationKind)
			{
				AddField(TEXT("kind"), FSpaceMMOWorldDocument::QuotedOrEmpty(Entry.StationKind));
			}

			if (!FMath::IsNearlyEqual(Entry.DockingRangeKilometres, Was.DockingRangeKilometres))
			{
				AddField(
					TEXT("dockingRangeKm"),
					FString::SanitizeFloat(Entry.DockingRangeKilometres));
			}
		}

		// The key last, because every edit above finds its entry by the key the file still has.
		if (Entry.Key != Marker->OriginalKey)
		{
			AddField(TEXT("key"), FSpaceMMOWorldDocument::QuotedOrEmpty(Entry.Key));
		}
	}

	return OutProblems.Num() == 0;
}

FReply SSpaceMMOAuthoringPanel::OnWriteClicked()
{
	TArray<FSpaceMMOWorldEdit> Edits;
	TArray<FString> Problems;

	if (!BuildEdits(Edits, Problems))
	{
		Status = FString::Printf(
			TEXT("Nothing written. %s"), *FString::Join(Problems, TEXT("  ")));

		UE_LOG(LogSpaceMMOAuthoring, Warning, TEXT("Refused to write: %s"),
			*FString::Join(Problems, TEXT(" ")));

		return FReply::Handled();
	}

	if (Edits.Num() == 0)
	{
		Status = TEXT("Nothing has changed, so nothing was written.");

		return FReply::Handled();
	}

	// What is on disk now, not what was read. Somebody editing origin.json in a text editor while
	// this panel was open is the case where a splice computed against a stale copy would land in
	// the wrong place, and it would land silently.
	FString OnDisk;

	if (!FFileHelper::LoadFileToString(OnDisk, *Document.GetPath()))
	{
		Status = FString::Printf(TEXT("Could not re-read %s."), *Document.GetPath());

		return FReply::Handled();
	}

	if (OnDisk != Document.GetText())
	{
		Status = TEXT(
			"The file has changed on disk since it was read. Nothing written: press Reload, "
			"which will also clear the preview.");

		return FReply::Handled();
	}

	FString Written;
	FString Error;

	if (!FSpaceMMOWorldDocument::ApplyEdits(Document.GetText(), Edits, Written, Error))
	{
		Status = FString::Printf(TEXT("Nothing written: %s"), *Error);

		UE_LOG(LogSpaceMMOAuthoring, Warning, TEXT("Edit failed: %s"), *Error);

		return FReply::Handled();
	}

	// Read back before it is trusted: the splice is text surgery, and the one failure worth
	// guarding against is producing something that no longer parses. Better to refuse than to
	// leave a file the seeder will reject.
	FSpaceMMOWorldDocument Check;

	if (!Check.LoadFromText(Written, Document.GetPath(), Error))
	{
		Status = FString::Printf(
			TEXT("Nothing written: the result would not parse (%s). This is a bug in the tool."),
			*Error);

		UE_LOG(LogSpaceMMOAuthoring, Error, TEXT("Refusing to write unparseable JSON: %s"), *Error);

		return FReply::Handled();
	}

	// And the check this engine's parser cannot make: Unreal accepts a trailing comma, .NET does
	// not, and the machine that has to read this file is the C# seeder.
	if (FSpaceMMOWorldDocument::HasDanglingComma(Written))
	{
		Status = TEXT(
			"Nothing written: the result would have a trailing comma, which the seeder rejects "
			"even though this editor would read it. This is a bug in the tool.");

		UE_LOG(LogSpaceMMOAuthoring, Error,
			TEXT("Refusing to write JSON with a dangling comma."));

		return FReply::Handled();
	}

	if (!FFileHelper::SaveStringToFile(
		Written, *Document.GetPath(), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		Status = FString::Printf(TEXT("Could not write %s."), *Document.GetPath());

		return FReply::Handled();
	}

	UE_LOG(LogSpaceMMOAuthoring, Log,
		TEXT("Wrote %d edits to %s."), Edits.Num(), *Document.GetPath());

	// Rebuilt from the file that was just written, so the markers standing in the viewport are the
	// content rather than a memory of it -- and anything the write did not do is visible at once.
	BuildPreview();

	Status = FString::Printf(
		TEXT("Wrote %d change%s at %s. Nothing is in the game until you re-seed:\n    %s"),
		Edits.Num(),
		Edits.Num() == 1 ? TEXT("") : TEXT("s"),
		*FDateTime::Now().ToString(TEXT("%H:%M")),
		ReseedCommand);

	return FReply::Handled();
}

FText SSpaceMMOAuthoringPanel::SummaryText() const
{
	int32 Changed = 0;
	int32 Added = 0;
	int32 Removed = 0;

	for (const ASpaceMMOPreviewMarker* const Marker : LiveMarkers())
	{
		switch (Marker->GetStatus())
		{
		case ESpaceMMOMarkerStatus::Moved:
			++Changed;

			break;

		case ESpaceMMOMarkerStatus::Added:
			++Added;

			break;

		case ESpaceMMOMarkerStatus::Removed:
			++Removed;

			break;

		default:
			break;
		}
	}

	if (Changed + Added + Removed == 0)
	{
		return LOCTEXT("NoChanges", "No changes.");
	}

	return FText::FromString(FString::Printf(
		TEXT("%d changed, %d new, %d removed."), Changed, Added, Removed));
}

TSharedRef<ITableRow> SSpaceMMOAuthoringPanel::GenerateRow(
	TSharedPtr<FSpaceMMOAuthoringRow> Row, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SSpaceMMOEntryRow, OwnerTable).Row(Row);
}

#undef LOCTEXT_NAMESPACE
