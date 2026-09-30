#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "SpaceMMOPlanetActor.h"
#include "SpaceMMOPlanetPatch.h"
#include "SpaceMMOPlanetTerrain.h"
#include "SpaceMMOWorldSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The ground a player sees is the ground everything stands on.
 *
 * <strong>Task 164.</strong> Everything that stands -- the character, every deposit, every station,
 * a landed ship -- is placed on FPlanetTerrain's height function, and everything a player sees is
 * the patch's triangles. On 30 September Joe's character was drawn a metre deep in Ares, and
 * rebuilding his patch from the log in a test put the drawn ground 1.11 m above his feet at exactly
 * the place the HUD printed. Across the five worlds the gap averaged 0.18 m on the Capital and a
 * metre on Grimhold, up to seven. Nothing measured it, because the one test of mesh against
 * function checked the vertices, and the vertices are on the function by construction.
 *
 * In the backend module rather than beside the patch tests because it reads the authored content,
 * and Json is linked here: a threshold tuned against one planet would silently compress another,
 * which is the lesson BodyPalettesSuitTheirTerrain already carries.
 */

namespace
{
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

	bool ReadDirection(const TSharedPtr<FJsonObject>& Object, FVector& OutDirection)
	{
		const TArray<TSharedPtr<FJsonValue>>* Components = nullptr;

		if (!Object->TryGetArrayField(TEXT("direction"), Components)
			|| Components == nullptr
			|| Components->Num() != 3)
		{
			return false;
		}

		OutDirection = FVector(
			(*Components)[0]->AsNumber(),
			(*Components)[1]->AsNumber(),
			(*Components)[2]->AsNumber()).GetSafeNormal();

		return !OutDirection.IsNearlyZero();
	}

	/** Drawn-minus-true gaps in metres, kept whole so a percentile can be read off them. */
	struct FGaps
	{
		TArray<double> Absolute;

		double Worst = 0.0;

		FVector WorstWhere = FVector::ZeroVector;

		void Add(const double Gap, const FVector& Where)
		{
			Absolute.Add(FMath::Abs(Gap));

			if (FMath::Abs(Gap) > FMath::Abs(Worst))
			{
				Worst = Gap;
				WorstWhere = Where;
			}
		}

		double Percentile95()
		{
			if (Absolute.Num() == 0)
			{
				return 0.0;
			}

			Absolute.Sort();

			return Absolute[(Absolute.Num() * 95) / 100];
		}
	};

	/**
	 * Samples the ground within a radius of a walker's patch centre, out to a distance, through the
	 * patch the planet actor would build for them there.
	 */
	void SampleAround(
		const FPlanetConfig& Planet,
		const FPlanetTerrainConfig& Terrain,
		const FVector& Centre,
		const double FromMetres,
		const double ToMetres,
		FRandomStream& Random,
		FGaps& OutGaps)
	{
		// The altitude a camera behind a walker has. Anything under about ninety metres gets the
		// same patch, so this is not a tuned number, just a low one.
		const FPlanetPatchConfig Patch = ASpaceMMOPlanetActor::PatchFor(Planet, Centre, 0.003);

		FVector Tangent;
		FVector Bitangent;
		FPlanetPatch::BuildTangentFrame(Patch.CentreDirection, Tangent, Bitangent);

		for (int32 Sample = 0; Sample < 1500; ++Sample)
		{
			const double Metres = Random.FRandRange(FromMetres, ToMetres);
			const double Angle = Random.FRandRange(0.0, 2.0 * UE_DOUBLE_PI);

			const FVector Unit = (Patch.CentreDirection * Planet.RadiusKilometres
				+ Tangent * (FMath::Cos(Angle) * Metres / 1000.0)
				+ Bitangent * (FMath::Sin(Angle) * Metres / 1000.0)).GetSafeNormal();

			double Drawn = 0.0;

			if (FPlanetPatch::DrawnRadiusKilometres(Planet, Terrain, Patch, Unit, Drawn))
			{
				OutGaps.Add(
					(Drawn - FPlanetTerrain::SurfaceRadiusKilometres(Planet, Terrain, Unit)) * 1000.0,
					Unit);
			}
		}
	}

	/**
	 * The bar, in metres. A p95 of six centimetres is a boot sole; twenty at worst is half a shin,
	 * somewhere, rarely. Out to 150 m -- where an outpost's rocks are -- the ground is judged from
	 * further off and the bar is looser. Set from measurement: every authored world clears it with
	 * room, and the ground as it was before task 164 misses it by a factor of ten.
	 */
	constexpr double UnderfootP95 = 0.06;
	constexpr double UnderfootWorst = 0.20;
	constexpr double SeenP95 = 0.10;
	constexpr double SeenWorst = 0.30;

