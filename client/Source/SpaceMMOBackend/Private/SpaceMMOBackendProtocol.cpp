#include "SpaceMMOBackendProtocol.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	/** Largest integer a double represents exactly: 2^53 - 1. */
	constexpr int64 MaxExactJsonInteger = 9007199254740991LL;

	/** Parses a JSON document into an object, or returns null. */
	TSharedPtr<FJsonObject> ParseObject(const FString& Json)
	{
		TSharedPtr<FJsonObject> Object;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);

		return FJsonSerializer::Deserialize(Reader, Object) && Object.IsValid() ? Object : nullptr;
	}

	/** Parses a JSON document into an array. */
	bool ParseArray(const FString& Json, TArray<TSharedPtr<FJsonValue>>& OutValues)
	{
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);

		return FJsonSerializer::Deserialize(Reader, OutValues);
	}

	/** Clamps an integer to a valid enum member, so an unknown value cannot index off the end. */
	template <typename EnumType>
	EnumType ToEnum(const int64 Value, const EnumType Fallback, const int64 MaxValue)
	{
		return Value >= 0 && Value <= MaxValue ? static_cast<EnumType>(Value) : Fallback;
	}
}

FString FSpaceMMOBackendProtocol::JoinUrl(const FString& BaseUrl, const FString& Path)
{
	FString Base = BaseUrl;
	FString Tail = Path;

	// Trim both sides and put back exactly one slash. Doubling it produces a URL that some servers
	// route and others 404, which is a miserable class of bug to chase.
	while (Base.EndsWith(TEXT("/")))
	{
		Base.LeftChopInline(1);
	}

	while (Tail.StartsWith(TEXT("/")))
	{
		Tail.RightChopInline(1);
	}

	return Tail.IsEmpty() ? Base : Base + TEXT("/") + Tail;
}

FString FSpaceMMOBackendProtocol::MakeCredentialsBody(const FString& Email, const FString& Password)
{
	FString Output;

	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);

	// Built through the writer rather than by string concatenation, so a password containing a
	// quote or a backslash produces valid JSON instead of a malformed body and a baffling 400.
	Writer->WriteObjectStart();
	Writer->WriteValue(TEXT("email"), Email);
	Writer->WriteValue(TEXT("password"), Password);
	Writer->WriteObjectEnd();
	Writer->Close();

	return Output;
}

FString FSpaceMMOBackendProtocol::MakeCreateCharacterBody(const FString& Name, const EBackendRace Race)
{
	FString Output;

	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);

	Writer->WriteObjectStart();
	Writer->WriteValue(TEXT("name"), Name);
	Writer->WriteValue(TEXT("race"), static_cast<int32>(Race));
	Writer->WriteObjectEnd();
	Writer->Close();

	return Output;
}

bool FSpaceMMOBackendProtocol::ReadInt64(
	const TSharedPtr<FJsonObject>& Object, const FString& Field, int64& OutValue)
{
	if (!Object.IsValid())
	{
		return false;
	}

	// Dispatched on the value's actual JSON type, not through TryGetStringField.
	//
	// That accessor *coerces*: asked for a string, it will happily stringify a number. So a
	// numeric field took the string path, was parsed with full precision from an already-rounded
	// double, and skipped the exactness check below entirely — which is exactly the bug this
	// function exists to prevent, and the LargeNumbers test caught it.
	const TSharedPtr<FJsonValue> Value = Object->TryGetField(Field);

	if (!Value.IsValid())
	{
		return false;
	}

	// A string is accepted as well as a number, so the server can send values beyond the exact
	// double range without the client having to guess.
	if (Value->Type == EJson::String)
	{
		return LexTryParseString(OutValue, *Value->AsString());
	}

	if (Value->Type != EJson::Number)
	{
		return false;
	}

	const double AsDouble = Value->AsNumber();

	if (FMath::Abs(AsDouble) > static_cast<double>(MaxExactJsonInteger)
		|| AsDouble != FMath::TruncToDouble(AsDouble))
	{
		// Past 2^53 a double cannot represent consecutive integers, so accepting this would be
		// accepting a number that is quietly not the one the server sent.
		return false;
	}

	OutValue = static_cast<int64>(AsDouble);

	return true;
}

