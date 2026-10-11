using Microsoft.EntityFrameworkCore;
using SpaceMMO.Data.Content;
using SpaceMMO.Data.Docking;
using SpaceMMO.Data.Entities;
using SpaceMMO.Data.Industry;
using SpaceMMO.Data.Inventories;
using SpaceMMO.Data.Quests;
using SpaceMMO.Domain.Characters;
using SpaceMMO.Domain.Content;
using SpaceMMO.Domain.Economy;
using SpaceMMO.Domain.Items;
using SpaceMMO.Domain.Progression;
using SpaceMMO.Domain.Quests;
using Xunit;

namespace SpaceMMO.Data.Tests.Content;

/// <summary>
/// Content the pack stops naming is retired, not deleted and not left serving (task 188).
/// </summary>
/// <remarks>
/// Each test seeds the shipped pack, then seeds it again with something taken out -- what an author
/// does with the editor's authoring panel, or by deleting a block of JSON -- and asks what the
/// database does about it. Joe, 11 October: nothing new starts from a retired row, and everything that
/// already refers to it stays valid.
/// </remarks>
[Collection(SharedDatabase.Name)]
public sealed class RetiredContentTests(DatabaseFixture fixture) : IAsyncLifetime
{
    private readonly DatabaseFixture _fixture = fixture;

    private ContentPack _shipped = ContentPack.Empty;

    public async Task InitializeAsync()
    {
        await _fixture.ResetAsync();

        _shipped = await ContentLoader.ReadAsync(ContentRoot());

        await SeedAsync(_shipped);
    }

    public Task DisposeAsync() => Task.CompletedTask;

    // ── Stations ─────────────────────────────────────────────────────────────

    [Fact]
    public async Task A_station_taken_out_of_the_pack_is_retired_and_cannot_be_docked_at()
    {
        // The case the authoring panel can produce today: RemoveEntry cuts a station out of
        // origin.json, and the next seed used to leave it in the database, served and dockable.
        int deepdock = await StationIdAsync("station_deepdock");
        int character = await CharacterAsync(Race.Humanoid, "Stranded");

        await DockAsync(character, deepdock);

        await SeedAsync(_shipped with { Stations = [.. _shipped.Stations.Where(s => s.Key != "station_deepdock")] });

        await using (SpaceMmoDbContext verify = _fixture.CreateContext())
        {
            Station retired = await verify.Stations.SingleAsync(s => s.Id == deepdock);

            Assert.NotNull(retired.RetiredAt);

            // Kept, not deleted: its row, its hangar and the character docked there are all still valid.
            Assert.True(await verify.Inventories.AnyAsync(i => i.StationId == deepdock));
            Assert.Equal(deepdock, (await verify.Characters.SingleAsync(c => c.Id == character)).DockedStationId);

            // And the stations that stayed are untouched.
            Assert.Null((await verify.Stations.SingleAsync(s => s.Key == "station_capital_hub")).RetiredAt);
        }

        int other = await CharacterAsync(Race.Martian, "Latecomer");

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        await Assert.ThrowsAsync<UnknownStationException>(() =>
            new DockingService(context, new InventoryService(context)).DockAsync(other, deepdock));
    }

    [Fact]
    public async Task Authoring_a_retired_key_again_brings_it_back()
    {
        await SeedAsync(_shipped with { Stations = [.. _shipped.Stations.Where(s => s.Key != "station_deepdock")] });
        await SeedAsync(_shipped);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.Null((await verify.Stations.SingleAsync(s => s.Key == "station_deepdock")).RetiredAt);
    }

    [Fact]
    public async Task A_pack_that_says_nothing_about_a_kind_retires_none_of_it()
    {
        // As with deposits (184): silence about stations is not a statement that there are none, and
        // the content tests load packs of one kind at a time.
        await SeedAsync(_shipped with { Stations = [] });

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.False(await verify.Stations.AnyAsync(s => s.RetiredAt != null));
    }

