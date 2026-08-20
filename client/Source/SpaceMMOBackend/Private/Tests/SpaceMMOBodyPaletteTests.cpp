#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "SpaceMMOPlanetPatch.h"
#include "SpaceMMOPlanetTerrain.h"
#include "SpaceMMOWorldSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** The authored universe, read from the same file the seeder reads. */
	bool ReadAuthoredBodies(TArray<TSharedPtr<FJsonValue>>& OutBodies, FString& OutWhere)
	{
		OutWhere = FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("data"), TEXT("universe"),
				TEXT("origin.json")));

		FString Text;

		if (!FFileHelper::LoadFileToString(Text, *OutWhere))
		{
			return false;
		}

		TSharedPtr<FJsonObject> Root;

		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);

		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* Bodies = nullptr;

		if (!Root->TryGetArrayField(TEXT("bodies"), Bodies) || Bodies == nullptr)
		{
			return false;
		}

		OutBodies = *Bodies;

		return true;
	}

	/** What height and steepness actually span on a body, sampled around it rather than at a point. */
	void MeasureBody(
		const FPlanetTerrainConfig& Terrain,
		double& OutLowHeight,
		double& OutHighHeight,
		double& OutSteepest)
	{
		OutLowHeight = 1.0;
		OutHighHeight = 0.0;
		OutSteepest = 0.0;

		const FPlanetConfig Planet = USpaceMMOWorldSubsystem::StartingPlanet();

		// Five places, because thresholds have to suit everywhere somebody might stand and one
		// patch describes one spot. The same five the authored values were measured across.
		const FVector Wheres[] = {
			FVector(0, 0, 1), FVector(1, 0, 0), FVector(0, 1, 0),
			FVector(-1, 0.3, 0.5), FVector(0.4, -0.8, 0.2),
		};

		for (const FVector& Where : Wheres)
		{
			FPlanetPatchConfig Patch;
			Patch.CentreDirection = Where.GetSafeNormal();
			Patch.AngularRadiusDegrees = 4.0;
			Patch.Resolution = 129;

			const FPlanetPatchMesh Mesh = FPlanetPatch::Build(Planet, Terrain, Patch);

			for (const FVector2D& Kind : Mesh.GroundKinds)
			{
				OutLowHeight = FMath::Min(OutLowHeight, Kind.X);
				OutHighHeight = FMath::Max(OutHighHeight, Kind.X);
				OutSteepest = FMath::Max(OutSteepest, Kind.Y);
			}
		}
	}
}