bool FSpaceMMOBackendProtocol::ParseSession(const FString& Json, FBackendSession& OutSession)
{
	const TSharedPtr<FJsonObject> Object = ParseObject(Json);

	if (!Object.IsValid())
	{
		return false;
	}

	int64 AccountId = 0;
	FString Token;

	if (!ReadInt64(Object, TEXT("accountId"), AccountId)
		|| !Object->TryGetStringField(TEXT("token"), Token)
		|| Token.IsEmpty())
	{
		return false;
	}

	OutSession.AccountId = static_cast<int32>(AccountId);
	OutSession.Token = Token;

	// Expiry is informational — the server decides what is expired. A missing or unparseable one
	// must not fail the login, or a date-format change locks every player out.
	FString ExpiresAt;

	if (Object->TryGetStringField(TEXT("expiresAt"), ExpiresAt))
	{
		FDateTime::ParseIso8601(*ExpiresAt, OutSession.ExpiresAt);
	}

	return true;
}

bool FSpaceMMOBackendProtocol::ParseCharacter(
	const TSharedPtr<FJsonObject>& Object, FBackendCharacter& OutCharacter)
{
	if (!Object.IsValid())
	{
		return false;
	}

	int64 Id = 0;
	FString Name;

	if (!ReadInt64(Object, TEXT("id"), Id) || !Object->TryGetStringField(TEXT("name"), Name))
	{
		return false;
	}

	OutCharacter.Id = static_cast<int32>(Id);
	OutCharacter.Name = Name;

	int64 Race = 0;
	int64 Faction = 0;
	int64 HomeBodyId = 0;

	if (ReadInt64(Object, TEXT("race"), Race))
	{
		OutCharacter.Race = ToEnum(Race, EBackendRace::Humanoid, 3);
	}

	if (ReadInt64(Object, TEXT("faction"), Faction))
	{
		OutCharacter.Faction = ToEnum(Faction, EBackendFaction::A, 1);
	}

	if (ReadInt64(Object, TEXT("homeBodyId"), HomeBodyId))
	{
		OutCharacter.HomeBodyId = static_cast<int32>(HomeBodyId);
	}

	ReadInt64(Object, TEXT("balanceMinorUnits"), OutCharacter.BalanceMinorUnits);

	return true;
}

bool FSpaceMMOBackendProtocol::ParseCharacterList(
	const FString& Json, TArray<FBackendCharacter>& OutCharacters)
{
	TArray<TSharedPtr<FJsonValue>> Values;

	if (!ParseArray(Json, Values))
	{
		return false;
	}

	OutCharacters.Reset();

	for (const TSharedPtr<FJsonValue>& Value : Values)
	{
		FBackendCharacter Character;

		if (Value.IsValid() && ParseCharacter(Value->AsObject(), Character))
		{
			OutCharacters.Add(Character);
		}
	}

	return true;
}

bool FSpaceMMOBackendProtocol::ParseSkills(const FString& Json, TArray<FBackendSkill>& OutSkills)
{
	TArray<TSharedPtr<FJsonValue>> Values;

	if (!ParseArray(Json, Values))
	{
		return false;
	}

	OutSkills.Reset();

	for (const TSharedPtr<FJsonValue>& Value : Values)
	{
		if (!Value.IsValid())
		{
			continue;
		}

		const TSharedPtr<FJsonObject> Object = Value->AsObject();

		FBackendSkill Skill;

		if (!Object.IsValid() || !Object->TryGetStringField(TEXT("key"), Skill.Key))
		{
			continue;
		}

		Object->TryGetStringField(TEXT("name"), Skill.Name);

		int64 Category = 0;
		int64 Level = 1;

		if (ReadInt64(Object, TEXT("category"), Category))
		{
			Skill.Category = ToEnum(Category, EBackendSkillCategory::Life, 2);
		}

		ReadInt64(Object, TEXT("xp"), Skill.Xp);

		if (ReadInt64(Object, TEXT("level"), Level))
		{
			Skill.Level = static_cast<int32>(Level);
		}

		OutSkills.Add(Skill);
	}

	return true;
}

