#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "SpaceMMOPlanetActor.h"
#include "SpaceMMOPlanetTerrain.h"
#include "SpaceMMOPreviewBody.h"
#include "SpaceMMOWorldDocument.h"
#include "SpaceMMOWorldSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/**
	 * The authored universe, read from the file the seeder reads.
	 *
	 * <strong>The real file, not a fixture.</strong> What this tool has to get right is leaving
	 * 324 lines of somebody's authored content alone while changing six numbers in the middle of
	 * it, and a hand-built two-entry fixture would demonstrate nothing about that. It would also
	 * miss every shape the real file has that a fixture would not think to include: comment keys
	 * between fields, blank lines grouping entries, a station with no body.
	 */
	bool ReadAuthoredUniverse(FString& OutText, FString& OutPath)
	{
		OutPath = FSpaceMMOWorldDocument::DefaultPath();

		return FFileHelper::LoadFileToString(OutText, *OutPath);
	}

	/** Which lines differ between two texts, by index. */
	TArray<int32> DifferingLines(const FString& Before, const FString& After)
	{
		TArray<FString> BeforeLines;
		TArray<FString> AfterLines;

		Before.ParseIntoArrayLines(BeforeLines, false);
		After.ParseIntoArrayLines(AfterLines, false);

		TArray<int32> Differing;

		const int32 Count = FMath::Max(BeforeLines.Num(), AfterLines.Num());

		for (int32 Index = 0; Index < Count; ++Index)
		{
			const FString& Left = BeforeLines.IsValidIndex(Index) ? BeforeLines[Index] : FString();
			const FString& Right = AfterLines.IsValidIndex(Index) ? AfterLines[Index] : FString();

			if (Left != Right)
			{
				Differing.Add(Index);
			}
		}

		return Differing;
	}

	/**
	 * True if the text is JSON the <em>seeder</em> would accept, not merely this engine.
	 *
	 * Unreal's reader tolerates a trailing comma and .NET's does not, so parsing here is not the
	 * question being asked. Removing the last entry of an array was deliberately left writing a
	 * dangling comma to check this, and a version of this helper that only parsed went green.
	 */
	bool ParsesAsJson(const FString& Text)
	{
		FSpaceMMOWorldDocument Document;
		FString Error;

		return Document.LoadFromText(Text, TEXT("(test)"), Error)
			&& !FSpaceMMOWorldDocument::HasDanglingComma(Text);
	}
}

