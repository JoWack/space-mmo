#include "SpaceMMOWorldDocument.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	bool IsJsonSpace(const TCHAR Character)
	{
		return Character == TEXT(' ')
			|| Character == TEXT('\t')
			|| Character == TEXT('\r')
			|| Character == TEXT('\n');
	}

	/** Index just past the string literal beginning at Index, which must be its opening quote. */
	int32 SkipString(const FString& Text, int32 Index)
	{
		const int32 Length = Text.Len();

		++Index;

		while (Index < Length)
		{
			const TCHAR Character = Text[Index];

			// A backslash escapes whatever follows, including a quote. Without this an entry whose
			// comment contains an escaped quote would end its string early, and every offset after
			// it would be wrong -- silently, and only for that one entry.
			if (Character == TEXT('\\'))
			{
				Index += 2;

				continue;
			}

			if (Character == TEXT('"'))
			{
				return Index + 1;
			}

			++Index;
		}

		return Length;
	}

	/** Index of the bracket or brace closing the one at Index, or INDEX_NONE if unbalanced. */
	int32 MatchBracket(const FString& Text, int32 Index)
	{
		const int32 Length = Text.Len();

		int32 Depth = 0;

		while (Index < Length)
		{
			const TCHAR Character = Text[Index];

			if (Character == TEXT('"'))
			{
				Index = SkipString(Text, Index);

				continue;
			}

			if (Character == TEXT('{') || Character == TEXT('['))
			{
				++Depth;
			}
			else if (Character == TEXT('}') || Character == TEXT(']'))
			{
				--Depth;

				if (Depth == 0)
				{
					return Index;
				}
			}

			++Index;
		}

		return INDEX_NONE;
	}

	/** The contents of the string literal at Index. Keys and enum values here carry no escapes. */
	FString ReadString(const FString& Text, const int32 Index)
	{
		const int32 After = SkipString(Text, Index);

		return Text.Mid(Index + 1, FMath::Max(0, After - Index - 2));
	}

	/**
	 * Where the value of Name begins, searching only the top level of [Start, End).
	 *
	 * The region is the <em>inside</em> of an object: Start just past its opening brace, End at its
	 * closing one. Depth is tracked so a key of the same name inside a nested object is not
	 * mistaken for this object's own.
	 */
	int32 FindFieldValue(
		const FString& Text,
		const int32 Start,
		const int32 End,
		const FString& Name,
		int32* const OutKeyStart = nullptr)
	{
		int32 Index = Start;
		int32 Depth = 0;

		while (Index < End)
		{
			const TCHAR Character = Text[Index];

			if (Character == TEXT('"'))
			{
				const int32 After = SkipString(Text, Index);

				if (Depth == 0)
				{
					int32 Probe = After;

					while (Probe < End && IsJsonSpace(Text[Probe]))
					{
						++Probe;
					}

					// A string followed by a colon is a key; one that is not is a value, and a
					// value that happens to read "direction" must not be taken for a field.
					if (Probe < End && Text[Probe] == TEXT(':') && ReadString(Text, Index) == Name)
					{
						int32 Value = Probe + 1;

						while (Value < End && IsJsonSpace(Text[Value]))
						{
							++Value;
						}

						if (OutKeyStart != nullptr)
						{
							*OutKeyStart = Index;
						}

						return Value < End ? Value : INDEX_NONE;
					}
				}

				Index = After;

				continue;
			}

			if (Character == TEXT('{') || Character == TEXT('['))
			{
				++Depth;
			}
			else if (Character == TEXT('}') || Character == TEXT(']'))
			{
				--Depth;
			}

			++Index;
		}

		return INDEX_NONE;
	}

	/** The brackets of a top-level array, or false if the document has no such array. */
	bool FindArray(const FString& Text, const FString& Name, int32& OutOpen, int32& OutClose)
	{
		int32 RootOpen = INDEX_NONE;

		for (int32 Index = 0; Index < Text.Len(); ++Index)
		{
			if (Text[Index] == TEXT('{'))
			{
				RootOpen = Index;

				break;
			}
		}

		if (RootOpen == INDEX_NONE)
		{
			return false;
		}

		const int32 RootClose = MatchBracket(Text, RootOpen);

		if (RootClose == INDEX_NONE)
		{
			return false;
		}

		const int32 Value = FindFieldValue(Text, RootOpen + 1, RootClose, Name);

		if (Value == INDEX_NONE || Text[Value] != TEXT('['))
		{
			return false;
		}

		const int32 Close = MatchBracket(Text, Value);

		if (Close == INDEX_NONE)
		{
			return false;
		}

		OutOpen = Value;
		OutClose = Close;

		return true;
	}

	/** Visits each object element of an array. Returning false from the visitor stops the walk. */
	void ForEachObject(
		const FString& Text,
		const int32 ArrayOpen,
		const int32 ArrayClose,
		TFunctionRef<bool(int32 ObjectOpen, int32 ObjectClose)> Visit)
	{
		int32 Index = ArrayOpen + 1;

		while (Index < ArrayClose)
		{
			const TCHAR Character = Text[Index];

			if (Character == TEXT('"'))
			{
				Index = SkipString(Text, Index);

				continue;
			}

			if (Character == TEXT('{'))
			{
				const int32 End = MatchBracket(Text, Index);

				if (End == INDEX_NONE)
				{
					return;
				}

				if (!Visit(Index, End))
				{
					return;
				}

				Index = End + 1;

				continue;
			}

			++Index;
		}
	}

	/** The object in a named array whose "key" field holds Key. */
	bool FindEntry(
		const FString& Text,
		const FString& ArrayName,
		const FString& Key,
		int32& OutOpen,
		int32& OutClose)
	{
		int32 ArrayOpen = INDEX_NONE;
		int32 ArrayClose = INDEX_NONE;

		if (!FindArray(Text, ArrayName, ArrayOpen, ArrayClose))
		{
			return false;
		}

		int32 FoundOpen = INDEX_NONE;
		int32 FoundClose = INDEX_NONE;

		ForEachObject(Text, ArrayOpen, ArrayClose,
			[&Text, &Key, &FoundOpen, &FoundClose](const int32 Open, const int32 Close)
			{
				const int32 Value = FindFieldValue(Text, Open + 1, Close, TEXT("key"));

				if (Value != INDEX_NONE
					&& Text[Value] == TEXT('"')
					&& ReadString(Text, Value) == Key)
				{
					FoundOpen = Open;
					FoundClose = Close;

					return false;
				}

				return true;
			});

		if (FoundOpen == INDEX_NONE)
		{
			return false;
		}

		OutOpen = FoundOpen;
		OutClose = FoundClose;

		return true;
	}

	/** Whatever this file uses to end a line, so an insertion does not mix the two. */
	FString DetectNewline(const FString& Text)
	{
		return Text.Contains(TEXT("\r\n")) ? FString(TEXT("\r\n")) : FString(TEXT("\n"));
	}

	/** The whitespace before Index on its own line, or empty if anything else precedes it. */
	FString IndentOfLine(const FString& Text, const int32 Index)
	{
		int32 Start = Index;

		while (Start > 0 && (Text[Start - 1] == TEXT(' ') || Text[Start - 1] == TEXT('\t')))
		{
			--Start;
		}

		if (Start > 0 && Text[Start - 1] != TEXT('\n'))
		{
			return FString();
		}

		return Text.Mid(Start, Index - Start);
	}

	/** The last index of the value beginning at Value, whatever kind of value it is. */
	int32 EndOfValue(const FString& Text, const int32 Value, const int32 Limit)
	{
		const TCHAR Character = Text[Value];

		if (Character == TEXT('"'))
		{
			return SkipString(Text, Value) - 1;
		}

		if (Character == TEXT('[') || Character == TEXT('{'))
		{
			return MatchBracket(Text, Value);
		}

		// A number, true, false or null: it ends where the next field or the object does.
		int32 Index = Value;

		while (Index < Limit
			&& Text[Index] != TEXT(',')
			&& Text[Index] != TEXT('}')
			&& Text[Index] != TEXT(']')
			&& !IsJsonSpace(Text[Index]))
		{
			++Index;
		}

		return Index - 1;
	}

	/**
	 * Cuts a span along with the line it sits on and whichever comma joins it to its neighbours.
	 *
	 * Shared by removing an entry from an array and removing a field from an entry, because the
	 * awkward part is the same in both: the last item in a list carries no comma of its own, so
	 * cutting only what it spans leaves the item before it ending in one — and a trailing comma is
	 * not JSON. That would surface at the next --seed rather than here, with the file written.
	 */
	FString CutSpanWithLine(const FString& Text, const int32 SpanStart, const int32 SpanEnd)
	{
		int32 CutStart = SpanStart;

		while (CutStart > 0
			&& (Text[CutStart - 1] == TEXT(' ') || Text[CutStart - 1] == TEXT('\t')))
		{
			--CutStart;
		}

		if (CutStart > 0 && Text[CutStart - 1] != TEXT('\n'))
		{
			CutStart = SpanStart;
		}

		int32 CutEnd = SpanEnd + 1;

		while (CutEnd < Text.Len() && (Text[CutEnd] == TEXT(' ') || Text[CutEnd] == TEXT('\t')))
		{
			++CutEnd;
		}

		bool bTookComma = false;

		if (CutEnd < Text.Len() && Text[CutEnd] == TEXT(','))
		{
			++CutEnd;
			bTookComma = true;
		}

		while (CutEnd < Text.Len() && (Text[CutEnd] == TEXT(' ') || Text[CutEnd] == TEXT('\t')))
		{
			++CutEnd;
		}

		if (CutEnd < Text.Len() && Text[CutEnd] == TEXT('\r'))
		{
			++CutEnd;
		}

		if (CutEnd < Text.Len() && Text[CutEnd] == TEXT('\n'))
		{
			++CutEnd;
		}

		if (!bTookComma)
		{
			int32 Probe = CutStart - 1;

			while (Probe >= 0 && IsJsonSpace(Text[Probe]))
			{
				--Probe;
			}

			if (Probe >= 0 && Text[Probe] == TEXT(','))
			{
				CutStart = Probe;
			}
		}

		return Text.Left(CutStart) + Text.Mid(CutEnd);
	}

	FString EscapeForJson(const FString& Value)
	{
		FString Escaped = Value;

		Escaped.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
		Escaped.ReplaceInline(TEXT("\""), TEXT("\\\""));

		return Escaped;
	}

	/** "r,g,b" as authored in a body's appearance. */
	bool ParseColour(const FString& Value, FLinearColor& OutColour)
	{
		TArray<FString> Parts;

		Value.ParseIntoArray(Parts, TEXT(","), true);

		if (Parts.Num() != 3)
		{
			return false;
		}

		OutColour = FLinearColor(
			FCString::Atof(*Parts[0].TrimStartAndEnd()),
			FCString::Atof(*Parts[1].TrimStartAndEnd()),
			FCString::Atof(*Parts[2].TrimStartAndEnd()));

		return true;
	}

	FVector ReadDirection(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;

		if (!Object->TryGetArrayField(Field, Values) || Values == nullptr || Values->Num() != 3)
		{
			return FVector::ZeroVector;
		}

		return FVector(
			(*Values)[0]->AsNumber(),
			(*Values)[1]->AsNumber(),
			(*Values)[2]->AsNumber());
	}
}