	/** How far a walker goes before the patch is rebuilt around them, in metres. */
	double WalkedMetres(const FPlanetConfig& Planet, const FVector& Centre)
	{
		const FPlanetPatchConfig Patch = ASpaceMMOPlanetActor::PatchFor(Planet, Centre, 0.003);

		const double Degrees =
			ASpaceMMOPlanetActor::DriftFractionFor(Patch, true) * Patch.AngularRadiusDegrees;

		return FMath::DegreesToRadians(Degrees) * Planet.RadiusKilometres * 1000.0;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMODrawnGroundMeetsTheFeetTest,
	"SpaceMMO.Terrain.DrawnGroundMeetsTheFeet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMODrawnGroundMeetsTheFeetTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FJsonObject> Root;
	FString Where;

	if (!ReadAuthoredUniverse(Root, Where))
	{
		AddError(FString::Printf(TEXT("Could not read the authored universe from %s"), *Where));

		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Bodies = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Stations = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Deposits = nullptr;

	if (!Root->TryGetArrayField(TEXT("bodies"), Bodies)
		|| !Root->TryGetArrayField(TEXT("stations"), Stations)
		|| !Root->TryGetArrayField(TEXT("resourceNodes"), Deposits))
	{
		AddError(TEXT("The authored universe has no bodies, stations or resource nodes."));

		return false;
	}

	// Every world is drawn at the starting planet's radius (task 157), which is what spacing and
	// feature size are both measured against.
	const FPlanetConfig Planet = USpaceMMOWorldSubsystem::StartingPlanet();

	int32 Checked = 0;

	for (const TSharedPtr<FJsonValue>& Value : *Bodies)
	{
		const TSharedPtr<FJsonObject> Body = Value->AsObject();

		const TSharedPtr<FJsonObject>* TerrainJson = nullptr;

		FString Key;
		Body->TryGetStringField(TEXT("key"), Key);

		// Shaped exactly as USpaceMMOTerrainPaintSubsystem shapes a planet: the starting terrain,
		// with whatever the body authors laid over it.
		FPlanetTerrainConfig Terrain = USpaceMMOWorldSubsystem::StartingPlanetTerrain();

		if (Body->TryGetObjectField(TEXT("terrain"), TerrainJson) && TerrainJson != nullptr)
		{
			double Seed = 0.0;

			if ((*TerrainJson)->TryGetNumberField(TEXT("seed"), Seed))
			{
				Terrain.Seed = static_cast<int64>(Seed);
			}

			(*TerrainJson)->TryGetNumberField(TEXT("maxElevationKm"), Terrain.MaxElevationKilometres);
			(*TerrainJson)->TryGetNumberField(TEXT("baseFrequency"), Terrain.BaseFrequency);
		}

		// Where players actually go on this world -- its station and its rocks -- and a spread of
		// places they might, so the answer is not one lucky hillside.
		TArray<FVector> Centres = {
			FVector(0, 0, 1), FVector(1, 0, 0), FVector(0, 1, 0),
			FVector(-1, 0.3, 0.5).GetSafeNormal(), FVector(0.4, -0.8, 0.2).GetSafeNormal(),
		};

		for (const TArray<TSharedPtr<FJsonValue>>* Placed : {Stations, Deposits})
		{
			for (const TSharedPtr<FJsonValue>& Entry : *Placed)
			{
				const TSharedPtr<FJsonObject> Object = Entry->AsObject();

				FString OnBody;
				FVector Direction;

				if (Object->TryGetStringField(TEXT("body"), OnBody)
					&& OnBody == Key
					&& ReadDirection(Object, Direction))
				{
					Centres.Add(Direction);
				}
			}
		}

		FRandomStream Random(164);

		FGaps Walked;
		FGaps Seen;

		for (const FVector& Centre : Centres)
		{
			const double Walk = WalkedMetres(Planet, Centre);

			// Everywhere a walker can stand before the patch is rebuilt around them.
			SampleAround(Planet, Terrain, Centre, 0.0, Walk, Random, Walked);

			// And the ground they look at from there: an outpost's rocks are 150 m from it.
			SampleAround(Planet, Terrain, Centre, Walk, 150.0, Random, Seen);
		}

		const double WalkedP95 = Walked.Percentile95();
		const double SeenP95Measured = Seen.Percentile95();

		AddInfo(FString::Printf(
			TEXT("%s from %d places: underfoot p95 %.3f m, worst %+.3f m; out to 150 m p95 %.3f m, "
				"worst %+.3f m."),
			*Key, Centres.Num(), WalkedP95, Walked.Worst, SeenP95Measured, Seen.Worst));

		TestTrue(
			FString::Printf(TEXT("%s: drawn ground within %.0f cm of the feet 95%% of the time "
				"(%.3f m)"), *Key, UnderfootP95 * 100.0, WalkedP95),
			WalkedP95 <= UnderfootP95);

		TestTrue(
			FString::Printf(TEXT("%s: drawn ground never more than %.0f cm from the feet (%+.3f m, "
				"at %s)"), *Key, UnderfootWorst * 100.0, Walked.Worst,
				*Walked.WorstWhere.ToCompactString()),
			FMath::Abs(Walked.Worst) <= UnderfootWorst);

		// Out to the rocks. A three-metre deposit thirty centimetres deep is a tenth of it gone.
		TestTrue(
			FString::Printf(TEXT("%s: drawn ground within %.0f cm of the true ground 95%% of the "
				"time out to 150 m (%.3f m)"), *Key, SeenP95 * 100.0, SeenP95Measured),
			SeenP95Measured <= SeenP95);

		TestTrue(
			FString::Printf(TEXT("%s: drawn ground never more than %.0f cm from the true ground out "
				"to 150 m (%+.3f m, at %s)"), *Key, SeenWorst * 100.0, Seen.Worst,
				*Seen.WorstWhere.ToCompactString()),
			FMath::Abs(Seen.Worst) <= SeenWorst);

		++Checked;
	}

	// And that a world was checked at all, since every assertion lives inside the loop.
	TestTrue(TEXT("Some body was measured"), Checked > 0);

	return true;
}

#endif
