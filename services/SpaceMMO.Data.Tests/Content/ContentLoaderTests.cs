using Microsoft.EntityFrameworkCore;
using SpaceMMO.Data.Content;
using SpaceMMO.Data.Entities;
using SpaceMMO.Domain.Content;
using SpaceMMO.Domain.Economy;
using SpaceMMO.Domain.Items;
using SpaceMMO.Domain.Quests;
using SpaceMMO.Domain.Universe;
using Xunit;

namespace SpaceMMO.Data.Tests.Content;

/// <summary>
/// Integration tests for loading authored content.
/// </summary>
/// <remarks>
/// These load the <em>real</em> files from <c>data/</c>, not a fixture. That makes the shipped
/// content itself part of the test suite: a typo in a recipe fails the build rather than the
/// server.
/// </remarks>
[Collection(SharedDatabase.Name)]
public sealed class ContentLoaderTests(DatabaseFixture fixture) : IAsyncLifetime
{
    private readonly DatabaseFixture _fixture = fixture;

    public async Task InitializeAsync() => await _fixture.ResetAsync();

    public Task DisposeAsync() => Task.CompletedTask;

    /// <summary>
    /// Walks up from the test binary to the repository's <c>data/</c> directory.
    /// </summary>
    /// <remarks>
    /// Located by walking rather than by a relative path, so the tests keep working if the build
    /// output moves.
    /// </remarks>
    private static string ContentRoot()
    {
        var directory = new DirectoryInfo(AppContext.BaseDirectory);

        while (directory is not null)
        {
            string candidate = Path.Combine(directory.FullName, "data");

            if (Directory.Exists(candidate) && Directory.Exists(Path.Combine(candidate, "items")))
            {
                return candidate;
            }

            directory = directory.Parent;
        }

        throw new DirectoryNotFoundException(
            $"Could not find the repository 'data' directory from {AppContext.BaseDirectory}.");
    }

    // ── The shipped content ──────────────────────────────────────────────────

    [Fact]
    public async Task TheShippedContent_IsValid()
    {
        // The most valuable test here: whatever is in data/ must always pass validation.
        ContentPack pack = await ContentLoader.ReadAsync(ContentRoot());

        IReadOnlyList<ContentError> errors = ContentValidator.Validate(pack);

        Assert.True(
            errors.Count == 0,
            $"Shipped content has problems:{Environment.NewLine}"
            + string.Join(Environment.NewLine, errors.Select(e => $"  {e}")));
    }

    [Fact]
    public async Task TheShippedContent_ContainsTheOnboardingChain()
    {
        ContentPack pack = await ContentLoader.ReadAsync(ContentRoot());

        // Design-bible §4: seven quests totalling 13,000 credits, which is the entire starting
        // money supply per character and the anchor for every early-game price.
        var story = pack.Quests.Where(q => q.Kind == QuestKind.MainStory).ToList();

        Assert.Equal(7, story.Count);
        Assert.Equal(13_000, story.Sum(q => q.RewardCredits));
    }

    [Fact]
    public async Task TheShippedContent_ChainsTheOnboardingQuestsInOrder()
    {
        ContentPack pack = await ContentLoader.ReadAsync(ContentRoot());

        Dictionary<string, QuestContent> byKey = pack.Quests.ToDictionary(q => q.Key);

        // Exactly one entry point, and every other story quest reachable from it.
        var story = pack.Quests.Where(q => q.Kind == QuestKind.MainStory).ToList();

        Assert.Single(story, q => q.Prerequisite is null);

        foreach (QuestContent quest in story.Where(q => q.Prerequisite is not null))
        {
            Assert.True(
                byKey.ContainsKey(quest.Prerequisite!),
                $"'{quest.Key}' requires '{quest.Prerequisite}', which does not exist.");
        }
    }

    // ── Applying ─────────────────────────────────────────────────────────────

    [Fact]
    public async Task LoadingTheShippedContent_PopulatesTheDatabase()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();
        await new ContentLoader(context).LoadAsync(ContentRoot());

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        // Counted against the pack rather than against literals. The claim worth making is that
        // the loader drops nothing, and hard-coded totals do not make it: they fail every time
        // content is authored, which trains whoever is authoring to bump the number without
        // reading why it moved — and a number that is always bumped catches nothing.
        ContentPack pack = await ContentLoader.ReadAsync(ContentRoot());

        Assert.Equal(pack.Skills.Count, await verify.Skills.CountAsync());
        Assert.Equal(pack.Items.Count, await verify.ItemDefs.CountAsync());
        Assert.Equal(pack.Recipes.Count, await verify.Recipes.CountAsync());
        Assert.Equal(pack.Quests.Count, await verify.QuestDefs.CountAsync());
        Assert.Equal(pack.ResourceNodes.Count, await verify.ResourceNodes.CountAsync());

