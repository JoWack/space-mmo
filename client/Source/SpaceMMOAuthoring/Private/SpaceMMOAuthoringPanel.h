#pragma once

#include "CoreMinimal.h"
#include "SpaceMMOPreviewMarker.h"
#include "SpaceMMOWorldDocument.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class ASpaceMMOPreviewBody;
class ITableRow;
class STableViewBase;

/** One line of the panel's list, which reads live from the marker rather than from a copy. */
struct FSpaceMMOAuthoringRow
{
	TWeakObjectPtr<ASpaceMMOPreviewMarker> Marker;
};

/**
 * The world authoring panel: read a body, drag what is on it, write the file back.
 *
 * <strong>Nothing reaches disk until Write.</strong> Every change lives in the markers standing in
 * the viewport, so Discard is simply rebuilding the preview from the file — and a session that ends
 * in a crash or a closed editor has changed no content. The alternative, writing as things move,
 * would make the seeder and the editor two writers of one file, which is what task 96 rejected when
 * it decided the capture key would only ever print.
 */
class SSpaceMMOAuthoringPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSpaceMMOAuthoringPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual ~SSpaceMMOAuthoringPanel() override;

private:
	/** Reads the file and refills the body picker. Does not touch the preview. */
	void ReloadDocument();

	void BuildPreview();

	/** Destroys every preview actor in the editor world, including any left by a previous run. */
	void ClearPreview();

	FReply OnReloadClicked();

	FReply OnBuildPreviewClicked();

	FReply OnClearPreviewClicked();

	FReply OnNewDepositClicked();

	FReply OnNewStationClicked();

	FReply OnRemoveSelectedClicked();

	FReply OnWriteClicked();

	FReply OnDiscardClicked();

	/** Spawns a marker for an entry and adds a row for it. */
	ASpaceMMOPreviewMarker* SpawnMarker(const FSpaceMMOAuthoredPlaceable& Entry);

	/** True if the file or the current preview already uses this key. */
	bool IsKeyTaken(const FString& Key) const;

	/** Where the viewport camera is looking, as a direction from the body's centre. */
	FVector DirectionUnderCamera() const;

	/** Everything the write would do, or the reasons it will not. */
	bool BuildEdits(TArray<FSpaceMMOWorldEdit>& OutEdits, TArray<FString>& OutProblems) const;

	TSharedRef<ITableRow> GenerateRow(
		TSharedPtr<FSpaceMMOAuthoringRow> Row, const TSharedRef<STableViewBase>& OwnerTable);

	TSharedRef<SWidget> GenerateBodyOption(TSharedPtr<FString> Option) const;

	FText BodyFactsText() const;

	FText SummaryText() const;

	FText SelectedBodyText() const;

	/** Markers in the viewport, dropping any the user deleted with the Delete key. */
	TArray<ASpaceMMOPreviewMarker*> LiveMarkers() const;

	UWorld* EditorWorld() const;

	FSpaceMMOWorldDocument Document;

	TArray<TSharedPtr<FString>> BodyOptions;

	FString SelectedBodyKey;

	TArray<TSharedPtr<FSpaceMMOAuthoringRow>> Rows;

	TSharedPtr<SListView<TSharedPtr<FSpaceMMOAuthoringRow>>> ListView;

	TSharedPtr<SComboBox<TSharedPtr<FString>>> BodyCombo;

	TWeakObjectPtr<ASpaceMMOPreviewBody> PreviewBody;

	/** What the panel last did, shown under the buttons. */
	FString Status;
};
