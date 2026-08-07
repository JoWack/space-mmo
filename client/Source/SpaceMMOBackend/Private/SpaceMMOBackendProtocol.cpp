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

		// Absent or null for anything no faction buys, which is most of the catalog. Left at zero,
		// which the client reads as "not sellable" — content forbids a zero price on an item that is
		// bought, so nothing real is hidden by the collapse.
		int64 FactionPrice = 0;

		if (ReadInt64(Object, TEXT("factionBuyPriceMinorUnits"), FactionPrice))
		{
			Item.FactionBuyPriceMinorUnits = FactionPrice;
		}

		int64 Where = 0;

		if (ReadInt64(Object, TEXT("kind"), Where))
		{
			Item.Kind = ToEnum(Where, EBackendInventoryKind::CharacterCarried, 2);
		}

		// Absent for a ship hold, which has no station. Zero reads as "not at a station", which is
		// what the market needs to know and the only distinction it acts on.
		if (ReadInt64(Object, TEXT("stationId"), Where))
		{
			Item.StationId = static_cast<int32>(Where);
		}

		OutItems.Add(Item);
	}

	return true;
}

bool FSpaceMMOBackendProtocol::ParseRecipes(
	const FString& Json, TArray<FBackendRecipe>& OutRecipes)
{
	TArray<TSharedPtr<FJsonValue>> Values;

	if (!ParseArray(Json, Values))
	{
		return false;
	}

	OutRecipes.Reset();

	for (const TSharedPtr<FJsonValue>& Value : Values)
	{
		if (!Value.IsValid())
		{
			continue;
		}

		const TSharedPtr<FJsonObject> Object = Value->AsObject();

		FBackendRecipe Recipe;

		// Dropped rather than defaulted. The key is the only name for a recipe that means the same
		// thing in two differently-seeded databases; without it there is nothing to refer to.
		if (!Object.IsValid() || !Object->TryGetStringField(TEXT("key"), Recipe.Key))
		{
			continue;
		}

		Object->TryGetStringField(TEXT("outputItemKey"), Recipe.OutputItemKey);
		Object->TryGetStringField(TEXT("outputName"), Recipe.OutputName);
		Object->TryGetStringField(TEXT("skillKey"), Recipe.SkillKey);
		Object->TryGetStringField(TEXT("skillName"), Recipe.SkillName);

		// Absent when the recipe needs no tool, which is the ordinary case rather than an error.
		Object->TryGetStringField(TEXT("requiredToolName"), Recipe.RequiredToolName);

		int64 Scratch = 0;

		if (ReadInt64(Object, TEXT("id"), Scratch))
		{
			Recipe.Id = static_cast<int32>(Scratch);
		}

		if (ReadInt64(Object, TEXT("outputQuantity"), Scratch))
		{
			Recipe.OutputQuantity = static_cast<int32>(Scratch);
		}

		if (ReadInt64(Object, TEXT("requiredLevel"), Scratch))
		{
			Recipe.RequiredLevel = static_cast<int32>(Scratch);
		}

		if (ReadInt64(Object, TEXT("jobSeconds"), Scratch))
		{
			Recipe.JobSeconds = static_cast<int32>(Scratch);
		}

		const TArray<TSharedPtr<FJsonValue>>* Inputs = nullptr;

		if (Object->TryGetArrayField(TEXT("inputs"), Inputs) && Inputs != nullptr)
		{
			for (const TSharedPtr<FJsonValue>& InputValue : *Inputs)
			{
				const TSharedPtr<FJsonObject> InputObject =
					InputValue.IsValid() ? InputValue->AsObject() : nullptr;

				FBackendRecipeInput Input;

				if (!InputObject.IsValid()
					|| !InputObject->TryGetStringField(TEXT("itemKey"), Input.ItemKey))
				{
					continue;
				}

				InputObject->TryGetStringField(TEXT("name"), Input.Name);

				if (ReadInt64(InputObject, TEXT("itemDefId"), Scratch))
				{
					Input.ItemDefId = static_cast<int32>(Scratch);
				}

				if (ReadInt64(InputObject, TEXT("quantity"), Scratch))
				{
					Input.Quantity = static_cast<int32>(Scratch);
				}

				Recipe.Inputs.Add(Input);
			}
		}

		OutRecipes.Add(Recipe);
	}

	return true;
}