bool FSpaceMMOBackendProtocol::ParseInventory(
	const FString& Json, TArray<FBackendInventoryItem>& OutItems)
{
	TArray<TSharedPtr<FJsonValue>> Values;

	if (!ParseArray(Json, Values))
	{
		return false;
	}

	OutItems.Reset();

	for (const TSharedPtr<FJsonValue>& Value : Values)
	{
		if (!Value.IsValid())
		{
			continue;
		}

		const TSharedPtr<FJsonObject> Object = Value->AsObject();

		FBackendInventoryItem Item;

		if (!Object.IsValid() || !Object->TryGetStringField(TEXT("itemKey"), Item.ItemKey))
		{
			continue;
		}

		Object->TryGetStringField(TEXT("name"), Item.Name);

		int64 ItemDefId = 0;
		int64 Quantity = 0;

		if (ReadInt64(Object, TEXT("itemDefId"), ItemDefId))
		{
			Item.ItemDefId = static_cast<int32>(ItemDefId);
		}

		if (ReadInt64(Object, TEXT("quantity"), Quantity))
		{
			Item.Quantity = static_cast<int32>(Quantity);
		}

		OutItems.Add(Item);
	}

	return true;
}

bool FSpaceMMOBackendProtocol::ParseBodies(const FString& Json, TArray<FBackendBody>& OutBodies)
{
	TArray<TSharedPtr<FJsonValue>> Values;

	if (!ParseArray(Json, Values))
	{
		return false;
	}

	OutBodies.Reset();

	for (const TSharedPtr<FJsonValue>& Value : Values)
	{
		if (!Value.IsValid())
		{
			continue;
		}

		const TSharedPtr<FJsonObject> Object = Value->AsObject();

		FBackendBody Body;

		if (!Object.IsValid() || !Object->TryGetStringField(TEXT("key"), Body.Key))
		{
			continue;
		}

		Object->TryGetStringField(TEXT("name"), Body.Name);
		Object->TryGetNumberField(TEXT("radiusKm"), Body.RadiusKilometres);

		int64 Id = 0;
		int64 StarSystemId = 0;

		if (ReadInt64(Object, TEXT("id"), Id))
		{
			Body.Id = static_cast<int32>(Id);
		}

		if (ReadInt64(Object, TEXT("starSystemId"), StarSystemId))
		{
			Body.StarSystemId = static_cast<int32>(StarSystemId);
		}

		OutBodies.Add(Body);
	}

	return true;
}

