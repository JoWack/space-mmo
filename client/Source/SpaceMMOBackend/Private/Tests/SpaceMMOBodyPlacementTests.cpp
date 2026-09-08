#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "SpaceMMOBackendProtocol.h"
#include "SpaceMMOBackendTypes.h"
#include "SpaceMMOPlanetActor.h"
#include "SpaceMMOWorldSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Where the bodies are, and that the client can be told.
 *
 * <strong>Task 157.</strong> A body carried no position at all until 8 September, so the client
 * drew one compiled-in planet and skipped every station belonging to a body it had never heard of:
 * Terra, Ares, Verdance and Grimhold were seeded, served, and invisible. These are the checks that
 * would notice it happening again — a new world authored without a position, or a station put on
 * one.
 *
 * <strong>In the backend module rather than beside the other planet tests</strong>, for the reason
 * the palette tests give: they read the authored JSON, and Json is linked here and not in
 * SpaceMMOCore.
 */

namespace
{
	/** The authored universe, read from the same file the seeder reads. */
	bool ReadAuthoredUniverse(TSharedPtr<FJsonObject>& OutRoot, FString& OutWhere)
	{
		OutWhere = FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("data"), TEXT("universe"),
				TEXT("origin.json")));

		FString Text;

		if (!FFileHelper::LoadFileToString(Text, *OutWhere))
		{
			return false;
		}

		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);

		return FJsonSerializer::Deserialize(Reader, OutRoot) && OutRoot.IsValid();
	}

	/** A body's authored position, or false when it has none. */
	bool AuthoredPosition(const TSharedPtr<FJsonObject>& Body, FVector& OutPosition)
	{
		const TArray<TSharedPtr<FJsonValue>>* Components = nullptr;

		if (!Body->TryGetArrayField(TEXT("systemPosition"), Components)
			|| Components == nullptr
			|| Components->Num() != 3)
		{
			return false;
		}

		OutPosition = FVector(
			(*Components)[0]->AsNumber(),
			(*Components)[1]->AsNumber(),
			(*Components)[2]->AsNumber());

		return true;
	}

	/**
	 * The tallest relief any body authors, in kilometres.
	 *
	 * Terrain rises above the nominal radius, so two spheres that merely fail to intersect can
	 * still have mountains inside one another. Taken from content rather than assumed, because the
	 * bodies do not agree: the Capital authors 0.35 km and Grimhold 0.7.
	 */
	double MaxAuthoredRelief(const TArray<TSharedPtr<FJsonValue>>& Bodies)
	{
		double Tallest = 0.0;

		for (const TSharedPtr<FJsonValue>& Value : Bodies)
		{
			const TSharedPtr<FJsonObject> Body = Value->AsObject();

			const TSharedPtr<FJsonObject>* Terrain = nullptr;

			double Relief = 0.0;

			if (Body->TryGetObjectField(TEXT("terrain"), Terrain)
				&& Terrain != nullptr
				&& (*Terrain)->TryGetNumberField(TEXT("maxElevationKm"), Relief))
			{
				Tallest = FMath::Max(Tallest, Relief);
			}
		}

		return Tallest;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOAuthoredBodiesArePlacedTest,
	"SpaceMMO.Bodies.AuthoredBodiesAreAllPlaced",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOAuthoredBodiesArePlacedTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FJsonObject> Root;
	FString Where;

	if (!TestTrue(TEXT("Read the authored universe"), ReadAuthoredUniverse(Root, Where)))
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Bodies = nullptr;

	if (!TestTrue(TEXT("It has bodies"), Root->TryGetArrayField(TEXT("bodies"), Bodies))
		|| Bodies == nullptr)
	{
		return false;
	}

	// Not a literal count. Content gets authored, and a number bumped whenever it is catches
	// nothing -- the claim worth making is that every body has one, whatever the total.
	TestTrue(TEXT("There are bodies to check"), Bodies->Num() > 0);

	TMap<FString, FVector> Placed;

	for (const TSharedPtr<FJsonValue>& Value : *Bodies)
	{
		const TSharedPtr<FJsonObject> Body = Value->AsObject();

		FString Key;
		Body->TryGetStringField(TEXT("key"), Key);

		FVector Position = FVector::ZeroVector;

		// A body without one is drawn by nobody, and a station standing on it cannot be placed.
		if (TestTrue(
			FString::Printf(TEXT("Body '%s' has a system position"), *Key),
			AuthoredPosition(Body, Position)))
		{
			Placed.Add(Key, Position);
		}
	}

	// Far enough apart that they do not intersect at the radius they are actually DRAWN at.
	//
	// This is the check the server's content validator deliberately cannot make: it knows only the
	// authored radiusKm, which is the 1:10 figure and thirty-odd times the drawn size, so an
	// overlap rule there would reject every position in the shipped pack. The client is where the
	// drawn radius lives, so this is where the claim belongs.
	const FPlanetConfig Drawn = USpaceMMOWorldSubsystem::StartingPlanet();

	const double MinimumSeparation = 2.0 * (Drawn.RadiusKilometres + MaxAuthoredRelief(*Bodies));

	TArray<FString> Keys;
	Placed.GetKeys(Keys);

	for (int32 I = 0; I < Keys.Num(); ++I)
	{
		for (int32 J = I + 1; J < Keys.Num(); ++J)
		{
			const double Apart = (Placed[Keys[I]] - Placed[Keys[J]]).Size();

			TestTrue(
				FString::Printf(
					TEXT("%s and %s are %.1f km apart, which clears %.1f km of drawn surface"),
					*Keys[I], *Keys[J], Apart, MinimumSeparation),
				Apart > MinimumSeparation);
		}
	}

	// The one agreement that cannot be checked at runtime without dropping somebody through the
	// world.
	//
	// A character is given a pawn before any of this arrives over HTTP, positioned against the
	// compiled-in starting planet -- so if content moves that body, the client cannot follow
	// without moving ground somebody is already standing on. USpaceMMOWorldSubsystem::EnsurePlanet
	// warns and leaves the planet where it is; this is what stops the disagreement being authored
	// in the first place.
	const FString StartingBody = GetDefault<ASpaceMMOPlanetActor>()->BodyKey;

	if (const FVector* const Authored = Placed.Find(StartingBody))
	{
		const FVector Compiled = Drawn.Centre.Kilometres;

		TestTrue(
			FString::Printf(
				TEXT("The starting body '%s' is authored at %s, where StartingPlanet() puts it "
					"(%s)"),
				*StartingBody, *Authored->ToString(), *Compiled.ToString()),
			(*Authored - Compiled).Size() < 0.001);
	}
	else
	{
		AddError(FString::Printf(
			TEXT("DefaultGame.ini starts on body '%s', which the authored universe does not "
				"place."),
			*StartingBody));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOAuthoredStationsStandOnPlacedBodiesTest,
	"SpaceMMO.Bodies.StationsStandOnPlacedBodies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOAuthoredStationsStandOnPlacedBodiesTest::RunTest(const FString& Parameters)
{
	// The direct guard against task 157 returning. A station on an unplaced body is served, is
	// real in the database, and is drawn by nothing -- which is exactly what four of the six were
	// doing, and the only thing that said so was one count in a log line.
	TSharedPtr<FJsonObject> Root;
	FString Where;

	if (!TestTrue(TEXT("Read the authored universe"), ReadAuthoredUniverse(Root, Where)))
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Bodies = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Stations = nullptr;

	if (!Root->TryGetArrayField(TEXT("bodies"), Bodies)
		|| !Root->TryGetArrayField(TEXT("stations"), Stations)
		|| Bodies == nullptr
		|| Stations == nullptr)
	{
		AddError(TEXT("The authored universe has no bodies or no stations."));

		return false;
	}

	TSet<FString> PlacedBodies;

	for (const TSharedPtr<FJsonValue>& Value : *Bodies)
	{
		const TSharedPtr<FJsonObject> Body = Value->AsObject();

		FVector Position = FVector::ZeroVector;

		FString Key;

		if (Body->TryGetStringField(TEXT("key"), Key) && AuthoredPosition(Body, Position))
		{
			PlacedBodies.Add(Key);
		}
	}

	int32 OnBodies = 0;

	for (const TSharedPtr<FJsonValue>& Value : *Stations)
	{
		const TSharedPtr<FJsonObject> Station = Value->AsObject();

		FString Body;

		// A deep-space station carries its own position and needs no body. Those are the ones a
		// missing planet was never fatal for.
		if (!Station->TryGetStringField(TEXT("body"), Body) || Body.IsEmpty())
		{
			continue;
		}

		++OnBodies;

		FString Key;
		Station->TryGetStringField(TEXT("key"), Key);

		TestTrue(
			FString::Printf(
				TEXT("Station '%s' stands on body '%s', which content places"), *Key, *Body),
			PlacedBodies.Contains(Body));
	}

	// And that any station is on a body at all, since the loop above passes against none.
	TestTrue(TEXT("Some stations stand on bodies"), OnBodies > 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOBodyPositionFromTheWireTest,
	"SpaceMMO.Bodies.PositionArrivesFromTheWire",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOBodyPositionFromTheWireTest::RunTest(const FString& Parameters)
{
	// Captured from a running server, not written by hand.
	//
	// <strong>This is the assertion that crosses the wire.</strong> A parse tested against
	// hand-built JSON proves the parser reads the field the test author typed, which is the bug --
	// a market panel filtered inventory for kind 1 and five green tests missed it because every one
	// of them built its inputs by hand. The body below is verbatim from GET /world/bodies on
	// 8 September, camelCase and integral numbers included: the server sends "systemY":60, not
	// 60.0, and a parser reading it as anything but a number would fail here rather than in a
	// playtest.
	const FString Payload = TEXT(R"([
		{"id":1,"key":"body_terra","name":"Terra","starSystemId":1,"radiusKm":637.1,
		 "systemX":-120,"systemY":60,"systemZ":0,
		 "lowColour":"0.10,0.14,0.09","highColour":"0.55,0.55,0.48","rockColour":"0.30,0.30,0.31",
		 "heightFrom":0.25,"heightTo":0.72,"slopeFrom":0.18,"slopeTo":0.38,
		 "terrainSeed":20260801,"maxElevationKm":0.5,"baseFrequency":9},
		{"id":9,"key":"body_nowhere","name":"Nowhere","starSystemId":1,"radiusKm":100.0,
		 "systemX":null,"systemY":null,"systemZ":null,
		 "lowColour":null,"highColour":null,"rockColour":null,
		 "heightFrom":null,"heightTo":null,"slopeFrom":null,"slopeTo":null,
		 "terrainSeed":null,"maxElevationKm":null,"baseFrequency":null}
	])");

	TArray<FBackendBody> Bodies;

	TestTrue(TEXT("Parsed"), FSpaceMMOBackendProtocol::ParseBodies(Payload, Bodies));

	if (!TestEqual(TEXT("Two bodies"), Bodies.Num(), 2))
	{
		return false;
	}

	TestTrue(TEXT("Terra is placed"), Bodies[0].bHasSystemPosition);

	// Each component checked separately and against different values, so a parser reading systemX
	// three times passes nothing.
	TestEqual(TEXT("X"), Bodies[0].SystemPositionKilometres.X, -120.0);
	TestEqual(TEXT("Y"), Bodies[0].SystemPositionKilometres.Y, 60.0);
	TestEqual(TEXT("Z"), Bodies[0].SystemPositionKilometres.Z, 0.0);

    // Null is not the origin.
    //
    // The trap FBackendShip's position carries the same comment about: zero is a real place --
    // the centre of the star system -- so an unplaced body read into a zero vector would be drawn
    // inside the star and look exactly like an answer.
	TestFalse(TEXT("An unplaced body is not placed"), Bodies[1].bHasSystemPosition);
	TestTrue(
		TEXT("And carries no position"),
		Bodies[1].SystemPositionKilometres.IsNearlyZero());

	return true;
}

#endif