/**
 * A body's palette thresholds have to suit the terrain it was authored with.
 *
 * <strong>In the backend module rather than beside the other terrain tests.</strong> It reads the
 * authored JSON, and Json is linked here and not in SpaceMMOCore -- adding a module dependency to
 * Core so that a test could live next to its neighbours would be paying in the shipping build for
 * something only a test needs.
 *
 * The two sit a few lines apart in one file and are silently coupled: height and steepness land in a
 * different part of 0..1 for every combination of relief and frequency, so thresholds tuned against
 * one world compress another's ground into a corner. That reads as a badly chosen palette rather
 * than a stale number, which is why it needs catching here rather than by looking.
 *
 * It cost a round already -- one shared slope range gave the Capital almost no rock at 12 degrees
 * and Grimhold nothing but at 46.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOBodyPalettesSuitTheirTerrainTest,
	"SpaceMMO.Terrain.BodyPalettesSuitTheirTerrain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOBodyPalettesSuitTheirTerrainTest::RunTest(const FString& Parameters)
{
	TArray<TSharedPtr<FJsonValue>> Bodies;
	FString Where;

	if (!ReadAuthoredBodies(Bodies, Where))
	{
		// Not a silent pass. The content is the point of this test, and a path that stopped
		// resolving would otherwise turn it green forever.
		AddError(FString::Printf(TEXT("Could not read authored bodies from %s"), *Where));

		return false;
	}

	int32 Checked = 0;

	for (const TSharedPtr<FJsonValue>& Value : Bodies)
	{
		const TSharedPtr<FJsonObject> Body = Value.IsValid() ? Value->AsObject() : nullptr;

		if (!Body.IsValid())
		{
			continue;
		}

		const TSharedPtr<FJsonObject>* Appearance = nullptr;
		const TSharedPtr<FJsonObject>* TerrainJson = nullptr;

		// A body with only one of the two is a working state: painted before it was shaped, or
		// shaped before anybody chose its colours.
		if (!Body->TryGetObjectField(TEXT("appearance"), Appearance)
			|| !Body->TryGetObjectField(TEXT("terrain"), TerrainJson))
		{
			continue;
		}

		FString Key;
		Body->TryGetStringField(TEXT("key"), Key);

		FPlanetTerrainConfig Terrain = USpaceMMOWorldSubsystem::StartingPlanetTerrain();

		double Seed = 0.0;

		if ((*TerrainJson)->TryGetNumberField(TEXT("seed"), Seed))
		{
			Terrain.Seed = static_cast<int64>(Seed);
		}

		(*TerrainJson)->TryGetNumberField(TEXT("maxElevationKm"), Terrain.MaxElevationKilometres);
		(*TerrainJson)->TryGetNumberField(TEXT("baseFrequency"), Terrain.BaseFrequency);

		double LowHeight = 0.0;
		double HighHeight = 0.0;
		double Steepest = 0.0;

		MeasureBody(Terrain, LowHeight, HighHeight, Steepest);

		double HeightFrom = 0.0;
		double HeightTo = 0.0;
		double SlopeFrom = 0.0;
		double SlopeTo = 0.0;

		(*Appearance)->TryGetNumberField(TEXT("heightFrom"), HeightFrom);
		(*Appearance)->TryGetNumberField(TEXT("heightTo"), HeightTo);
		(*Appearance)->TryGetNumberField(TEXT("slopeFrom"), SlopeFrom);
		(*Appearance)->TryGetNumberField(TEXT("slopeTo"), SlopeTo);

		AddInfo(FString::Printf(
			TEXT("%s: ground height %.3f..%.3f steepest %.3f; thresholds height %.2f..%.2f slope %.2f..%.2f"),
			*Key, LowHeight, HighHeight, Steepest, HeightFrom, HeightTo, SlopeFrom, SlopeTo));

		// Both height thresholds inside the ground that exists, or the blend never traverses it: a
		// heightFrom above everything paints the world its low colour throughout, and a heightTo
		// below everything paints it the high one. Either looks like one flat choice.
		TestTrue(
			FString::Printf(TEXT("%s heightFrom %.2f is inside its ground %.3f..%.3f"),
				*Key, HeightFrom, LowHeight, HighHeight),
			HeightFrom > LowHeight && HeightFrom < HighHeight);

		TestTrue(
			FString::Printf(TEXT("%s heightTo %.2f is inside its ground %.3f..%.3f"),
				*Key, HeightTo, LowHeight, HighHeight),
			HeightTo > LowHeight && HeightTo <= HighHeight);

		TestTrue(
			FString::Printf(TEXT("%s height thresholds are the right way round"), *Key),
			HeightFrom < HeightTo);

		// Rock has to appear somewhere. A slopeFrom above the body's steepest ground means a rock
		// colour that is authored and never drawn -- invisible until somebody wonders why one world
		// looks flat.
		TestTrue(
			FString::Printf(TEXT("%s slopeFrom %.2f is below its steepest %.3f"),
				*Key, SlopeFrom, Steepest),
			SlopeFrom < Steepest);

		// And it has to read as rock on the steepest ground rather than as a faint wash. Two thirds
		// of the way from start to full cover is the line.
		const double Reached =
			(Steepest - SlopeFrom) / FMath::Max(SlopeTo - SlopeFrom, UE_DOUBLE_SMALL_NUMBER);

		TestTrue(
			FString::Printf(TEXT("%s rock reaches %.0f%% cover on its steepest ground"),
				*Key, 100.0 * FMath::Clamp(Reached, 0.0, 1.0)),
			Reached > 0.66);

		++Checked;
	}

	// And that any body was checked at all, since every assertion above lives inside the loop.
	TestTrue(TEXT("Some body is both shaped and painted"), Checked > 0);

	return true;
}

#endif
