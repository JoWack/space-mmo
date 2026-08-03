using Microsoft.EntityFrameworkCore;
using SpaceMMO.Data.Content;
using SpaceMMO.Data.Entities;
using SpaceMMO.Domain.Content;
using SpaceMMO.Domain.Economy;
using SpaceMMO.Domain.Items;
using SpaceMMO.Domain.Quests;
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

        Assert.Equal(5, await verify.Skills.CountAsync());
        Assert.Equal(7, await verify.ItemDefs.CountAsync());
        Assert.Equal(4, await verify.Recipes.CountAsync());
        Assert.Equal(7, await verify.QuestDefs.CountAsync());
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

        Assert.Equal(7, await verify.ItemDefs.CountAsync());
        Assert.Equal(4, await verify.Recipes.CountAsync());
        Assert.Equal(7, await verify.QuestDefs.CountAsync());
        Assert.Equal(7, await verify.QuestSteps.CountAsync());

        // 1 + 1 + 2 + 2 across the four recipes. Doubling here would mean the wholesale replace
        // is appending rather than replacing.
        Assert.Equal(6, await verify.RecipeInputs.CountAsync());
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
        Assert.Equal(4, await verify.Recipes.CountAsync());
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