    [Fact]
    public async Task Retiring_twice_keeps_when_it_was_first_retired()
    {
        ContentPack without = _shipped with { Stations = [.. _shipped.Stations.Where(s => s.Key != "station_deepdock")] };

        await SeedAsync(without);

        DateTimeOffset first;

        await using (SpaceMmoDbContext read = _fixture.CreateContext())
        {
            first = (await read.Stations.SingleAsync(s => s.Key == "station_deepdock")).RetiredAt!.Value;
        }

        await SeedAsync(without);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.Equal(first, (await verify.Stations.SingleAsync(s => s.Key == "station_deepdock")).RetiredAt);
    }

    // ── Recipes and items ────────────────────────────────────────────────────

    [Fact]
    public async Task A_retired_recipe_starts_no_job_but_one_already_running_can_be_claimed()
    {
        int character = await CharacterAsync(Race.Humanoid, "Shipwright");
        int station = await StationIdAsync("station_terra_hub");
        int recipe = await RecipeIdAsync("craft_crude_mining_laser");

        await DockAsync(character, station);
        await GiveAsync(character, station, "scrap_alloy", 16);

        long running;

        await using (SpaceMmoDbContext start = _fixture.CreateContext())
        {
            running = (await new IndustryService(start).StartJobAsync(character, recipe, station, 1)).JobId;
        }

        await SeedAsync(WithoutRecipe("craft_crude_mining_laser"));

        await using (SpaceMmoDbContext refused = _fixture.CreateContext())
        {
            ContentRetiredException error = await Assert.ThrowsAsync<ContentRetiredException>(() =>
                new IndustryService(refused).StartJobAsync(character, recipe, station, 1));

            Assert.Equal("craft_crude_mining_laser", error.Key);
        }

        await using (SpaceMmoDbContext shift = _fixture.CreateContext())
        {
            IndustryJob job = await shift.IndustryJobs.SingleAsync(j => j.Id == running);
            job.CompletesAt = DateTimeOffset.UtcNow.AddSeconds(-1);
            await shift.SaveChangesAsync();
        }

        await using SpaceMmoDbContext claim = _fixture.CreateContext();

        ClaimJobResult claimed = await new IndustryService(claim).ClaimJobAsync(running, character);

        Assert.Equal(1, claimed.Quantity);
    }

    [Fact]
    public async Task Goods_of_a_retired_item_are_kept_and_still_move()
    {
        // Joe, 11 October: held goods keep working. Only new supply stops, and the pack has none: the
        // item goes out together with the one recipe that made it.
        int character = await CharacterAsync(Race.Humanoid, "Hoarder");
        int station = await StationIdAsync("station_terra_hub");

        await DockAsync(character, station);
        await GiveAsync(character, station, "alloy_frame", 3);

        // The frame, the recipe that makes it, and the freighter's recipe that consumes it: what an
        // author has to take out together for the pack to validate.
        ContentPack without = _shipped with
        {
            Items = [.. _shipped.Items.Where(i => i.Key != "alloy_frame")],
            Recipes = [.. _shipped.Recipes.Where(r => r.Key is not "build_alloy_frame" and not "assemble_hull_freighter")],
        };

        await SeedAsync(without);

        await using (SpaceMmoDbContext verify = _fixture.CreateContext())
        {
            Assert.NotNull((await verify.ItemDefs.SingleAsync(d => d.Key == "alloy_frame")).RetiredAt);
            Assert.NotNull((await verify.Recipes.SingleAsync(r => r.Key == "assemble_hull_freighter")).RetiredAt);
            Assert.Null((await verify.ItemDefs.SingleAsync(d => d.Key == "hull_freighter")).RetiredAt);
        }

        await using SpaceMmoDbContext context = _fixture.CreateContext();
        var inventories = new InventoryService(context);

        Inventory hangar = await inventories.GetOrCreateStationHangarAsync(character, station);
        Inventory pockets = await inventories.GetOrCreateCarriedAsync(character);
        pockets.CapacityM3 = 1_000.0;

        await inventories.TransferAsync(hangar.Id, pockets.Id, await ItemIdAsync("alloy_frame"), 2);
        await context.SaveChangesAsync();

        Assert.Equal(2, await inventories.QuantityOfAsync(pockets.Id, await ItemIdAsync("alloy_frame")));
    }

    // ── Quests ───────────────────────────────────────────────────────────────

