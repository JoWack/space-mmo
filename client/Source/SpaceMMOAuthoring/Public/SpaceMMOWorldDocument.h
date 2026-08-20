#pragma once

#include "CoreMinimal.h"

/** Which authored array an entry belongs to. */
enum class ESpaceMMOPlaceableKind : uint8
{
	/** An entry in <c>resourceNodes</c>. */
	Deposit,

	/** An entry in <c>stations</c>, placed on a body by direction. */
	Station,
};

/**
 * One thing standing on a body, as authored.
 *
 * Deposits and stations are one type here because they are one type in the file: both are placed
 * by a direction from a body's centre and nothing else, for the reason the content file states —
 * how high the ground is there is a question the terrain function already answers, and a second
 * answer would be free to disagree with it. The fields that differ are the ones that describe what
 * the thing <em>is</em>, and those are carried alongside rather than made into two hierarchies.
 */
struct SPACEMMOAUTHORING_API FSpaceMMOAuthoredPlaceable
{
	ESpaceMMOPlaceableKind Kind = ESpaceMMOPlaceableKind::Deposit;

	FString Key;

	FString BodyKey;

	/** As authored, not normalised. Content may write whole numbers; the seeder normalises. */
	FVector Direction = FVector::ZeroVector;

	// Deposits.

	FString Item;

	FString Skill;

	/** Item key of a tool the character must hold, or empty for bare hands. */
	FString RequiredTool;

	int32 RequiredLevel = 1;

	int32 QuantityMax = 100;

	int32 RespawnSeconds = 600;

	// Stations.

	FString Name;

	FString SystemKey;

	FString StationKind = TEXT("TradingHub");

	double DockingRangeKilometres = 5.0;
};

/**
 * A body, as authored: enough to draw it and to say what its ground is shaped like.
 */
struct SPACEMMOAUTHORING_API FSpaceMMOAuthoredBody
{
	FString Key;

	FString Name;

	/** The system it belongs to, which a station authored on it has to name as well. */
	FString SystemKey;

	double RadiusKilometres = 0.0;

	/** False for a body nobody has shaped yet, which is a working state rather than an error. */
	bool bHasTerrain = false;

	int64 TerrainSeed = 1;

	double MaxElevationKilometres = 0.5;

	double BaseFrequency = 2.0;

	/** False for a body nobody has painted; the preview then draws with the plain material. */
	bool bHasAppearance = false;

	FLinearColor LowColour = FLinearColor::Gray;

	FLinearColor HighColour = FLinearColor::White;

	FLinearColor RockColour = FLinearColor::Gray;

	/** Height from and to, then slope from and to: where each blend starts and finishes. */
	FVector4 PaletteRanges = FVector4(0.0, 1.0, 0.0, 1.0);
};

/**
 * What a write is asking the file to do.
 */
enum class ESpaceMMOWorldEditKind : uint8
{
	/** Rewrite an existing entry's direction, and nothing else about it. */
	SetDirection,

	/** Rewrite one named field of an existing entry, adding or dropping it if need be. */
	SetField,

	/** Cut an entry out of its array, including the comments written inside it. */
	Remove,

	/** Add a new entry at the end of its array. */
	Append,
};

/** One change to make to the file, named by the key it was read under. */
struct SPACEMMOAUTHORING_API FSpaceMMOWorldEdit
{
	ESpaceMMOWorldEditKind Kind = ESpaceMMOWorldEditKind::SetDirection;

	/** The key as it appears in the file today, which is what finds the entry. */
	FString ExistingKey;

	/** The entry to write, for SetDirection (direction only) and Append (all of it). */
	FSpaceMMOAuthoredPlaceable Entry;

	/** For SetField: which field, and the JSON text of its value. */
	FString Field;

	/** Already quoted and escaped if it is a string. Empty drops the field entirely. */
	FString RawValue;
};

/**
 * <c>data/universe/origin.json</c>, read for editing and written back in place.
 *
 * <strong>Written by splicing text, not by serialising an object graph, and that is the whole
 * design.</strong> Nearly half the lines in that file are <c>$comment</c> keys, and they are not
 * decoration — they are where the reasoning for every placement lives. A JSON writer would reorder
 * keys, restyle every number, and put the entire file into one diff, which makes a content change
 * unreviewable and puts the comments at the mercy of whatever the writer felt like emitting. So a
 * moved deposit rewrites the six numbers of its own direction and leaves every other byte of the
 * file exactly as it was.
 *
 * The reading half does use a JSON parser, because reading has none of those hazards.
 *
 * <strong>Pure, and free of the editor.</strong> Everything here is text in and text out, so the
 * splice can be tested headlessly against the real file — which matters more than usual, since the
 * failure it guards against is silently mangling authored content.
 */
class SPACEMMOAUTHORING_API FSpaceMMOWorldDocument
{
public:
	/** <c>data/universe/origin.json</c>, resolved from the project directory. */
	static FString DefaultPath();