/**
 * The file the seeder reads is the file this reads, and the shapes in it survive the trip.
 *
 * Named keys rather than counts, because counts of shipped content fail whenever anybody authors
 * anything and train the next person to bump the number rather than read why it moved.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOAuthoringReadsTheAuthoredUniverseTest,
	"SpaceMMO.Authoring.ReadsTheAuthoredUniverse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOAuthoringReadsTheAuthoredUniverseTest::RunTest(const FString& Parameters)
{
	FString Text;
	FString Path;

	if (!ReadAuthoredUniverse(Text, Path))
	{
		// Not a silent pass. The content is the point, and a path that stopped resolving would
		// otherwise turn every test in this file green forever.
		AddError(FString::Printf(TEXT("Could not read the authored universe from %s"), *Path));

		return false;
	}

	FSpaceMMOWorldDocument Document;
	FString Error;

	if (!Document.LoadFromText(Text, Path, Error))
	{
		AddError(Error);

		return false;
	}

	const FSpaceMMOAuthoredBody* const Ares = Document.FindBody(TEXT("body_ares"));

	if (Ares == nullptr)
	{
		AddError(TEXT("No body_ares in the authored universe."));

		return false;
	}

	TestTrue(TEXT("Ares is shaped by authored terrain"), Ares->bHasTerrain);
	TestTrue(TEXT("Ares is painted by an authored palette"), Ares->bHasAppearance);
	TestTrue(TEXT("Ares has a radius"), Ares->RadiusKilometres > 0.0);

	// Every body must be drawable: a key and a shape are what the preview needs, and a body that
	// arrived with neither would show as an empty picker entry rather than as an error.
	for (const FSpaceMMOAuthoredBody& Body : Document.GetBodies())
	{
		TestFalse(TEXT("Every body has a key"), Body.Key.IsEmpty());
	}

	const TArray<FSpaceMMOAuthoredPlaceable> OnCapital =
		Document.PlaceablesOn(TEXT("body_capital"));

	const bool bHasDeposit = OnCapital.ContainsByPredicate(
		[](const FSpaceMMOAuthoredPlaceable& Entry)
		{
			return Entry.Kind == ESpaceMMOPlaceableKind::Deposit;
		});

	const bool bHasStation = OnCapital.ContainsByPredicate(
		[](const FSpaceMMOAuthoredPlaceable& Entry)
		{
			return Entry.Kind == ESpaceMMOPlaceableKind::Station;
		});

	TestTrue(TEXT("The capital has deposits to stand markers on"), bHasDeposit);
	TestTrue(TEXT("The capital has a station to stand a marker on"), bHasStation);

	for (const FSpaceMMOAuthoredPlaceable& Entry : Document.GetPlaceables())
	{
		// A zero direction names no point on the sphere, and the validator rejects it. One reaching
		// the tool would put a marker at the body's centre with nothing looking wrong.
		TestFalse(
			FString::Printf(TEXT("'%s' has a direction"), *Entry.Key),
			Entry.Direction.IsNearlyZero());
	}

	// A station that orbits nothing has no body to stand on, and must not be offered as if it did.
	const bool bTookDeepdock = Document.GetPlaceables().ContainsByPredicate(
		[](const FSpaceMMOAuthoredPlaceable& Entry) { return Entry.Key == TEXT("station_deepdock"); });

	TestFalse(TEXT("A station with no body is not placeable on one"), bTookDeepdock);

	return true;
}

/**
 * Moving a deposit rewrites its direction and nothing else at all.
 *
 * <strong>This is the test the whole design exists for.</strong> Nearly half of origin.json is
 * <c>$comment</c> keys carrying the reasoning behind every placement, and a tool that serialised
 * the file back out would restyle all 324 lines while looking like it worked. Asserting that
 * exactly one line differs is the only version of this that would notice.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOAuthoringMovingRewritesOnlyTheDirectionTest,
	"SpaceMMO.Authoring.MovingRewritesOnlyTheDirection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOAuthoringMovingRewritesOnlyTheDirectionTest::RunTest(const FString& Parameters)
{
	FString Text;
	FString Path;

	if (!ReadAuthoredUniverse(Text, Path))
	{
		AddError(FString::Printf(TEXT("Could not read the authored universe from %s"), *Path));

		return false;
	}

	const FVector Moved(-0.5, 0.25, 0.125);

	FString Written;
	FString Error;

	if (!FSpaceMMOWorldDocument::SetDirection(
		Text, TEXT("resourceNodes"), TEXT("node_capital_ferrite_a"), Moved, Written, Error))
	{
		AddError(Error);

		return false;
	}

	const TArray<int32> Differing = DifferingLines(Text, Written);

	TestEqual(TEXT("Exactly one line changed"), Differing.Num(), 1);

	if (Differing.Num() == 1)
	{
		TArray<FString> Lines;

		Written.ParseIntoArrayLines(Lines, false);

		TestTrue(
			TEXT("The line that changed is the direction"),
			Lines.IsValidIndex(Differing[0]) && Lines[Differing[0]].Contains(TEXT("\"direction\"")));
	}

	FSpaceMMOWorldDocument After;

	if (!After.LoadFromText(Written, Path, Error))
	{
		AddError(FString::Printf(TEXT("The written file no longer parses: %s"), *Error));

		return false;
	}

	const FSpaceMMOAuthoredPlaceable* const Entry = After.GetPlaceables().FindByPredicate(
		[](const FSpaceMMOAuthoredPlaceable& Candidate)
		{
			return Candidate.Key == TEXT("node_capital_ferrite_a");
		});

	if (Entry == nullptr)
	{
		AddError(TEXT("The moved deposit is no longer in the file."));

		return false;
	}

	TestTrue(
		TEXT("The direction that comes back is the one that was written"),
		Entry->Direction.Equals(Moved, 1e-6));

	// The comment belonging to that entry sits immediately above the line that changed, which is
	// exactly where a splice that miscounted its span would eat it.
	//
	// Taken out of the file rather than written down here. This asserted a literal sentence from
	// origin.json until 24 August, when the content it quoted was reworded and the test failed for
	// having an opinion about prose. What it is meant to check is that the entry keeps whatever
	// comment it had, which is the same check without the brittleness -- the same reason this file
	// does not assert counts of shipped content either.
	TArray<FString> BeforeLines;

	Text.ParseIntoArrayLines(BeforeLines, false);

	FString OwnComment;

	for (int32 Index = 0; Index < BeforeLines.Num(); ++Index)
	{
		if (BeforeLines[Index].Contains(TEXT("node_capital_ferrite_a")))
		{
			// Scan forward to this entry's own comment, stopping at the end of the entry so a
			// neighbour's comment can never stand in for it.
			for (int32 Scan = Index; Scan < BeforeLines.Num(); ++Scan)
			{
				if (BeforeLines[Scan].Contains(TEXT("\"$comment\"")))
				{
					OwnComment = BeforeLines[Scan].TrimStartAndEnd();

					break;
				}

				if (BeforeLines[Scan].Contains(TEXT("\"direction\"")))
				{
					break;
				}
			}

			break;
		}
	}

	if (OwnComment.IsEmpty())
	{
		AddError(TEXT("The deposit being moved has no comment, so this cannot check one survives."));

		return false;
	}

	TestTrue(
		TEXT("The entry's own comment survives, whatever it says"),
		Written.Contains(OwnComment));

	return true;
}

/**
 * Removing an entry leaves JSON, from the middle of an array and from the end of one.
 *
 * The end is the case worth having a test for: the last entry carries no comma of its own, so
 * cutting only what it spans leaves the entry before it ending in one, and a trailing comma is not
 * JSON. That failure would surface at the next --seed rather than here, with the file already
 * written.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOAuthoringRemovingLeavesValidJsonTest,
	"SpaceMMO.Authoring.RemovingLeavesValidJson",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOAuthoringRemovingLeavesValidJsonTest::RunTest(const FString& Parameters)
{
	FString Text;
	FString Path;

	if (!ReadAuthoredUniverse(Text, Path))
	{
		AddError(FString::Printf(TEXT("Could not read the authored universe from %s"), *Path));

		return false;
	}

	FSpaceMMOWorldDocument Before;
	FString Error;

	if (!Before.LoadFromText(Text, Path, Error))
	{
		AddError(Error);

		return false;
	}

	// Whatever is authored last in each array, found rather than named, so this keeps testing the
	// end case after somebody authors something new at the end.
	FString LastNode;
	FString LastStation;

	for (const FSpaceMMOAuthoredPlaceable& Entry : Before.GetPlaceables())
	{
		if (Entry.Kind == ESpaceMMOPlaceableKind::Deposit)
		{
			LastNode = Entry.Key;
		}
		else
		{
			LastStation = Entry.Key;
		}
	}

	const TArray<TPair<const TCHAR*, FString>> Cases =
	{
		{ TEXT("resourceNodes"), TEXT("node_capital_ferrite_a") },
		{ TEXT("resourceNodes"), LastNode },
		{ TEXT("stations"), LastStation },
	};

	for (const TPair<const TCHAR*, FString>& Case : Cases)
	{
		if (Case.Value.IsEmpty())
		{
			continue;
		}

		FString Written;

		if (!FSpaceMMOWorldDocument::RemoveEntry(Text, Case.Key, Case.Value, Written, Error))
		{
			AddError(Error);

			continue;
		}

		FSpaceMMOWorldDocument After;

		if (!After.LoadFromText(Written, Path, Error))
		{
			AddError(FString::Printf(
				TEXT("Removing '%s' left something that does not parse: %s"), *Case.Value, *Error));

			continue;
		}

		// The load-bearing half. Cutting the last entry of an array leaves the entry before it
		// ending in a comma, and this engine's reader will accept that while the seeder will not.
		TestFalse(
			FString::Printf(
				TEXT("Removing '%s' leaves no dangling comma for the seeder to choke on"),
				*Case.Value),
			FSpaceMMOWorldDocument::HasDanglingComma(Written));

		TestFalse(
			FString::Printf(TEXT("'%s' is gone"), *Case.Value),
			FSpaceMMOWorldDocument::HasEntry(Written, Case.Value));

		TestEqual(
			FString::Printf(TEXT("Removing '%s' removed exactly one entry"), *Case.Value),
			After.GetPlaceables().Num(),
			Before.GetPlaceables().Num() - 1);

		// Every other body's content is untouched: a cut that took its neighbour as well would
		// still parse, and would still pass a test that only looked for the entry it asked for.
		for (const FSpaceMMOAuthoredPlaceable& Entry : Before.GetPlaceables())
		{
			if (Entry.Key == Case.Value)
			{
				continue;
			}

			TestTrue(
				FString::Printf(TEXT("'%s' survives removing '%s'"), *Entry.Key, *Case.Value),
				FSpaceMMOWorldDocument::HasEntry(Written, Entry.Key));
		}
	}

	return true;
}

/**
 * An added deposit comes back the way it went in, and reads like its neighbours.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOAuthoringAppendedEntriesComeBackTest,
	"SpaceMMO.Authoring.AppendedEntriesComeBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOAuthoringAppendedEntriesComeBackTest::RunTest(const FString& Parameters)
{
	FString Text;
	FString Path;

	if (!ReadAuthoredUniverse(Text, Path))
	{
		AddError(FString::Printf(TEXT("Could not read the authored universe from %s"), *Path));

		return false;
	}

	FSpaceMMOAuthoredPlaceable Node;

	Node.Kind = ESpaceMMOPlaceableKind::Deposit;
	Node.Key = TEXT("node_test_appended");
	Node.BodyKey = TEXT("body_ares");
	Node.Item = TEXT("ferrite_ore");
	Node.Skill = TEXT("mining");
	Node.RequiredTool = TEXT("crude_mining_laser");
	Node.RequiredLevel = 3;
	Node.QuantityMax = 42;
	Node.RespawnSeconds = 900;
	Node.Direction = FVector(0.25, -0.5, 0.75);

	FString Written;
	FString Error;

	if (!FSpaceMMOWorldDocument::AppendEntry(Text, Node, Written, Error))
	{
		AddError(Error);

		return false;
	}

	FSpaceMMOWorldDocument After;

	if (!After.LoadFromText(Written, Path, Error))
	{
		AddError(FString::Printf(TEXT("The written file no longer parses: %s"), *Error));

		return false;
	}

	const FSpaceMMOAuthoredPlaceable* const Read = After.GetPlaceables().FindByPredicate(
		[](const FSpaceMMOAuthoredPlaceable& Candidate)
		{
			return Candidate.Key == TEXT("node_test_appended");
		});

	if (Read == nullptr)
	{
		AddError(TEXT("The appended deposit is not in the written file."));

		return false;
	}

	// Every field, not just the direction. A writer that dropped the tool would author a deposit
	// that is minable bare-handed, and nothing about the file would look wrong.
	TestEqual(TEXT("Body"), Read->BodyKey, Node.BodyKey);
	TestEqual(TEXT("Item"), Read->Item, Node.Item);
	TestEqual(TEXT("Skill"), Read->Skill, Node.Skill);
	TestEqual(TEXT("Required tool"), Read->RequiredTool, Node.RequiredTool);
	TestEqual(TEXT("Required level"), Read->RequiredLevel, Node.RequiredLevel);
	TestEqual(TEXT("Quantity"), Read->QuantityMax, Node.QuantityMax);
	TestEqual(TEXT("Respawn"), Read->RespawnSeconds, Node.RespawnSeconds);
	TestTrue(TEXT("Direction"), Read->Direction.Equals(Node.Direction, 1e-6));

	// Nothing that was there before may have moved out of the way to make room.
	FSpaceMMOWorldDocument Before;

	if (Before.LoadFromText(Text, Path, Error))
	{
		TestEqual(
			TEXT("Appending added exactly one entry"),
			After.GetPlaceables().Num(),
			Before.GetPlaceables().Num() + 1);
	}

	// Indented like the entry above it, or the file grows a second style every time somebody
	// places a deposit graphically.
	TArray<FString> Lines;

	Written.ParseIntoArrayLines(Lines, false);

	int32 AppendedLine = INDEX_NONE;

	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		if (Lines[Index].Contains(TEXT("node_test_appended")))
		{
			AppendedLine = Index;

			break;
		}
	}

	if (AppendedLine != INDEX_NONE && AppendedLine >= 2)
	{
		// The key sits one line inside the brace that opened the entry.
		const FString BraceLine = Lines[AppendedLine - 1];

		TestEqual(TEXT("A new entry opens with a brace on its own line"), BraceLine.TrimEnd(),
			FString(TEXT("    {")));

		TestTrue(
			TEXT("A new entry's fields are indented like the ones already in the file"),
			Lines[AppendedLine].StartsWith(TEXT("      \"key\"")));
	}
	else
	{
		AddError(TEXT("Could not find the appended entry in the written text."));
	}

	return true;
}

/**
 * A batch of edits leaves every authored comment in the file.
 *
 * The comments are the reasoning behind the content, and they are the thing a naive writer
 * destroys. Counting them before and after — allowing only for the ones inside an entry that was
 * deliberately cut — is what would notice.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOAuthoringCommentsSurviveEveryEditTest,
	"SpaceMMO.Authoring.CommentsSurviveEveryEdit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOAuthoringCommentsSurviveEveryEditTest::RunTest(const FString& Parameters)
{
	FString Text;
	FString Path;

	if (!ReadAuthoredUniverse(Text, Path))
	{
		AddError(FString::Printf(TEXT("Could not read the authored universe from %s"), *Path));

		return false;
	}

	const auto CountComments = [](const FString& In)
	{
		int32 Count = 0;
		int32 From = 0;

		for (;;)
		{
			const int32 At = In.Find(TEXT("Comment\""), ESearchCase::CaseSensitive,
				ESearchDir::FromStart, From);

			if (At == INDEX_NONE)
			{
				break;
			}

			++Count;
			From = At + 1;
		}

		return Count;
	};

	const int32 CommentsBefore = CountComments(Text);

	TestTrue(TEXT("The authored file has comments to lose"), CommentsBefore > 10);

	FSpaceMMOAuthoredPlaceable Added;

	Added.Kind = ESpaceMMOPlaceableKind::Station;
	Added.Key = TEXT("station_test_appended");
	Added.Name = TEXT("Test Outpost");
	Added.SystemKey = TEXT("system_origin");
	Added.BodyKey = TEXT("body_ares");
	Added.StationKind = TEXT("TradingHub");
	Added.DockingRangeKilometres = 5.0;
	Added.Direction = FVector(0.0, 1.0, 0.0);

	FSpaceMMOAuthoredPlaceable Moved;

	Moved.Kind = ESpaceMMOPlaceableKind::Deposit;
	Moved.Key = TEXT("node_ares_regolith");
	Moved.Direction = FVector(-0.9, 0.1, 0.4);

	TArray<FSpaceMMOWorldEdit> Edits;

	Edits.Add({ ESpaceMMOWorldEditKind::SetDirection, TEXT("node_ares_regolith"), Moved });
	Edits.Add({ ESpaceMMOWorldEditKind::Append, FString(), Added });

	FString Written;
	FString Error;

	if (!FSpaceMMOWorldDocument::ApplyEdits(Text, Edits, Written, Error))
	{
		AddError(Error);

		return false;
	}

	TestEqual(
		TEXT("Moving and adding lose no comments"), CountComments(Written), CommentsBefore);

	TestTrue(TEXT("The result still parses"), ParsesAsJson(Written));

	// The long explanations are what a reformatting writer would mangle rather than delete, so one
	// is checked verbatim rather than by counting.
	TestTrue(
		TEXT("A long authored comment survives verbatim"),
		Written.Contains(TEXT("Radii are already at the 1:10 universe scale")));

	return true;
}

/**
 * One failed edit writes nothing at all.
 *
 * A batch that applied the moves it understood and gave up on the rest would leave the file
 * agreeing with neither the editor nor the author's memory of what they did, with nothing to say
 * which half landed.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOAuthoringAFailedBatchChangesNothingTest,
	"SpaceMMO.Authoring.AFailedBatchChangesNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOAuthoringAFailedBatchChangesNothingTest::RunTest(const FString& Parameters)
{
	FString Text;
	FString Path;

	if (!ReadAuthoredUniverse(Text, Path))
	{
		AddError(FString::Printf(TEXT("Could not read the authored universe from %s"), *Path));

		return false;
	}

	FSpaceMMOAuthoredPlaceable Good;

	Good.Kind = ESpaceMMOPlaceableKind::Deposit;
	Good.Key = TEXT("node_ares_regolith");
	Good.Direction = FVector(-0.9, 0.1, 0.4);

	FSpaceMMOAuthoredPlaceable Missing;

	Missing.Kind = ESpaceMMOPlaceableKind::Deposit;
	Missing.Key = TEXT("node_that_does_not_exist");
	Missing.Direction = FVector(1.0, 0.0, 0.0);

	TArray<FSpaceMMOWorldEdit> Edits;

	Edits.Add({ ESpaceMMOWorldEditKind::SetDirection, TEXT("node_ares_regolith"), Good });
	Edits.Add({ ESpaceMMOWorldEditKind::SetDirection, TEXT("node_that_does_not_exist"), Missing });

	FString Written;
	FString Error;

	const bool bApplied = FSpaceMMOWorldDocument::ApplyEdits(Text, Edits, Written, Error);

	TestFalse(TEXT("A batch naming an entry that is not there fails"), bApplied);
	TestFalse(TEXT("And says why"), Error.IsEmpty());
	TestEqual(TEXT("And leaves the text exactly as it was"), Written, Text);

	return true;
}


/**
 * The table-top preview is the planet the game draws, to scale.
 *
 * <strong>Measured off the surface, not read off the settings.</strong> The preview shrinks the
 * body so it fits in a viewport, and the only thing that makes that honest is relief shrinking by
 * the same factor — scaled against the radius the planet is <em>drawn</em> at rather than the one
 * it is authored at. Ares is 339 km authored and 20 km drawn, so getting that wrong makes the
 * preview seventeen times too smooth: a plausible-looking ball that is not the planet, and nothing
 * in the configuration would look wrong.
 *
 * So this samples the ground itself, in both models, and compares the shape rather than the inputs.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOAuthoringPreviewIsTheDrawnPlanetToScaleTest,
	"SpaceMMO.Authoring.PreviewIsTheDrawnPlanetToScale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOAuthoringPreviewIsTheDrawnPlanetToScaleTest::RunTest(const FString& Parameters)
{
	FString Text;
	FString Path;

	if (!ReadAuthoredUniverse(Text, Path))
	{
		AddError(FString::Printf(TEXT("Could not read the authored universe from %s"), *Path));

		return false;
	}

	FSpaceMMOWorldDocument Document;
	FString Error;

	if (!Document.LoadFromText(Text, Path, Error))
	{
		AddError(Error);

		return false;
	}

	const double DrawnRadius = FSpaceMMOPreviewScale::DrawnRadiusKilometres();

	TestTrue(TEXT("The game draws a planet of some size"), DrawnRadius > 0.0);

	const double PreviewRadiusCentimetres =
		FSpaceMMOPreviewScale::DefaultPreviewRadiusCentimetres;

	int32 Checked = 0;

	for (const FSpaceMMOAuthoredBody& Body : Document.GetBodies())
	{
		if (!Body.bHasTerrain)
		{
			continue;
		}

		// What the game builds: the authored shape on the radius it actually draws.
		FPlanetConfig Drawn;

		Drawn.RadiusKilometres = DrawnRadius;

		FPlanetTerrainConfig DrawnTerrain;

		DrawnTerrain.Seed = Body.TerrainSeed;
		DrawnTerrain.MaxElevationKilometres = Body.MaxElevationKilometres;
		DrawnTerrain.BaseFrequency = Body.BaseFrequency;

		const FPlanetConfig Preview = FSpaceMMOPreviewScale::PlanetFor(PreviewRadiusCentimetres);

		const FPlanetTerrainConfig PreviewTerrain =
			FSpaceMMOPreviewScale::TerrainFor(Body, DrawnRadius, PreviewRadiusCentimetres);

		double MostRelief = 0.0;
		double WorstDifference = 0.0;

		// A spiral over the whole sphere rather than a few axes: terrain is a function of
		// direction, and three directions could agree by accident.
		for (int32 Sample = 0; Sample < 400; ++Sample)
		{
			const double Fraction = (Sample + 0.5) / 400.0;
			const double Z = 1.0 - (2.0 * Fraction);
			const double Ring = FMath::Sqrt(FMath::Max(0.0, 1.0 - (Z * Z)));
			const double Angle = Sample * 2.39996;

			const FVector Direction(Ring * FMath::Cos(Angle), Ring * FMath::Sin(Angle), Z);

			const double DrawnFraction =
				FPlanetTerrain::SurfaceRadiusKilometres(Drawn, DrawnTerrain, Direction)
					/ Drawn.RadiusKilometres;

			const double PreviewFraction =
				FPlanetTerrain::SurfaceRadiusKilometres(Preview, PreviewTerrain, Direction)
					/ Preview.RadiusKilometres;

			WorstDifference =
				FMath::Max(WorstDifference, FMath::Abs(DrawnFraction - PreviewFraction));

			MostRelief = FMath::Max(MostRelief, DrawnFraction - 1.0);
		}

		TestTrue(
			FString::Printf(
				TEXT("%s previews at the same shape it is drawn at (worst %.9f)"),
				*Body.Key, WorstDifference),
			WorstDifference < 1e-9);

		// And it is a shape at all. A comparison of two spheres would pass the check above while
		// showing nothing to place anything against.
		TestTrue(
			FString::Printf(TEXT("%s has relief to see (%.4f of its radius)"), *Body.Key, MostRelief),
			MostRelief > 0.001);

		++Checked;
	}

	TestTrue(TEXT("At least one authored body was measured"), Checked > 0);

	return true;
}


/**
 * The body the client is configured to draw has content worth landing on.
 *
 * <strong>This is a config-against-content check, and it exists because the two disagreed for
 * months.</strong> Which world the scene is used to be answered by two separate keys — the planet
 * actor drew <c>body_ares</c> from DefaultGame.ini while the deposit subsystem placed
 * <c>body_capital</c>'s content from a hard-coded default — so the playtest world had Ares' terrain
 * with the Capital's ore standing on it, and neither setting looked wrong from where it was
 * written. There is one key now, which means flipping it silently decides what a new player can do.
 *
 * So: whatever it names must have something a brand-new character can gather bare-handed, and
 * somewhere to dock. Ares fails both — one level-15 ore behind a mining laser — which is exactly
 * the mistake this would catch.
 *
 * It lives with the authoring tests because this is where the authored universe is already read,
 * and the tool's whole purpose is deciding what goes on that body.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSpaceMMOAuthoringConfiguredBodyIsPlayableTest,
	"SpaceMMO.Authoring.ConfiguredBodyIsPlayable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceMMOAuthoringConfiguredBodyIsPlayableTest::RunTest(const FString& Parameters)
{
	FString Text;
	FString Path;

	if (!ReadAuthoredUniverse(Text, Path))
	{
		AddError(FString::Printf(TEXT("Could not read the authored universe from %s"), *Path));

		return false;
	}

	FSpaceMMOWorldDocument Document;
	FString Error;

	if (!Document.LoadFromText(Text, Path, Error))
	{
		AddError(Error);

		return false;
	}

	// The one key, read the same way the game reads it.
	const FString Configured = GetDefault<ASpaceMMOPlanetActor>()->BodyKey;

	if (Document.FindBody(Configured) == nullptr)
	{
		AddError(FString::Printf(
			TEXT("DefaultGame.ini draws '%s', which is not an authored body."), *Configured));

		return false;
	}

	const TArray<FSpaceMMOAuthoredPlaceable> OnBody = Document.PlaceablesOn(Configured);

	const bool bCanStart = OnBody.ContainsByPredicate(
		[](const FSpaceMMOAuthoredPlaceable& Entry)
		{
			// Bare hands, first level: the onboarding questline's first step gathers scrap, and a
			// world whose only ore needs a tool the player has not crafted yet is unstartable.
			return Entry.Kind == ESpaceMMOPlaceableKind::Deposit
				&& Entry.RequiredLevel <= 1
				&& Entry.RequiredTool.IsEmpty();
		});

	const bool bSomewhereToDock = OnBody.ContainsByPredicate(
		[](const FSpaceMMOAuthoredPlaceable& Entry)
		{
			return Entry.Kind == ESpaceMMOPlaceableKind::Station;
		});

	TestTrue(
		FString::Printf(
			TEXT("'%s' has something a new character can gather with no tool"), *Configured),
		bCanStart);

	TestTrue(
		FString::Printf(TEXT("'%s' has a station to dock at"), *Configured),
		bSomewhereToDock);

	return true;
}

#endif