bool FSpaceMMOBackendProtocol::ParseIndustryJobs(
	const FString& Json, TArray<FBackendIndustryJob>& OutJobs)
{
	TArray<TSharedPtr<FJsonValue>> Values;

	if (!ParseArray(Json, Values))
	{
		return false;
	}

	OutJobs.Reset();

	for (const TSharedPtr<FJsonValue>& Value : Values)
	{
		if (!Value.IsValid())
		{
			continue;
		}

		const TSharedPtr<FJsonObject> Object = Value->AsObject();

		FBackendIndustryJob Job;

		int64 Id = 0;

		// A job with no id cannot be claimed, so it is worse than useless to display: it would
		// show a player something to collect and then refuse every attempt.
		if (!Object.IsValid() || !ReadInt64(Object, TEXT("id"), Id) || Id <= 0)
		{
			continue;
		}

		Job.Id = Id;

		Object->TryGetStringField(TEXT("recipeKey"), Job.RecipeKey);
		Object->TryGetStringField(TEXT("outputName"), Job.OutputName);
		Object->TryGetBoolField(TEXT("isClaimable"), Job.bIsClaimable);

		int64 Scratch = 0;

		if (ReadInt64(Object, TEXT("outputQuantityTotal"), Scratch))
		{
			Job.OutputQuantityTotal = static_cast<int32>(Scratch);
		}

		if (ReadInt64(Object, TEXT("runs"), Scratch))
		{
			Job.Runs = static_cast<int32>(Scratch);
		}

		if (ReadInt64(Object, TEXT("secondsRemaining"), Scratch))
		{
			Job.SecondsRemaining = static_cast<int32>(Scratch);
		}

		OutJobs.Add(Job);
	}

	return true;
}

bool FSpaceMMOBackendProtocol::ParseJournal(
	const FString& Json, TArray<FBackendJournalEntry>& OutEntries)
{
	TArray<TSharedPtr<FJsonValue>> Values;

	if (!ParseArray(Json, Values))
	{
		return false;
	}

	OutEntries.Reset();

	for (const TSharedPtr<FJsonValue>& Value : Values)
	{
		const TSharedPtr<FJsonObject> Object = Value.IsValid() ? Value->AsObject() : nullptr;

		FBackendJournalEntry Entry;

		if (!Object.IsValid() || !Object->TryGetStringField(TEXT("questKey"), Entry.QuestKey))
		{
			continue;
		}

		Object->TryGetStringField(TEXT("name"), Entry.Name);

		// Absent on a quest with no active step, which is an ordinary state rather than a gap.
		Object->TryGetStringField(TEXT("stepDescription"), Entry.StepDescription);
		Object->TryGetStringField(TEXT("stepTargetKey"), Entry.StepTargetKey);

		int64 Scratch = 0;

		if (ReadInt64(Object, TEXT("state"), Scratch))
		{
			Entry.State = ToEnum(Scratch, EBackendQuestState::InProgress, 3);
		}

		if (ReadInt64(Object, TEXT("stepObjective"), Scratch))
		{
			Entry.StepObjective = ToEnum(Scratch, EBackendObjective::Gather, 5);
		}

		if (ReadInt64(Object, TEXT("stepProgress"), Scratch))
		{
			Entry.StepProgress = static_cast<int32>(Scratch);
		}

		if (ReadInt64(Object, TEXT("stepRequired"), Scratch))
		{
			Entry.StepRequired = static_cast<int32>(Scratch);
		}

		OutEntries.Add(Entry);
	}

	return true;
}

bool FSpaceMMOBackendProtocol::ParseAvailableQuests(
	const FString& Json, TArray<FBackendAvailableQuest>& OutQuests)
{
	TArray<TSharedPtr<FJsonValue>> Values;

	if (!ParseArray(Json, Values))
	{
		return false;
	}

	OutQuests.Reset();

	for (const TSharedPtr<FJsonValue>& Value : Values)
	{
		const TSharedPtr<FJsonObject> Object = Value.IsValid() ? Value->AsObject() : nullptr;

		FBackendAvailableQuest Quest;

		// Dropped without a key, because the key is what accepting names. An entry that can be
		// listed but never taken is worse than one that is missing.
		if (!Object.IsValid() || !Object->TryGetStringField(TEXT("questKey"), Quest.QuestKey))
		{
			continue;
		}

		Object->TryGetStringField(TEXT("name"), Quest.Name);

		OutQuests.Add(Quest);
	}

	return true;
}

