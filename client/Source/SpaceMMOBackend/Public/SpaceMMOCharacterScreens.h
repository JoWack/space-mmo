#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "SpaceMMOBackendTypes.h"

#include "SpaceMMOCharacterScreens.generated.h"

/*
 * Character select and new character (task 110).
 *
 * Both are overlays shown by the controller after sign-in, the arrangement 107's sign-in screen uses.
 * Each is a C++ class with named parts; the layout and the look are Widget Blueprints, authored in
 * the editor against the part names listed on each class. Every part is optional, so a half-built
 * Blueprint shows what it has rather than failing to load.
 *
 * Buttons are bound here by name rather than in a graph, so a Blueprint needs no wiring: a button
 * called PlayButton plays.
 */

/** What should happen once a session exists. Decided by a pure function so it can be tested. */
UENUM()
enum class ESpaceMMOOpening : uint8
{
	/** Play a character straight away: a developer override, or no screen to choose on. */
	Claim,

	/** Show character select. */
	ChooseCharacter,

	/** Show new character, for an account with none. */
	CreateCharacter,

	/** Nothing to play as and nowhere to make one. Logged, as before task 110. */
	NothingToPlay,
};

USTRUCT()
struct SPACEMMOBACKEND_API FSpaceMMOOpening
{
	GENERATED_BODY()

	UPROPERTY()
	ESpaceMMOOpening Kind = ESpaceMMOOpening::NothingToPlay;

	/** The character to claim, for Claim only. */
	UPROPERTY()
	int32 CharacterId = 0;

	/**
	 * Decides what follows a sign-in.
	 *
	 * <strong>The developer overrides still win</strong>, as agreed: <c>-CharacterId=</c> names a
	 * character, and a sign-in from the credentials file plays the first one — two clients on one
	 * desktop, which is how a server is tested here, must not stop at a menu each. Without a
	 * screen configured the game behaves exactly as it did before, which is the state every
	 * automated run is in.
	 */
	static FSpaceMMOOpening Decide(
		int32 DesiredCharacterId,
		bool bSignedInFromCredentialsFile,
		const TArray<FBackendCharacter>& Characters,
		bool bHaveSelectScreen,
		bool bHaveNewCharacterScreen);
};

/** One character's row, as text. Built by a pure function and tested without a widget. */
USTRUCT(BlueprintType)
struct SPACEMMOBACKEND_API FSpaceMMOCharacterRowText
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Menus")
	int32 CharacterId = 0;

	/** "Kestrel". */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Menus")
	FString Name;

	/** "Martian · Humanity United". Empty until the races have loaded. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Menus")
	FString Lineage;

	/** "1,250.00 cr". */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Menus")
	FString Credits;

	/** "Last seen: Ares, on foot", or "Not played yet". */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Menus")
	FString LastSeen;
};

/**
 * One row of character select.
 *
 * Parts: <c>NameText</c>, <c>LineageText</c>, <c>CreditsText</c>, <c>LastSeenText</c> (text
 * blocks); <c>SelectButton</c>, a button covering the row; <c>SelectionFrame</c>, any widget —
 * the ice-blue outline — shown only while the row is selected.
 */
UCLASS()
class SPACEMMOBACKEND_API USpaceMMOCharacterRow : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetRow(const FSpaceMMOCharacterRowText& Row, bool bInSelected, class USpaceMMOCharacterSelectScreen* InOwner);

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Menus")
	bool bSelected = false;

protected:
	virtual void NativeConstruct() override;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Menus", meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> NameText;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Menus", meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> LineageText;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Menus", meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> CreditsText;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Menus", meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> LastSeenText;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Menus", meta = (BindWidgetOptional))
	TObjectPtr<class UButton> SelectButton;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Menus", meta = (BindWidgetOptional))
	TObjectPtr<class UWidget> SelectionFrame;

private:
	UFUNCTION()
	void HandleClicked();

	int32 CharacterId = 0;

	TWeakObjectPtr<class USpaceMMOCharacterSelectScreen> Owner;
};

/**
 * Character select: one row per character, then Play, New character and Sign out.
 *
 * Parts: <c>CharacterRows</c>, a panel the rows are added to; <c>PlayButton</c>,
 * <c>NewCharacterButton</c>, <c>SignOutButton</c>. Set <c>RowClass</c> on the Blueprint's class
 * defaults to the row Blueprint.
 */
UCLASS()
class SPACEMMOBACKEND_API USpaceMMOCharacterSelectScreen : public UUserWidget
{
	GENERATED_BODY()

public:
	/** A row's text. Pure; tested against the server's real payloads. */
	static FSpaceMMOCharacterRowText BuildRow(
		const FBackendCharacter& Character, const TArray<FBackendRace>& Races);

	/** The grey line on its own. */
	static FString DescribeLastSeen(const FBackendCharacter& Character);

	/** Called by the controller each time the screen is shown. */
	void Open();

	void Select(int32 CharacterId);

	UFUNCTION(BlueprintPure, Category = "SpaceMMO|Menus")
	int32 GetSelectedCharacterId() const { return SelectedCharacterId; }

	UFUNCTION(BlueprintCallable, Category = "SpaceMMO|Menus")
	void Play();

protected:
	virtual void NativeConstruct() override;

	virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Menus", meta = (BindWidgetOptional))
	TObjectPtr<class UPanelWidget> CharacterRows;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Menus", meta = (BindWidgetOptional))
	TObjectPtr<class UButton> PlayButton;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Menus", meta = (BindWidgetOptional))
	TObjectPtr<class UButton> NewCharacterButton;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Menus", meta = (BindWidgetOptional))
	TObjectPtr<class UButton> SignOutButton;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SpaceMMO|Menus")
	TSubclassOf<USpaceMMOCharacterRow> RowClass;

private:
	UFUNCTION()
	void HandleNewCharacter();

	UFUNCTION()
	void HandleSignOut();

	class USpaceMMOBackendClient* Backend() const;

	int32 SelectedCharacterId = 0;

	FString RowSignature;

	bool bWarnedAboutWiring = false;
};

/** One race's row, as text and colour. */
USTRUCT(BlueprintType)
struct SPACEMMOBACKEND_API FSpaceMMORaceRowText
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Menus")
	EBackendRace Race = EBackendRace::Humanoid;

	/** "Martian". */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Menus")
	FString RaceName;

	/** "Humanity United". */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Menus")
	FString FactionName;

	/** "home world Ares". */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Menus")
	FString HomeWorld;

	/** "Martian — Humanity United — home world Ares", the line as agreed. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Menus")
	FString Summary;

	/** Whether the home world's palette is known, and what it is. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Menus")
	bool bHasPalette = false;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Menus")
	FLinearColor LowColour = FLinearColor::Black;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Menus")
	FLinearColor HighColour = FLinearColor::White;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Menus")
	FLinearColor RockColour = FLinearColor::Gray;
};

/**
 * One row of the race list.
 *
 * Parts: <c>SummaryText</c> for the whole agreed line, or <c>RaceText</c>, <c>FactionText</c> and
 * <c>HomeWorldText</c> to lay the three out separately; <c>LowSwatch</c>, <c>HighSwatch</c>,
 * <c>RockSwatch</c> (images, tinted with the home world's palette); <c>SelectButton</c>;
 * <c>SelectionFrame</c>.
 */
UCLASS()
class SPACEMMOBACKEND_API USpaceMMORaceRow : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetRow(const FSpaceMMORaceRowText& Row, bool bInSelected, class USpaceMMONewCharacterScreen* InOwner);

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Menus")
	bool bSelected = false;

protected:
	virtual void NativeConstruct() override;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Menus", meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> SummaryText;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Menus", meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> RaceText;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Menus", meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> FactionText;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Menus", meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> HomeWorldText;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Menus", meta = (BindWidgetOptional))
	TObjectPtr<class UImage> LowSwatch;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Menus", meta = (BindWidgetOptional))
	TObjectPtr<class UImage> HighSwatch;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Menus", meta = (BindWidgetOptional))
	TObjectPtr<class UImage> RockSwatch;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Menus", meta = (BindWidgetOptional))
	TObjectPtr<class UButton> SelectButton;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Menus", meta = (BindWidgetOptional))
	TObjectPtr<class UWidget> SelectionFrame;

private:
	UFUNCTION()
	void HandleClicked();

	EBackendRace Race = EBackendRace::Humanoid;

	TWeakObjectPtr<class USpaceMMONewCharacterScreen> Owner;
};

/**
 * New character: a name, a race, and the server's answer.
 *
 * Parts: <c>NameBox</c> (editable text box); <c>RaceRows</c>, a panel; <c>FailureText</c>, the red
 * line under the list, showing the server's refusal verbatim; <c>BackButton</c>,
 * <c>CreateButton</c>. Set <c>RaceRowClass</c> on the class defaults.
 *
 * Create plays the new character straight away (Joe, 1 October).
 */
UCLASS()
class SPACEMMOBACKEND_API USpaceMMONewCharacterScreen : public UUserWidget
{
	GENERATED_BODY()

public:
	/** A race's row. Pure; the palette comes from the home world's body, joined by key. */
	static FSpaceMMORaceRowText BuildRaceRow(const FBackendRace& Race, const TArray<FBackendBody>& Bodies);

	/** Called by the controller each time the screen is shown. */
	void Open();

	void SelectRace(EBackendRace Race);

	UFUNCTION(BlueprintCallable, Category = "SpaceMMO|Menus")
	void Create();

	/** Why the last attempt was refused, in the server's words, or empty. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Menus")
	FString FailureMessage;

	/** Whether a creation is in flight; Create is disabled meanwhile so it cannot be sent twice. */
	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Menus")
	bool bCreating = false;

protected:
	virtual void NativeConstruct() override;

	virtual void NativeDestruct() override;

	virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Menus", meta = (BindWidgetOptional))
	TObjectPtr<class UEditableTextBox> NameBox;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Menus", meta = (BindWidgetOptional))
	TObjectPtr<class UPanelWidget> RaceRows;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Menus", meta = (BindWidgetOptional))
	TObjectPtr<class UTextBlock> FailureText;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Menus", meta = (BindWidgetOptional))
	TObjectPtr<class UButton> BackButton;

	UPROPERTY(BlueprintReadOnly, Category = "SpaceMMO|Menus", meta = (BindWidgetOptional))
	TObjectPtr<class UButton> CreateButton;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SpaceMMO|Menus")
	TSubclassOf<USpaceMMORaceRow> RaceRowClass;

private:
	UFUNCTION()
	void HandleBack();

	UFUNCTION()
	void HandleCreated(int32 CharacterId);

	UFUNCTION()
	void HandleRefused(const FString& Message);

	class USpaceMMOBackendClient* Backend() const;

	EBackendRace SelectedRace = EBackendRace::Humanoid;

	FString RowSignature;

	bool bWarnedAboutWiring = false;
};
