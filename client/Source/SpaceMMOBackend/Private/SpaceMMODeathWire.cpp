#include "SpaceMMODeathWire.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	TSharedPtr<FJsonObject> ParseObject(const FString& Json)
	{
		TSharedPtr<FJsonObject> Object;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);

		return FJsonSerializer::Deserialize(Reader, Object) && Object.IsValid() ? Object : nullptr;
	}

	/** Largest integer a double represents exactly: 2^53 - 1. */
	constexpr int64 MaxExactJsonInteger = 9007199254740991LL;

	/**
	 * An integer field, read as the protocol's own reader does (its is private to it): by the value's JSON type, a
	 * string accepted too, and a number refused past what a double holds exactly rather than read as the wrong one.
	 */
	bool ReadInt64(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, int64& OutValue)
	{
		const TSharedPtr<FJsonValue> Value = Object.IsValid() ? Object->TryGetField(Field) : nullptr;

		if (!Value.IsValid())
		{
			return false;
		}

		if (Value->Type == EJson::String)
		{
			return LexTryParseString(OutValue, *Value->AsString());
		}

		if (Value->Type != EJson::Number)
		{
			return false;
		}

		const double AsDouble = Value->AsNumber();

		if (FMath::Abs(AsDouble) > static_cast<double>(MaxExactJsonInteger) || AsDouble != FMath::TruncToDouble(AsDouble))
		{
			return false;
		}

		OutValue = static_cast<int64>(AsDouble);

		return true;
	}

	int32 ReadInt(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		int64 Value = 0;

		return ReadInt64(Object, Field, Value) ? static_cast<int32>(Value) : 0;
	}

	/** A style number, held to the members there are, so an unknown one cannot index off the end. */
	EBackendItemStyle ReadStyle(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		const int32 Value = ReadInt(Object, Field);

		return Value >= 0 && Value <= static_cast<int32>(EBackendItemStyle::Grimhold)
			? static_cast<EBackendItemStyle>(Value)
			: EBackendItemStyle::Common;
	}

	FBackendLootItem ReadItem(const TSharedPtr<FJsonObject>& Object)
	{
		FBackendLootItem Item;
		Item.ItemDefId = ReadInt(Object, TEXT("itemDefId"));
		Item.Style = ReadStyle(Object, TEXT("style"));
		Item.ShownStyle = ReadStyle(Object, TEXT("shownStyle"));
		Item.Quantity = ReadInt(Object, TEXT("quantity"));
		Object->TryGetStringField(TEXT("itemKey"), Item.ItemKey);
		Object->TryGetStringField(TEXT("name"), Item.Name);
		Object->TryGetNumberField(TEXT("volumeM3"), Item.VolumeM3);

		return Item;
	}

	TArray<TSharedPtr<FJsonObject>> Objects(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		TArray<TSharedPtr<FJsonObject>> Result;
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;

		if (Object.IsValid() && Object->TryGetArrayField(Field, Values))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Values)
			{
				if (Value.IsValid() && Value->Type == EJson::Object)
				{
					Result.Add(Value->AsObject());
				}
			}
		}

		return Result;
	}

	bool ReadContainer(const TSharedPtr<FJsonObject>& Object, FBackendLootContainer& Out)
	{
		if (!Object.IsValid() || !ReadInt64(Object, TEXT("id"), Out.Id))
		{
			return false;
		}

		Out.DroppedByCharacterId = ReadInt(Object, TEXT("droppedByCharacterId"));
		Object->TryGetStringField(TEXT("droppedBy"), Out.DroppedBy);
		Object->TryGetNumberField(TEXT("x"), Out.PositionKilometres.X);
		Object->TryGetNumberField(TEXT("y"), Out.PositionKilometres.Y);
		Object->TryGetNumberField(TEXT("z"), Out.PositionKilometres.Z);
		Object->TryGetNumberField(TEXT("secondsLeft"), Out.SecondsLeft);

		Out.Items.Reset();

		for (const TSharedPtr<FJsonObject>& Item : Objects(Object, TEXT("items")))
		{
			Out.Items.Add(ReadItem(Item));
		}

		return true;
	}

	/** A field holding an object, or null when it is absent or JSON null. */
	TSharedPtr<FJsonObject> ObjectField(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		const TSharedPtr<FJsonValue> Value = Object.IsValid() ? Object->TryGetField(Field) : nullptr;

		return Value.IsValid() && Value->Type == EJson::Object ? Value->AsObject() : nullptr;
	}
}