	/** Reads and parses. False and a reason on any failure; the document is left empty. */
	bool Load(const FString& Path, FString& OutError);

	/** The same, for text already in hand. Used by the tests. */
	bool LoadFromText(const FString& InText, const FString& InPath, FString& OutError);

	/** The file exactly as read, byte for byte. */
	const FString& GetText() const { return Text; }

	const FString& GetPath() const { return Path; }

	const TArray<FSpaceMMOAuthoredBody>& GetBodies() const { return Bodies; }

	const TArray<FSpaceMMOAuthoredPlaceable>& GetPlaceables() const { return Placeables; }

	/** Everything standing on one body, deposits first, in file order. */
	TArray<FSpaceMMOAuthoredPlaceable> PlaceablesOn(const FString& BodyKey) const;

	const FSpaceMMOAuthoredBody* FindBody(const FString& BodyKey) const;

	/**
	 * Applies edits to text and hands back the result. Nothing is written to disk.
	 *
	 * Each edit re-finds its entry in the text produced by the one before, so removals and appends
	 * cannot invalidate an offset computed earlier. One failure abandons the whole batch and
	 * returns the original text: a half-applied set of moves would be worse than none, because the
	 * file would then disagree with both the editor and the author's memory of what they did.
	 */
	static bool ApplyEdits(
		const FString& InText,
		const TArray<FSpaceMMOWorldEdit>& Edits,
		FString& OutText,
		FString& OutError);

	/** Which top-level array a kind lives in. */
	static const TCHAR* ArrayNameFor(ESpaceMMOPlaceableKind Kind);

	/**
	 * Rewrites one entry's <c>direction</c> and touches nothing else.
	 */
	static bool SetDirection(
		const FString& InText,
		const TCHAR* ArrayName,
		const FString& Key,
		const FVector& Direction,
		FString& OutText,
		FString& OutError);

	/**
	 * Rewrites one named field of an entry, adding it if it is missing and dropping it if the
	 * value is empty.
	 *
	 * <c>RawValue</c> is JSON text, not a string to be quoted: a caller writing a name passes
	 * <c>"Ares Outpost"</c> with its quotes, and one writing a level passes <c>15</c> without. The
	 * alternative — a typed value union — buys nothing here, because every caller already knows
	 * which of the two it has.
	 *
	 * A field being added is placed on its own line immediately above <c>direction</c>, which is
	 * the last field of every entry in the file and therefore the one place an insertion cannot
	 * land in the middle of a comment.
	 */
	static bool SetField(
		const FString& InText,
		const TCHAR* ArrayName,
		const FString& Key,
		const FString& Field,
		const FString& RawValue,
		FString& OutText,
		FString& OutError);

	/** A string as JSON, quoted and escaped. Empty stays empty, which drops the field. */
	static FString QuotedOrEmpty(const FString& Value);

	/** Cuts one entry, its trailing comma and its line, out of its array. */
	static bool RemoveEntry(
		const FString& InText,
		const TCHAR* ArrayName,
		const FString& Key,
		FString& OutText,
		FString& OutError);

	/** Adds an entry to the end of its array, indented like the entry before it. */
	static bool AppendEntry(
		const FString& InText,
		const FSpaceMMOAuthoredPlaceable& Entry,
		FString& OutText,
		FString& OutError);

	/**
	 * A direction as the content file writes one.
	 *
	 * Six decimals, matching the capture key (task 96, option 1) so a bearing taken in game and a
	 * marker dragged in the editor produce the same shape of line. Six decimals is about a metre on
	 * a 20 km body and finer than that on a real one.
	 */
	static FString FormatDirection(const FVector& Direction);

	/** The object text for a new entry, without indentation applied to the first line. */
	static FString FormatEntry(const FSpaceMMOAuthoredPlaceable& Entry, const FString& Indent);

	/** True if the text has an entry with this key in either array. */
	static bool HasEntry(const FString& InText, const FString& Key);

	/**
	 * True if any comma is followed by the end of its array or object.
	 *
	 * <strong>Unreal's JSON reader accepts a trailing comma and .NET's does not.</strong> That
	 * asymmetry is a trap this file is on the wrong side of: the editor writes the content and the
	 * C# seeder reads it, so "it parses here" proves nothing about the machine that matters. A cut
	 * that left a dangling comma would pass every check the editor can make and fail at the next
	 * --seed, by which time the file is written.
	 *
	 * Found by mutation: removing the last entry of an array was left writing a trailing comma on
	 * purpose, and the test asserting the result still parsed went green.
	 */
	static bool HasDanglingComma(const FString& InText);

private:
	FString Text;

	FString Path;

	TArray<FSpaceMMOAuthoredBody> Bodies;

	TArray<FSpaceMMOAuthoredPlaceable> Placeables;
};