        // And that it loaded something at all, since every count above would pass against an
        // empty directory.
        Assert.NotEmpty(pack.Items);
        Assert.NotEmpty(pack.Recipes);
    }

    [Fact]
    public async Task TheShippedContentReallyLocksTheFourMaterials()
    {
        // Read from the real files, because the rule that enforces planet-locking is worthless if
        // the flag never arrives. A misspelt JSON key leaves PlanetLocked false, the validator
        // finds nothing to check, and every other test in this file still passes — the loudest
        // possible silence.
        ContentPack pack = await ContentLoader.ReadAsync(ContentRoot());

        string[] locked = [.. pack.Items.Where(i => i.PlanetLocked).Select(i => i.Key).Order()];

        Assert.Equal(
            ["ares_regolith", "grimhold_slag", "terran_ferrite", "verdant_amber"], locked);

        // And that each really does come from one world, since that is the fact hauling is built
        // on rather than a property of the flag.
        foreach (string key in locked)
        {
            string[] bodies = [.. pack.ResourceNodes
                .Where(n => n.Item == key)
                .Select(n => n.Body)
                .Distinct()
                .Order()];

            Assert.Single(bodies);
        }

        // The cross-faction recipe consumes all four, which is what makes two of them cross the
        // line to reach any given builder (ADR-0008).
        RecipeContent frame = pack.Recipes.Single(r => r.Key == "build_alloy_frame");

        string[] frameInputs = [.. frame.Inputs.Select(i => i.Item).Order()];

        Assert.Equal<IEnumerable<string>>(locked, frameInputs);
    }

    [Fact]
    public async Task StationsArriveWithSomewhereToBe()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();
        await new ContentLoader(context).LoadAsync(ContentRoot());

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        // A station on a body carries a direction and no system position. Nothing can dock at a
        // station whose position never arrived, so a mis-parsed field presents as a station that
        // exists and cannot be reached.
        Station capital = await verify.Stations.SingleAsync(s => s.Key == "station_capital_hub");

        Assert.NotNull(capital.BodyId);
        Assert.NotNull(capital.DirectionX);
        Assert.Null(capital.SystemX);
        Assert.True(capital.DockingRangeKilometres > 0.0);

        // And the deep-space one is the mirror image, which is what makes the second branch real
        // rather than a code path nothing exercises.
        Station deepdock = await verify.Stations.SingleAsync(s => s.Key == "station_deepdock");

        Assert.Null(deepdock.BodyId);
        Assert.Null(deepdock.DirectionX);
        Assert.NotNull(deepdock.SystemX);
        Assert.Equal(StationKind.Spaceport, deepdock.Kind);

        // Every station is placed exactly one way. Both at once is what the validator refuses, and
        // it can only be reached by editing a station that already existed — the case nobody
        // reloads by hand.
        foreach (Station station in await verify.Stations.ToListAsync())
        {
            Assert.True(
                (station.DirectionX is null) != (station.SystemX is null),
                $"{station.Key} is placed {(station.DirectionX is null ? "no" : "both")} ways.");
        }
    }

    [Fact]
    public async Task MovingAStationIntoDeepSpaceLeavesNoOldPositionBehind()
    {
        await using (SpaceMmoDbContext first = _fixture.CreateContext())
        {
            await new ContentLoader(first).LoadAsync(ContentRoot());
        }

        ContentPack pack = await ContentLoader.ReadAsync(ContentRoot());

        // The same station, relocated off its body. A reload that only wrote the new position
        // would leave the old direction beside it, producing exactly the row the validator will
        // not let anyone author.
        StationContent moved = pack.Stations.Single(s => s.Key == "station_capital_hub") with
        {
            Body = null,
            Direction = null,
            SystemPosition = [12.0, 3.0, -4.0],
        };

        ContentPack edited = pack with
        {
            Stations = [.. pack.Stations.Where(s => s.Key != moved.Key), moved],
        };

        await using (SpaceMmoDbContext second = _fixture.CreateContext())
        {
            await new ContentLoader(second).ApplyAsync(edited);
        }

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Station station = await verify.Stations.SingleAsync(s => s.Key == moved.Key);

        Assert.Null(station.DirectionX);
        Assert.Null(station.DirectionY);
        Assert.Null(station.DirectionZ);
        Assert.Equal(12.0, station.SystemX);
    }

    [Fact]
    public async Task LoadedRecipes_KeepTheirInputsAndGates()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();
        await new ContentLoader(context).LoadAsync(ContentRoot());

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Recipe refine = await verify.Recipes
            .Include(r => r.Inputs)
            .SingleAsync(r => r.Key == "refine_ferrite_plate");

        Assert.Equal(4, refine.OutputQuantity);
        Assert.Equal(60, refine.JobSeconds);

        RecipeInput input = Assert.Single(refine.Inputs);
        Assert.Equal(20, input.Quantity);
    }

    [Fact]
    public async Task LoadedQuests_KeepTheirPrerequisiteChain()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();
        await new ContentLoader(context).LoadAsync(ContentRoot());

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        QuestDef craftTool = await verify.QuestDefs
            .Include(q => q.PrerequisiteQuestDef)
            .SingleAsync(q => q.Key == "intro_craft_tool");

        Assert.Equal("intro_gather_scrap", craftTool.PrerequisiteQuestDef!.Key);

        // Credits are authored as whole credits and stored as minor units.
        Assert.Equal(Credits.FromWholeCredits(750), craftTool.RewardCredits);
    }

    [Fact]
    public async Task LoadedItems_KeepTheirCategories()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();
        await new ContentLoader(context).LoadAsync(ContentRoot());

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        ItemDef laser = await verify.ItemDefs.SingleAsync(i => i.Key == "crude_mining_laser");

        Assert.Equal(ItemCategory.Tool, laser.Category);
        Assert.False(laser.IsStackable);
    }

    // ── Idempotence ──────────────────────────────────────────────────────────

    [Fact]
    public async Task LoadingTwice_ChangesNothing()
    {
        // Content loads at every startup, so it must converge on the files rather than
        // accumulating duplicates.
        await using (SpaceMmoDbContext first = _fixture.CreateContext())
        {
            await new ContentLoader(first).LoadAsync(ContentRoot());
        }

        await using (SpaceMmoDbContext second = _fixture.CreateContext())
        {
            await new ContentLoader(second).LoadAsync(ContentRoot());
        }

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        ContentPack pack = await ContentLoader.ReadAsync(ContentRoot());

        Assert.Equal(pack.Items.Count, await verify.ItemDefs.CountAsync());
        Assert.Equal(pack.Recipes.Count, await verify.Recipes.CountAsync());
        Assert.Equal(pack.Quests.Count, await verify.QuestDefs.CountAsync());

        Assert.Equal(
            pack.Quests.Sum(q => q.Steps.Count), await verify.QuestSteps.CountAsync());

        // Doubling here would mean the wholesale replace is appending rather than replacing,
        // which is the failure this test exists for and the one a literal total hides once
        // somebody has bumped it twice.
        Assert.Equal(
            pack.Recipes.Sum(r => r.Inputs.Count), await verify.RecipeInputs.CountAsync());
    }

    [Fact]
    public async Task Reloading_AppliesEdits()
    {
        await using (SpaceMmoDbContext first = _fixture.CreateContext())
        {
            await new ContentLoader(first).LoadAsync(ContentRoot());
        }

        ContentPack pack = await ContentLoader.ReadAsync(ContentRoot());

        // A balance change: halve the refining duration.
        RecipeContent original = pack.Recipes.Single(r => r.Key == "refine_ferrite_plate");
        RecipeContent tweaked = original with { JobSeconds = 30 };

        var edited = pack with
        {
            Recipes = [.. pack.Recipes.Where(r => r.Key != original.Key), tweaked],
        };

        await using (SpaceMmoDbContext second = _fixture.CreateContext())
        {
            await new ContentLoader(second).ApplyAsync(edited);
        }

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.Equal(
            30, (await verify.Recipes.SingleAsync(r => r.Key == original.Key)).JobSeconds);

        // Edited, not added.
        Assert.Equal(edited.Recipes.Count, await verify.Recipes.CountAsync());
    }

    [Fact]
    public async Task Reloading_RemovesIngredientsThatWereDropped()
    {
        // Inputs are replaced wholesale rather than diffed. A dropped ingredient left behind would
        // quietly keep charging players for a material the recipe no longer needs.
        await using (SpaceMmoDbContext first = _fixture.CreateContext())
        {
            await new ContentLoader(first).LoadAsync(ContentRoot());
        }

        ContentPack pack = await ContentLoader.ReadAsync(ContentRoot());

        RecipeContent original = pack.Recipes.Single(r => r.Key == "build_shuttle_hull_section");
        Assert.Equal(2, original.Inputs.Count);

        RecipeContent simplified = original with
        {
            Inputs = [original.Inputs.First(i => i.Item == "ferrite_plate")],
        };

        var edited = pack with
        {
            Recipes = [.. pack.Recipes.Where(r => r.Key != original.Key), simplified],
        };

        await using (SpaceMmoDbContext second = _fixture.CreateContext())
        {
            await new ContentLoader(second).ApplyAsync(edited);
        }

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Recipe stored = await verify.Recipes
            .Include(r => r.Inputs)
            .SingleAsync(r => r.Key == original.Key);

        Assert.Single(stored.Inputs);
    }

    // ── Failure modes ────────────────────────────────────────────────────────

    [Fact]
    public async Task InvalidContent_IsRejectedBeforeAnythingIsWritten()
    {
        // Applied as a unit, so one bad recipe cannot leave the database half-updated.
        var broken = new ContentPack(
            [],
            [],
            [new RecipeContent("r1", "ghost", 1, "phantom", 1, 60, 0, null,
                [new RecipeInputContent("vapour", 1)])],
            [],
            [],
            [],
            [],
            []);

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        await Assert.ThrowsAsync<InvalidOperationException>(
            () => new ContentLoader(context).ApplyAsync(broken));

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.Equal(0, await verify.Recipes.CountAsync());
    }

    [Fact]
    public async Task AMissingContentDirectory_FailsClearly()
    {
        await Assert.ThrowsAsync<DirectoryNotFoundException>(
            () => ContentLoader.ReadAsync(Path.Combine(Path.GetTempPath(), "spacemmo-not-here")));
    }
}