bool FSpaceMMODeathWire::ParseDeathCosts(const FString& Json, FBackendDeathCosts& Out)
{
	const TSharedPtr<FJsonObject> Object = ParseObject(Json);

	if (!Object.IsValid() || !Object->TryGetBoolField(TEXT("applied"), Out.bApplied))
	{
		return false;
	}

	Object->TryGetBoolField(TEXT("atStation"), Out.bAtStation);

	Out.Lost.Reset();
	Out.Worn.Reset();

	for (const TSharedPtr<FJsonObject>& Line : Objects(Object, TEXT("lost")))
	{
		FBackendDeathLoss Loss;
		Loss.ItemDefId = ReadInt(Line, TEXT("itemDefId"));
		Loss.Style = ReadStyle(Line, TEXT("style"));
		Loss.Dropped = ReadInt(Line, TEXT("dropped"));
		Loss.Destroyed = ReadInt(Line, TEXT("destroyed"));
		Line->TryGetStringField(TEXT("itemKey"), Loss.ItemKey);
		Line->TryGetStringField(TEXT("name"), Loss.Name);

		Out.Lost.Add(Loss);
	}

	for (const TSharedPtr<FJsonObject>& Line : Objects(Object, TEXT("worn")))
	{
		FBackendWornLoss Worn;
		ReadInt64(Line, TEXT("itemInstanceId"), Worn.ItemInstanceId);
		Worn.Before = ReadInt(Line, TEXT("before"));
		Worn.After = ReadInt(Line, TEXT("after"));
		Line->TryGetStringField(TEXT("itemKey"), Worn.ItemKey);
		Line->TryGetStringField(TEXT("name"), Worn.Name);

		Out.Worn.Add(Worn);
	}

	Out.bHasContainer = ReadContainer(ObjectField(Object, TEXT("container")), Out.Container);

	return true;
}

bool FSpaceMMODeathWire::ParseContainers(const FString& Json, TArray<FBackendLootContainer>& Out)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);

	if (!FJsonSerializer::Deserialize(Reader, Values))
	{
		return false;
	}

	Out.Reset();

	for (const TSharedPtr<FJsonValue>& Value : Values)
	{
		FBackendLootContainer Container;

		if (Value.IsValid() && Value->Type == EJson::Object && ReadContainer(Value->AsObject(), Container))
		{
			Out.Add(Container);
		}
	}

	return true;
}

bool FSpaceMMODeathWire::ParseLootResult(const FString& Json, FBackendLootResult& Out)
{
	const TSharedPtr<FJsonObject> Object = ParseObject(Json);

	if (!Object.IsValid() || !Object->TryGetBoolField(TEXT("took"), Out.bTook))
	{
		return false;
	}

	Out.Refusal.Reset();
	Object->TryGetStringField(TEXT("refusal"), Out.Refusal);

	Out.Taken.Reset();

	for (const TSharedPtr<FJsonObject>& Line : Objects(Object, TEXT("taken")))
	{
		Out.Taken.Add(ReadItem(Line));
	}

	Out.bHasLeft = ReadContainer(ObjectField(Object, TEXT("left")), Out.Left);

	return true;
}

FString FSpaceMMODeathWire::MakeDeathBody(const int32 CharacterId, const FVector& PositionKilometres)
{
	return FString::Printf(
		TEXT("{\"characterId\":%d,\"x\":%.9f,\"y\":%.9f,\"z\":%.9f}"),
		CharacterId, PositionKilometres.X, PositionKilometres.Y, PositionKilometres.Z);
}

FString FSpaceMMODeathWire::MakeLootBody(const int32 CharacterId, const TArray<FBackendLootItem>& Wanted)
{
	if (Wanted.IsEmpty())
	{
		return FString::Printf(TEXT("{\"characterId\":%d}"), CharacterId);
	}

	TArray<FString> Items;

	for (const FBackendLootItem& Item : Wanted)
	{
		Items.Add(FString::Printf(
			TEXT("{\"itemDefId\":%d,\"style\":%d}"), Item.ItemDefId, static_cast<int32>(Item.Style)));
	}

	return FString::Printf(
		TEXT("{\"characterId\":%d,\"items\":[%s]}"), CharacterId, *FString::Join(Items, TEXT(",")));
}