bool FSpaceMMOBackendProtocol::ParseBook(
	const FString& Json, TArray<FBackendBookEntry>& OutEntries)
{
	TArray<TSharedPtr<FJsonValue>> Values;

	if (!ParseArray(Json, Values))
	{
		return false;
	}

	OutEntries.Reset();

	for (const TSharedPtr<FJsonValue>& Value : Values)
	{
		const TSharedPtr<FJsonObject> Object = Value.IsValid() ? Value->AsObject() : nullptr;

		FBackendBookEntry Entry;

		int64 OrderId = 0;

		// An order with no id cannot be acted on, and a book is a list of things to act on.
		if (!Object.IsValid() || !ReadInt64(Object, TEXT("orderId"), OrderId) || OrderId <= 0)
		{
			continue;
		}

		Entry.OrderId = OrderId;

		int64 Scratch = 0;

		if (ReadInt64(Object, TEXT("side"), Scratch))
		{
			Entry.Side = ToEnum(Scratch, EBackendOrderSide::Buy, 1);
		}

		ReadInt64(Object, TEXT("priceMinorUnits"), Entry.PriceMinorUnits);

		if (ReadInt64(Object, TEXT("quantityRemaining"), Scratch))
		{
			Entry.QuantityRemaining = static_cast<int32>(Scratch);
		}

		OutEntries.Add(Entry);
	}

	return true;
}

FString FSpaceMMOBackendProtocol::MakePlaceOrderBody(
	const int32 CharacterId,
	const int32 StationId,
	const int32 ItemDefId,
	const EBackendOrderSide Side,
	const int64 LimitPriceMinorUnits,
	const int32 Quantity)
{
	return FString::Printf(
		TEXT("{\"characterId\":%d,\"stationId\":%d,\"itemDefId\":%d,\"side\":%d,")
		TEXT("\"limitPriceMinorUnits\":%lld,\"quantity\":%d}"),
		CharacterId,
		StationId,
		ItemDefId,
		static_cast<int32>(Side),
		LimitPriceMinorUnits,
		Quantity);
}

FString FSpaceMMOBackendProtocol::MakeAcceptQuestBody(
	const int32 CharacterId, const FString& QuestKey)
{
	return FString::Printf(
		TEXT("{\"characterId\":%d,\"questKey\":\"%s\"}"), CharacterId, *QuestKey);
}

FString FSpaceMMOBackendProtocol::MakeStartJobBody(
	const int32 CharacterId, const int32 RecipeId, const int32 StationId, const int32 Runs)
{
	return FString::Printf(
		TEXT("{\"characterId\":%d,\"recipeId\":%d,\"stationId\":%d,\"runs\":%d}"),
		CharacterId, RecipeId, StationId, Runs);
}

FString FSpaceMMOBackendProtocol::MakeClaimJobBody(const int32 CharacterId, const int64 JobId)
{
	return FString::Printf(
		TEXT("{\"characterId\":%d,\"jobId\":%lld}"), CharacterId, JobId);
}

bool FSpaceMMOBackendProtocol::ParseFactionSale(
	const FString& Json, int32& OutQuantitySold, int64& OutPaidMinorUnits)
{
	OutQuantitySold = 0;
	OutPaidMinorUnits = 0;

	const TSharedPtr<FJsonObject> Object = ParseObject(Json);

	if (!Object.IsValid())
	{
		return false;
	}

	int64 Sold = 0;

	if (ReadInt64(Object, TEXT("quantitySold"), Sold))
	{
		OutQuantitySold = static_cast<int32>(Sold);
	}

	ReadInt64(Object, TEXT("paidMinorUnits"), OutPaidMinorUnits);

	return true;
}

FString FSpaceMMOBackendProtocol::FormatCredits(const int64 MinorUnits)
{
	// Minor units are hundredths, so the split is exact rather than a division that loses a
	// fraction. Formatting a balance through a double is how a credit goes missing.
	const int64 Whole = MinorUnits / 100;
	const int64 Fraction = FMath::Abs(MinorUnits % 100);

	FString Digits = FString::Printf(TEXT("%lld"), FMath::Abs(Whole));

	FString Grouped;

	for (int32 Index = 0; Index < Digits.Len(); ++Index)
	{
		if (Index > 0 && (Digits.Len() - Index) % 3 == 0)
		{
			Grouped.AppendChar(TEXT(','));
		}

		Grouped.AppendChar(Digits[Index]);
	}

	const FString Sign = MinorUnits < 0 ? TEXT("-") : TEXT("");

	return FString::Printf(TEXT("%s%s.%02lld"), *Sign, *Grouped, Fraction);
}