bool FSpaceMMOBackendProtocol::ParseResourceNodes(
	const FString& Json, TArray<FBackendResourceNode>& OutNodes)
{
	TArray<TSharedPtr<FJsonValue>> Values;

	if (!ParseArray(Json, Values))
	{
		return false;
	}

	OutNodes.Reset();

	for (const TSharedPtr<FJsonValue>& Value : Values)
	{
		if (!Value.IsValid())
		{
			continue;
		}

		const TSharedPtr<FJsonObject> Object = Value->AsObject();

		FBackendResourceNode Node;

		if (!Object.IsValid() || !Object->TryGetStringField(TEXT("key"), Node.Key))
		{
			continue;
		}

		// The direction is required, and required to be usable. Everything else about a deposit
		// can be missing and still leave something drawable; without a direction there is nowhere
		// to draw it. Read before anything else is stored, so a bad one costs nothing.
		double DirectionX = 0.0;
		double DirectionY = 0.0;
		double DirectionZ = 0.0;

		if (!Object->TryGetNumberField(TEXT("directionX"), DirectionX)
			|| !Object->TryGetNumberField(TEXT("directionY"), DirectionY)
			|| !Object->TryGetNumberField(TEXT("directionZ"), DirectionZ))
		{
			continue;
		}

		Node.Direction = FVector(DirectionX, DirectionY, DirectionZ);

		if (Node.Direction.IsNearlyZero())
		{
			continue;
		}

		// Normalised on arrival even though the server normalises on load and a check constraint
		// stands behind that. It costs one square root per deposit per session, and the failure it
		// prevents — every deposit on the planet displaced by a common factor — would look like a
		// terrain bug rather than a parsing one.
		Node.Direction = Node.Direction.GetSafeNormal();

		Object->TryGetStringField(TEXT("itemKey"), Node.ItemKey);
		Object->TryGetStringField(TEXT("itemName"), Node.ItemName);
		Object->TryGetStringField(TEXT("skillKey"), Node.SkillKey);

		int64 Id = 0;
		int64 BodyId = 0;
		int64 RequiredLevel = 0;
		int64 QuantityMax = 0;

		if (ReadInt64(Object, TEXT("id"), Id))
		{
			Node.Id = Id;
		}

		if (ReadInt64(Object, TEXT("bodyId"), BodyId))
		{
			Node.BodyId = static_cast<int32>(BodyId);
		}

		if (ReadInt64(Object, TEXT("requiredLevel"), RequiredLevel))
		{
			Node.RequiredLevel = static_cast<int32>(RequiredLevel);
		}

		if (ReadInt64(Object, TEXT("quantityMax"), QuantityMax))
		{
			Node.QuantityMax = static_cast<int32>(QuantityMax);
		}

		OutNodes.Add(Node);
	}

	return true;
}

FString FSpaceMMOBackendProtocol::ExtractErrorMessage(const FString& Body)
{
	const TSharedPtr<FJsonObject> Object = ParseObject(Body);

	if (!Object.IsValid())
	{
		return FString();
	}

	FString Message;

	if (Object->TryGetStringField(TEXT("error"), Message))
	{
		return Message;
	}

	// RFC 7807 problem details, which is what AddProblemDetails produces. Detail is the specific
	// one, title the general — prefer the specific.
	if (Object->TryGetStringField(TEXT("detail"), Message) && !Message.IsEmpty())
	{
		return Message;
	}

	Object->TryGetStringField(TEXT("title"), Message);

	return Message;
}

FBackendFailure FSpaceMMOBackendProtocol::ClassifyFailure(const int32 HttpStatus, const FString& Body)
{
	FBackendFailure Failure;
	Failure.HttpStatus = HttpStatus;
	Failure.Message = ExtractErrorMessage(Body);

	if (HttpStatus == 0)
	{
		Failure.Error = EBackendError::Transport;
		Failure.Message = TEXT("Could not reach the server.");

		return Failure;
	}

	if (HttpStatus >= 200 && HttpStatus < 300)
	{
		Failure.Error = EBackendError::None;

		return Failure;
	}

	if (HttpStatus == 401)
	{
		Failure.Error = EBackendError::Unauthenticated;
	}
	else if (HttpStatus == 404)
	{
		Failure.Error = EBackendError::NotFound;
	}
	else if (HttpStatus >= 400 && HttpStatus < 500)
	{
		Failure.Error = EBackendError::Rejected;
	}
	else
	{
		Failure.Error = EBackendError::Server;
	}

	if (Failure.Message.IsEmpty())
	{
		Failure.Message = FString::Printf(TEXT("Request failed with status %d."), HttpStatus);
	}

	return Failure;
}

FString FBackendCharacter::FormatBalance() const
{
	const int64 Whole = BalanceMinorUnits / 100;
	const int64 Fraction = FMath::Abs(BalanceMinorUnits % 100);

	// Integer arithmetic throughout. Formatting a balance through a float is how a display ends up
	// one credit short of the number the ledger holds.
	return FString::Printf(TEXT("%s.%02lld"), *FText::AsNumber(Whole).ToString(), Fraction);
}