    [Fact]
    public async Task A_retired_quest_is_refused_but_one_under_way_keeps_its_journal()
    {
        string last = LastMainStoryQuest();

        int newcomer = await CharacterAsync(Race.Humanoid, "Newcomer");
        int veteran = await CharacterAsync(Race.Martian, "Veteran");

        await using (SpaceMmoDbContext context = _fixture.CreateContext())
        {
            QuestDef quest = await context.QuestDefs.SingleAsync(q => q.Key == last);

            context.CharacterQuests.Add(new CharacterQuest
            {
                CharacterId = veteran,
                QuestDefId = quest.Id,
                State = QuestState.InProgress,
                StepOrdinal = 1,
                StepProgress = 0,
                StartedAt = DateTimeOffset.UtcNow,
            });

            await context.SaveChangesAsync();
        }

        await SeedAsync(_shipped with { Quests = [.. _shipped.Quests.Where(q => q.Key != last)] });

        await using (SpaceMmoDbContext accept = _fixture.CreateContext())
        {
            await Assert.ThrowsAsync<ContentRetiredException>(() =>
                new QuestService(accept).AcceptAsync(newcomer, last));
        }

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.True(await verify.CharacterQuests.AnyAsync(
            cq => cq.CharacterId == veteran && cq.State == QuestState.InProgress));

        // Its steps stay too: the journal reads the current step's words from them.
        Assert.True(await verify.QuestSteps.AnyAsync(s => s.QuestDef!.Key == last));
    }

    // ── Helpers ──────────────────────────────────────────────────────────────

    private ContentPack WithoutRecipe(string key) =>
        _shipped with { Recipes = [.. _shipped.Recipes.Where(r => r.Key != key)] };

    /// <summary>The main-story quest nothing else waits on, so taking it out leaves a valid pack.</summary>
    private string LastMainStoryQuest()
    {
        HashSet<string> prerequisites = [.. _shipped.Quests.Select(q => q.Prerequisite).OfType<string>()];

        return _shipped.Quests.Single(q => q.Kind == QuestKind.MainStory && !prerequisites.Contains(q.Key)).Key;
    }

    private async Task SeedAsync(ContentPack pack)
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        await new ContentLoader(context).ApplyAsync(pack);
    }

    private async Task<int> CharacterAsync(Race race, string name)
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        string homeKey = Races.HomeBodyKeyFor(race);

        var account = new Account
        {
            Email = $"{name.ToLowerInvariant()}@example.com",
            PasswordHash = "not-a-real-hash",
            CreatedAt = DateTimeOffset.UtcNow,
        };
        context.Accounts.Add(account);
        await context.SaveChangesAsync();

        var character = new Character
        {
            AccountId = account.Id,
            Name = name,
            Race = race,
            HomeBodyId = await context.Bodies.Where(b => b.Key == homeKey).Select(b => b.Id).SingleAsync(),
            Balance = Credits.FromWholeCredits(50_000),
            CreatedAt = DateTimeOffset.UtcNow,
        };
        context.Characters.Add(character);
        await context.SaveChangesAsync();

        foreach (Skill skill in await context.Skills.ToListAsync())
        {
            context.CharacterSkills.Add(new CharacterSkill
            {
                CharacterId = character.Id,
                SkillId = skill.Id,
                Xp = SkillCurve.XpForLevel(50),
            });
        }

        await context.SaveChangesAsync();

        return character.Id;
    }

    private async Task DockAsync(int character, int station)
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        await new DockingService(context, new InventoryService(context)).DockAsync(character, station);
    }

    private async Task GiveAsync(int character, int station, string itemKey, int quantity)
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();
        var inventories = new InventoryService(context);

        Inventory hangar = await inventories.GetOrCreateStationHangarAsync(character, station);

        await inventories.AddAsync(hangar.Id, await ItemIdAsync(itemKey), quantity, Credits.Zero);
        await context.SaveChangesAsync();
    }

    private async Task<int> StationIdAsync(string key)
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        return await context.Stations.Where(s => s.Key == key).Select(s => s.Id).SingleAsync();
    }

    private async Task<int> RecipeIdAsync(string key)
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        return await context.Recipes.Where(r => r.Key == key).Select(r => r.Id).SingleAsync();
    }

    private async Task<int> ItemIdAsync(string key)
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        return await context.ItemDefs.Where(d => d.Key == key).Select(d => d.Id).SingleAsync();
    }

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
}