bool FSpaceMMOBackendProtocol::ParseResolvedCharacter(
	const FString& Json, FBackendResolvedCharacter& OutResolved)
{
	const TSharedPtr<FJsonObject> Object = ParseObject(Json);

	if (!Object.IsValid())
	{
		return false;
	}

	int64 AccountId = 0;
	int64 CharacterId = 0;

	// A zero or missing character id is a failure, not a default. Treating it as "character 0"
	// would hand the caller an identity the backend never granted.
	if (!ReadInt64(Object, TEXT("characterId"), CharacterId) || CharacterId <= 0)
	{
		return false;
	}

	if (ReadInt64(Object, TEXT("accountId"), AccountId))
	{
		OutResolved.AccountId = static_cast<int32>(AccountId);
	}

	OutResolved.CharacterId = static_cast<int32>(CharacterId);

	Object->TryGetStringField(TEXT("characterName"), OutResolved.CharacterName);

	return true;
}

bool FSpaceMMOBackendProtocol::ParseGatherResult(
	const FString& Json, FBackendGatherResult& OutResult)
{
	const TSharedPtr<FJsonObject> Object = ParseObject(Json);

	if (!Object.IsValid())
	{
		return false;
	}

	int64 ItemDefId = 0;
	int64 Quantity = 0;
	int64 XpAwarded = 0;
	int64 NodeRemaining = 0;

	if (ReadInt64(Object, TEXT("itemDefId"), ItemDefId))
	{
		OutResult.ItemDefId = static_cast<int32>(ItemDefId);
	}

	if (ReadInt64(Object, TEXT("quantity"), Quantity))
	{
		OutResult.Quantity = static_cast<int32>(Quantity);
	}

	// Through ReadInt64 rather than a double, like every other quantity that accumulates without
	// bound. XP reaches 13,034,431 at level 99 today, which is comfortably exact — but the guard
	// belongs on the field, not on the value it happens to hold this year.
	if (ReadInt64(Object, TEXT("xpAwarded"), XpAwarded))
	{
		OutResult.XpAwarded = XpAwarded;
	}

	if (ReadInt64(Object, TEXT("nodeRemaining"), NodeRemaining))
	{
		OutResult.NodeRemaining = static_cast<int32>(NodeRemaining);
	}

	Object->TryGetBoolField(TEXT("depleted"), OutResult.bDepleted);

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

bool FSpaceMMOBackendProtocol::ParseStations(
	const FString& Json, TArray<FBackendStation>& OutStations)
{
	TArray<TSharedPtr<FJsonValue>> Values;

	if (!ParseArray(Json, Values))
	{
		return false;
	}

	OutStations.Reset();

	for (const TSharedPtr<FJsonValue>& Value : Values)
	{
		const TSharedPtr<FJsonObject> Object = Value.IsValid() ? Value->AsObject() : nullptr;

		FBackendStation Station;

		if (!Object.IsValid() || !Object->TryGetStringField(TEXT("key"), Station.Key))
		{
			continue;
		}

		Object->TryGetStringField(TEXT("name"), Station.Name);
		Object->TryGetStringField(TEXT("kind"), Station.Kind);

		int64 Scratch = 0;

		if (ReadInt64(Object, TEXT("id"), Scratch))
		{
			Station.Id = static_cast<int32>(Scratch);
		}

		if (ReadInt64(Object, TEXT("bodyId"), Scratch))
		{
			Station.BodyId = static_cast<int32>(Scratch);
		}

		Object->TryGetNumberField(TEXT("dockingRangeKm"), Station.DockingRangeKilometres);

		// Placed one way or the other, never both, and unplaced is allowed. The server refuses to
		// serve both, so reading the direction first and only falling through to a system
		// position means a row that somehow carried both would be drawn on its body rather than
		// in two places at once.
		double X = 0.0;
		double Y = 0.0;
		double Z = 0.0;

		if (Object->TryGetNumberField(TEXT("directionX"), X)
			&& Object->TryGetNumberField(TEXT("directionY"), Y)
			&& Object->TryGetNumberField(TEXT("directionZ"), Z))
		{
			const FVector Direction(X, Y, Z);

			// A zero direction names no point on the sphere and normalising it gives a NaN, which
			// would be a station at no position rather than a visible mistake.
			if (!Direction.IsNearlyZero())
			{
				Station.Direction = Direction.GetSafeNormal();
				Station.bOnBody = true;
				Station.bPlaced = true;
			}
		}
		else if (Object->TryGetNumberField(TEXT("systemX"), X)
			&& Object->TryGetNumberField(TEXT("systemY"), Y)
			&& Object->TryGetNumberField(TEXT("systemZ"), Z))
		{
			Station.Position = FSystemCoordinate(FVector(X, Y, Z));
			Station.bOnBody = false;
			Station.bPlaced = true;
		}

		// A station on a body with no direction, or one in deep space with no coordinate, stays
		// unplaced rather than being dropped. It exists, it is listed, and nothing can dock at it.
		OutStations.Add(Station);
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