FString FSpaceMMOWorldDocument::DefaultPath()
{
	// The client project lives under client/, and data/ is its sibling. Content is not inside the
	// Unreal project on purpose: the C# seeder reads the same file, and a copy under Content/ would
	// be a second source of truth for exactly the thing task 96 says must have one.
	return FPaths::ConvertRelativePathToFull(
		FPaths::Combine(
			FPaths::ProjectDir(), TEXT(".."), TEXT("data"), TEXT("universe"), TEXT("origin.json")));
}

bool FSpaceMMOWorldDocument::Load(const FString& InPath, FString& OutError)
{
	FString Contents;

	if (!FFileHelper::LoadFileToString(Contents, *InPath))
	{
		OutError = FString::Printf(TEXT("Could not read %s"), *InPath);

		return false;
	}

	return LoadFromText(Contents, InPath, OutError);
}

bool FSpaceMMOWorldDocument::LoadFromText(
	const FString& InText, const FString& InPath, FString& OutError)
{
	Text.Empty();
	Path.Empty();
	Bodies.Empty();
	Placeables.Empty();

	TSharedPtr<FJsonObject> Root;

	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(InText);

	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = FString::Printf(TEXT("%s is not valid JSON"), *InPath);

		return false;
	}

	Text = InText;
	Path = InPath;

	const TArray<TSharedPtr<FJsonValue>>* BodyValues = nullptr;

	if (Root->TryGetArrayField(TEXT("bodies"), BodyValues) && BodyValues != nullptr)
	{
		for (const TSharedPtr<FJsonValue>& Value : *BodyValues)
		{
			const TSharedPtr<FJsonObject> Object = Value.IsValid() ? Value->AsObject() : nullptr;

			if (!Object.IsValid())
			{
				continue;
			}

			FSpaceMMOAuthoredBody Body;

			Object->TryGetStringField(TEXT("key"), Body.Key);
			Object->TryGetStringField(TEXT("name"), Body.Name);
			Object->TryGetStringField(TEXT("system"), Body.SystemKey);
			Object->TryGetNumberField(TEXT("radiusKm"), Body.RadiusKilometres);

			const TSharedPtr<FJsonObject>* Terrain = nullptr;

			if (Object->TryGetObjectField(TEXT("terrain"), Terrain) && Terrain != nullptr)
			{
				Body.bHasTerrain = true;

				double Seed = 1.0;

				(*Terrain)->TryGetNumberField(TEXT("seed"), Seed);
				(*Terrain)->TryGetNumberField(TEXT("maxElevationKm"), Body.MaxElevationKilometres);
				(*Terrain)->TryGetNumberField(TEXT("baseFrequency"), Body.BaseFrequency);

				Body.TerrainSeed = static_cast<int64>(Seed);
			}

			const TSharedPtr<FJsonObject>* Appearance = nullptr;

			if (Object->TryGetObjectField(TEXT("appearance"), Appearance) && Appearance != nullptr)
			{
				FString Low;
				FString High;
				FString Rock;

				const bool bRead =
					(*Appearance)->TryGetStringField(TEXT("lowColour"), Low)
					&& (*Appearance)->TryGetStringField(TEXT("highColour"), High)
					&& (*Appearance)->TryGetStringField(TEXT("rockColour"), Rock)
					&& ParseColour(Low, Body.LowColour)
					&& ParseColour(High, Body.HighColour)
					&& ParseColour(Rock, Body.RockColour);

				if (bRead)
				{
					double HeightFrom = 0.0;
					double HeightTo = 1.0;
					double SlopeFrom = 0.0;
					double SlopeTo = 1.0;

					(*Appearance)->TryGetNumberField(TEXT("heightFrom"), HeightFrom);
					(*Appearance)->TryGetNumberField(TEXT("heightTo"), HeightTo);
					(*Appearance)->TryGetNumberField(TEXT("slopeFrom"), SlopeFrom);
					(*Appearance)->TryGetNumberField(TEXT("slopeTo"), SlopeTo);

					Body.PaletteRanges = FVector4(HeightFrom, HeightTo, SlopeFrom, SlopeTo);
					Body.bHasAppearance = true;
				}
			}

			if (!Body.Key.IsEmpty())
			{
				Bodies.Add(Body);
			}
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* NodeValues = nullptr;

	if (Root->TryGetArrayField(TEXT("resourceNodes"), NodeValues) && NodeValues != nullptr)
	{
		for (const TSharedPtr<FJsonValue>& Value : *NodeValues)
		{
			const TSharedPtr<FJsonObject> Object = Value.IsValid() ? Value->AsObject() : nullptr;

			if (!Object.IsValid())
			{
				continue;
			}

			FSpaceMMOAuthoredPlaceable Node;

			Node.Kind = ESpaceMMOPlaceableKind::Deposit;

			Object->TryGetStringField(TEXT("key"), Node.Key);
			Object->TryGetStringField(TEXT("body"), Node.BodyKey);
			Object->TryGetStringField(TEXT("item"), Node.Item);
			Object->TryGetStringField(TEXT("skill"), Node.Skill);
			Object->TryGetStringField(TEXT("requiredTool"), Node.RequiredTool);
			Object->TryGetNumberField(TEXT("requiredLevel"), Node.RequiredLevel);
			Object->TryGetNumberField(TEXT("quantityMax"), Node.QuantityMax);
			Object->TryGetNumberField(TEXT("respawnSeconds"), Node.RespawnSeconds);

			Node.Direction = ReadDirection(Object, TEXT("direction"));

			if (!Node.Key.IsEmpty())
			{
				Placeables.Add(Node);
			}
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* StationValues = nullptr;

	if (Root->TryGetArrayField(TEXT("stations"), StationValues) && StationValues != nullptr)
	{
		for (const TSharedPtr<FJsonValue>& Value : *StationValues)
		{
			const TSharedPtr<FJsonObject> Object = Value.IsValid() ? Value->AsObject() : nullptr;

			if (!Object.IsValid())
			{
				continue;
			}

			FSpaceMMOAuthoredPlaceable Station;

			Station.Kind = ESpaceMMOPlaceableKind::Station;

			Object->TryGetStringField(TEXT("key"), Station.Key);
			Object->TryGetStringField(TEXT("name"), Station.Name);
			Object->TryGetStringField(TEXT("system"), Station.SystemKey);
			Object->TryGetStringField(TEXT("body"), Station.BodyKey);
			Object->TryGetStringField(TEXT("kind"), Station.StationKind);
			Object->TryGetNumberField(TEXT("dockingRangeKm"), Station.DockingRangeKilometres);

			Station.Direction = ReadDirection(Object, TEXT("direction"));

			// A station with no body floats in the system and is placed by systemPosition instead.
			// There is no body to stand it on and no direction to edit, so the tool leaves it alone
			// rather than showing a marker that could not mean anything.
			if (!Station.Key.IsEmpty() && !Station.BodyKey.IsEmpty())
			{
				Placeables.Add(Station);
			}
		}
	}

	return true;
}

TArray<FSpaceMMOAuthoredPlaceable> FSpaceMMOWorldDocument::PlaceablesOn(
	const FString& BodyKey) const
{
	TArray<FSpaceMMOAuthoredPlaceable> Found;

	for (const FSpaceMMOAuthoredPlaceable& Placeable : Placeables)
	{
		if (Placeable.BodyKey == BodyKey)
		{
			Found.Add(Placeable);
		}
	}

	return Found;
}

const FSpaceMMOAuthoredBody* FSpaceMMOWorldDocument::FindBody(const FString& BodyKey) const
{
	return Bodies.FindByPredicate(
		[&BodyKey](const FSpaceMMOAuthoredBody& Body) { return Body.Key == BodyKey; });
}

const TCHAR* FSpaceMMOWorldDocument::ArrayNameFor(const ESpaceMMOPlaceableKind Kind)
{
	return Kind == ESpaceMMOPlaceableKind::Station ? TEXT("stations") : TEXT("resourceNodes");
}

FString FSpaceMMOWorldDocument::FormatDirection(const FVector& Direction)
{
	return FString::Printf(TEXT("[%.6f, %.6f, %.6f]"), Direction.X, Direction.Y, Direction.Z);
}

FString FSpaceMMOWorldDocument::FormatEntry(
	const FSpaceMMOAuthoredPlaceable& Entry, const FString& Indent)
{
	const FString Field = Indent + TEXT("  ");

	TArray<FString> Lines;

	if (Entry.Kind == ESpaceMMOPlaceableKind::Deposit)
	{
		// Field order matches the deposits already in the file, so a new entry reads like its
		// neighbours in a diff rather than announcing which tool wrote it.
		Lines.Add(FString::Printf(TEXT("\"key\": \"%s\","), *EscapeForJson(Entry.Key)));
		Lines.Add(FString::Printf(TEXT("\"body\": \"%s\","), *EscapeForJson(Entry.BodyKey)));
		Lines.Add(FString::Printf(TEXT("\"item\": \"%s\","), *EscapeForJson(Entry.Item)));
		Lines.Add(FString::Printf(TEXT("\"skill\": \"%s\","), *EscapeForJson(Entry.Skill)));

		if (!Entry.RequiredTool.IsEmpty())
		{
			Lines.Add(FString::Printf(
				TEXT("\"requiredTool\": \"%s\","), *EscapeForJson(Entry.RequiredTool)));
		}

		Lines.Add(FString::Printf(TEXT("\"requiredLevel\": %d,"), Entry.RequiredLevel));
		Lines.Add(FString::Printf(TEXT("\"quantityMax\": %d,"), Entry.QuantityMax));
		Lines.Add(FString::Printf(TEXT("\"respawnSeconds\": %d,"), Entry.RespawnSeconds));
		Lines.Add(FString::Printf(TEXT("\"direction\": %s"), *FormatDirection(Entry.Direction)));
	}
	else
	{
		Lines.Add(FString::Printf(TEXT("\"key\": \"%s\","), *EscapeForJson(Entry.Key)));
		Lines.Add(FString::Printf(TEXT("\"name\": \"%s\","), *EscapeForJson(Entry.Name)));
		Lines.Add(FString::Printf(TEXT("\"system\": \"%s\","), *EscapeForJson(Entry.SystemKey)));
		Lines.Add(FString::Printf(TEXT("\"body\": \"%s\","), *EscapeForJson(Entry.BodyKey)));
		Lines.Add(FString::Printf(TEXT("\"kind\": \"%s\","), *EscapeForJson(Entry.StationKind)));
		Lines.Add(FString::Printf(TEXT("\"direction\": %s,"), *FormatDirection(Entry.Direction)));
		Lines.Add(FString::Printf(
			TEXT("\"dockingRangeKm\": %s"),
			*FString::SanitizeFloat(Entry.DockingRangeKilometres)));
	}

	FString Body;

	for (const FString& Line : Lines)
	{
		Body += Field + Line + TEXT("\n");
	}

	return TEXT("{\n") + Body + Indent + TEXT("}");
}

bool FSpaceMMOWorldDocument::HasEntry(const FString& InText, const FString& Key)
{
	int32 Open = INDEX_NONE;
	int32 Close = INDEX_NONE;

	return FindEntry(InText, TEXT("resourceNodes"), Key, Open, Close)
		|| FindEntry(InText, TEXT("stations"), Key, Open, Close);
}

FString FSpaceMMOWorldDocument::QuotedOrEmpty(const FString& Value)
{
	return Value.IsEmpty()
		? FString()
		: FString::Printf(TEXT("\"%s\""), *EscapeForJson(Value));
}

bool FSpaceMMOWorldDocument::HasDanglingComma(const FString& InText)
{
	int32 Index = 0;

	while (Index < InText.Len())
	{
		const TCHAR Character = InText[Index];

		if (Character == TEXT('"'))
		{
			Index = SkipString(InText, Index);

			continue;
		}

		if (Character == TEXT(','))
		{
			int32 Probe = Index + 1;

			while (Probe < InText.Len() && IsJsonSpace(InText[Probe]))
			{
				++Probe;
			}

			if (Probe < InText.Len() && (InText[Probe] == TEXT(']') || InText[Probe] == TEXT('}')))
			{
				return true;
			}
		}

		++Index;
	}

	return false;
}

bool FSpaceMMOWorldDocument::SetDirection(
	const FString& InText,
	const TCHAR* ArrayName,
	const FString& Key,
	const FVector& Direction,
	FString& OutText,
	FString& OutError)
{
	return SetField(
		InText, ArrayName, Key, TEXT("direction"), FormatDirection(Direction), OutText, OutError);
}

bool FSpaceMMOWorldDocument::SetField(
	const FString& InText,
	const TCHAR* ArrayName,
	const FString& Key,
	const FString& Field,
	const FString& RawValue,
	FString& OutText,
	FString& OutError)
{
	int32 Open = INDEX_NONE;
	int32 Close = INDEX_NONE;

	if (!FindEntry(InText, ArrayName, Key, Open, Close))
	{
		OutError = FString::Printf(TEXT("No entry '%s' in %s"), *Key, ArrayName);

		return false;
	}

	int32 KeyStart = INDEX_NONE;

	const int32 Value = FindFieldValue(InText, Open + 1, Close, Field, &KeyStart);

	if (Value != INDEX_NONE)
	{
		const int32 ValueEnd = EndOfValue(InText, Value, Close);

		if (ValueEnd < Value)
		{
			OutError = FString::Printf(
				TEXT("Entry '%s' has an unreadable '%s'"), *Key, *Field);

			return false;
		}

		if (RawValue.IsEmpty())
		{
			// An emptied optional field is dropped rather than written as "". A deposit with
			// "requiredTool": "" would be gated on a tool with no key, which is a tool nobody can
			// ever hold, and it would look like an authored gate rather than a mistake.
			OutText = CutSpanWithLine(InText, KeyStart, ValueEnd);

			return true;
		}

		OutText = InText.Left(Value) + RawValue + InText.Mid(ValueEnd + 1);

		return true;
	}

	if (RawValue.IsEmpty())
	{
		// Absent and wanted absent. Not an error: clearing a field that was never there is what
		// the author asked for.
		OutText = InText;

		return true;
	}

	// Added above the direction, which every entry ends with. Anywhere else risks landing between
	// a comment and the field it is explaining.
	int32 AnchorKey = INDEX_NONE;

	const int32 Anchor = FindFieldValue(InText, Open + 1, Close, TEXT("direction"), &AnchorKey);

	if (Anchor == INDEX_NONE || AnchorKey == INDEX_NONE)
	{
		OutError = FString::Printf(
			TEXT("Entry '%s' has no direction to add '%s' beside"), *Key, *Field);

		return false;
	}

	const FString Indent = IndentOfLine(InText, AnchorKey);
	const FString Newline = DetectNewline(InText);

	const FString Line =
		FString::Printf(TEXT("\"%s\": %s,"), *EscapeForJson(Field), *RawValue);

	OutText = InText.Left(AnchorKey - Indent.Len())
		+ Indent + Line + Newline
		+ InText.Mid(AnchorKey - Indent.Len());

	return true;
}

bool FSpaceMMOWorldDocument::RemoveEntry(
	const FString& InText,
	const TCHAR* ArrayName,
	const FString& Key,
	FString& OutText,
	FString& OutError)
{
	int32 Open = INDEX_NONE;
	int32 Close = INDEX_NONE;

	if (!FindEntry(InText, ArrayName, Key, Open, Close))
	{
		OutError = FString::Printf(TEXT("No entry '%s' in %s"), *Key, ArrayName);

		return false;
	}

	OutText = CutSpanWithLine(InText, Open, Close);

	return true;
}

bool FSpaceMMOWorldDocument::AppendEntry(
	const FString& InText,
	const FSpaceMMOAuthoredPlaceable& Entry,
	FString& OutText,
	FString& OutError)
{
	const TCHAR* const ArrayName = ArrayNameFor(Entry.Kind);

	int32 ArrayOpen = INDEX_NONE;
	int32 ArrayClose = INDEX_NONE;

	if (!FindArray(InText, ArrayName, ArrayOpen, ArrayClose))
	{
		OutError = FString::Printf(TEXT("The file has no %s array"), ArrayName);

		return false;
	}

	int32 LastOpen = INDEX_NONE;
	int32 LastClose = INDEX_NONE;

	ForEachObject(InText, ArrayOpen, ArrayClose,
		[&LastOpen, &LastClose](const int32 Open, const int32 Close)
		{
			LastOpen = Open;
			LastClose = Close;

			return true;
		});

	const FString Newline = DetectNewline(InText);

	if (LastOpen == INDEX_NONE)
	{
		// An empty array. Indent one step in from the line the array itself is on, which is the
		// only thing there is to take a lead from.
		const FString ArrayIndent = IndentOfLine(InText, ArrayOpen);
		const FString Indent = ArrayIndent + TEXT("  ");

		FString Written = FormatEntry(Entry, Indent);

		Written.ReplaceInline(TEXT("\n"), *Newline);

		OutText = InText.Left(ArrayOpen + 1)
			+ Newline + Indent + Written + Newline + ArrayIndent
			+ InText.Mid(ArrayClose);

		return true;
	}

	const FString Indent = IndentOfLine(InText, LastOpen);

	FString Written = FormatEntry(Entry, Indent);

	Written.ReplaceInline(TEXT("\n"), *Newline);

	OutText = InText.Left(LastClose + 1)
		+ TEXT(",") + Newline + Indent + Written
		+ InText.Mid(LastClose + 1);

	return true;
}

bool FSpaceMMOWorldDocument::ApplyEdits(
	const FString& InText,
	const TArray<FSpaceMMOWorldEdit>& Edits,
	FString& OutText,
	FString& OutError)
{
	FString Working = InText;

	for (const FSpaceMMOWorldEdit& Edit : Edits)
	{
		FString Next;

		bool bApplied = false;

		switch (Edit.Kind)
		{
		case ESpaceMMOWorldEditKind::SetDirection:
			bApplied = SetDirection(
				Working,
				ArrayNameFor(Edit.Entry.Kind),
				Edit.ExistingKey,
				Edit.Entry.Direction,
				Next,
				OutError);

			break;

		case ESpaceMMOWorldEditKind::SetField:
			bApplied = SetField(
				Working,
				ArrayNameFor(Edit.Entry.Kind),
				Edit.ExistingKey,
				Edit.Field,
				Edit.RawValue,
				Next,
				OutError);

			break;

		case ESpaceMMOWorldEditKind::Remove:
			bApplied = RemoveEntry(
				Working, ArrayNameFor(Edit.Entry.Kind), Edit.ExistingKey, Next, OutError);

			break;

		case ESpaceMMOWorldEditKind::Append:
			bApplied = AppendEntry(Working, Edit.Entry, Next, OutError);

			break;
		}

		if (!bApplied)
		{
			// The original, not what was managed so far. Half a set of moves on disk would agree
			// with neither the editor nor with what the author thinks they did, and there would be
			// nothing to say which half landed.
			OutText = InText;

			return false;
		}

		Working = MoveTemp(Next);
	}

	OutText = MoveTemp(Working);

	return true;
}
