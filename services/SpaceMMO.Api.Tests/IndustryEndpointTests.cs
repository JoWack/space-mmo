using System.Net;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using SpaceMMO.Api.Endpoints;
using SpaceMMO.Data;
using SpaceMMO.Data.Entities;
using SpaceMMO.Data.Industry;
using SpaceMMO.Domain.Characters;
using SpaceMMO.Domain.Economy;
using SpaceMMO.Domain.Industry;
using SpaceMMO.Domain.Items;
using SpaceMMO.Domain.Progression;
using SpaceMMO.Domain.Universe;
using Xunit;

namespace SpaceMMO.Api.Tests;

/// <summary>
/// The two read endpoints a client needs before it can craft anything.
/// </summary>
/// <remarks>
/// Starting a job takes a recipe id, so without a catalog to read, a client could only craft by
/// hard-coding database ids -- which works until the database is next seeded in a different order
/// and then silently builds the wrong thing. Listing jobs matters for the same class of reason:
/// whether a job is finished is a question about the server's clock, and a client that answered it
/// locally would offer a claim button the server refuses.
/// </remarks>
[Collection(SharedApiDatabase.Name)]
public sealed class IndustryEndpointTests(ApiDatabaseFixture fixture) : IAsyncLifetime, IDisposable
{
    private readonly ApiDatabaseFixture _fixture = fixture;

    private ApiFactory _factory = null!;
    private HttpClient _client = null!;

    private int _recipeId;
    private int _stationId;
    private int _oreItemDefId;

    public async Task InitializeAsync()
    {
        await _fixture.ResetAsync();

        _factory = new ApiFactory(_fixture.ConnectionString);
        _client = _factory.CreateClient();

        await AuthorizationTests.SeedStartingWorldAsync(_fixture);
        await SeedRecipeAsync();
    }

    public Task DisposeAsync() => Task.CompletedTask;

    public void Dispose()
    {
        _client?.Dispose();
        _factory?.Dispose();
    }

    [Fact]
    public async Task Recipes_are_readable_without_signing_in()
    {
        // No token, matching bodies and deposits. What can be built is authored content, identical
        // for everyone, and the dedicated server has to be able to read it without holding a
        // player's credentials.
        HttpResponseMessage response = await _client.GetAsync("/industry/recipes");

        Assert.Equal(HttpStatusCode.OK, response.StatusCode);

        RecipeResponse[] recipes =
            (await response.Content.ReadFromJsonAsync<RecipeResponse[]>())!;

        Assert.NotEmpty(recipes);
    }

    [Fact]
    public async Task A_recipe_carries_the_keys_a_client_needs_and_not_only_ids()
    {
        RecipeResponse recipe = Assert.Single(
            (await _client.GetFromJsonAsync<RecipeResponse[]>("/industry/recipes"))!);

        // Ids are assigned by the database and differ between any two seeded environments. A client
        // that remembered one would break the day the database was rebuilt in a different order,
        // and it would break by quietly referring to a different recipe rather than by failing.
        Assert.Equal("refine_test_plate", recipe.Key);
        Assert.Equal("test_plate", recipe.OutputItemKey);
        Assert.Equal("refining", recipe.SkillKey);

        Assert.Equal(4, recipe.OutputQuantity);
        Assert.Equal(1, recipe.RequiredLevel);
        Assert.Equal(60, recipe.JobSeconds);

        RecipeInputResponse input = Assert.Single(recipe.Inputs);

        Assert.Equal("test_ore", input.ItemKey);
        Assert.Equal(20, input.Quantity);
    }

    [Fact]
    public async Task A_running_job_reports_the_time_left_rather_than_letting_the_client_work_it_out()
    {
        (string token, int characterId) = await SignedInCharacterAsync("crafter@example.com");

        await StockCharacterAsync(characterId, 40);

        await StartJobAsync(characterId, token);

        IndustryJobResponse job = Assert.Single(await JobsAsync(characterId, token));

        Assert.Equal("refine_test_plate", job.RecipeKey);
        Assert.Equal(IndustryJobState.Running, job.State);

        // The job runs for 60 seconds, so it cannot possibly be claimable yet. Both fields are the
        // server's answer about the server's clock; the client is only allowed to render them.
        Assert.False(job.IsClaimable);
        Assert.InRange(job.SecondsRemaining, 1, 60);

        // Four plates per run, one run.
        Assert.Equal(4, job.OutputQuantityTotal);
    }

    [Fact]
    public async Task A_cancelled_job_stops_being_listed()
    {
        (string token, int characterId) = await SignedInCharacterAsync("canceller@example.com");

        await StockCharacterAsync(characterId, 40);

        StartJobResult result = await StartJobAsync(characterId, token);

        Assert.Single(await JobsAsync(characterId, token));

        HttpResponseMessage cancelled = await PostAsync(
            "/industry/jobs/cancel", new { characterId, jobId = result.JobId }, token);

        Assert.Equal(HttpStatusCode.OK, cancelled.StatusCode);

        // Terminal jobs are history, not something to act on. A list that kept them would leave a
        // client one bug away from offering to claim a job that has already been resolved.
        Assert.Empty(await JobsAsync(characterId, token));
    }

    [Fact]
    public async Task A_brand_new_character_can_afford_its_first_job()
    {
        // The bug this pins down was reachable by simply playing: creation left the balance at
        // zero, every job charges a fee, and the only faucet was a questline the client could not
        // yet reach. A player could mine ore indefinitely and never refine any of it, with the
        // refusal naming a sum they had no way to earn.
        (string token, int characterId) = await SignedInCharacterAsync("newcomer@example.com");

        // Ore, and deliberately no credits. The stake granted at creation is the whole point.
        await StockCharacterAsync(characterId, 40, grantCredits: false);

        await StartJobAsync(characterId, token);

        Assert.Single(await JobsAsync(characterId, token));
    }

    [Fact]
    public async Task Listing_another_accounts_jobs_reports_not_found()
    {
        (_, int characterId) = await SignedInCharacterAsync("owner@example.com");

        (string attackerToken, _) = await SignedInCharacterAsync("thief@example.com");

        HttpResponseMessage response =
            await GetAsync($"/industry/jobs?characterId={characterId}", attackerToken);

        // Not Forbidden -- matching every other ownership check here. "That exists but is not
        // yours" would make the endpoint an oracle for which character ids are real.
        Assert.Equal(HttpStatusCode.NotFound, response.StatusCode);
    }

    /// <summary>
    /// Starts the seeded recipe, failing with whatever the server actually objected to.
    /// </summary>
    /// <remarks>
    /// The endpoint maps five distinct refusals -- skill, slots, tool, materials, funds -- onto the
    /// same 409, distinguished only by a reason in the body. A bare status assertion here would
    /// report "expected OK, got Conflict" and leave which of the five entirely open.
    /// </remarks>
    private async Task<StartJobResult> StartJobAsync(int characterId, string token)
    {
        HttpResponseMessage response = await PostAsync(
            "/industry/jobs",
            new { characterId, recipeId = _recipeId, stationId = _stationId, runs = 1 },
            token);

        Assert.True(
            response.StatusCode == HttpStatusCode.OK,
            $"Starting the job returned {(int)response.StatusCode}: "
            + await response.Content.ReadAsStringAsync());

        return (await response.Content.ReadFromJsonAsync<StartJobResult>())!;
    }

    private async Task<IndustryJobResponse[]> JobsAsync(int characterId, string token)
    {
        HttpResponseMessage response =
            await GetAsync($"/industry/jobs?characterId={characterId}", token);

        Assert.Equal(HttpStatusCode.OK, response.StatusCode);

        return (await response.Content.ReadFromJsonAsync<IndustryJobResponse[]>())!;
    }

    private async Task<(string Token, int CharacterId)> SignedInCharacterAsync(string email)
    {
        HttpResponseMessage registered = await _client.PostAsJsonAsync(
            new Uri("/accounts/register", UriKind.Relative),
            new { email, password = "a-sufficiently-long-password" });

        registered.EnsureSuccessStatusCode();

        string token = (await registered.Content.ReadFromJsonAsync<SessionPayload>())!.Token;

        // Named from the address, because character names are unique and a fixed one would make
        // any test that needs two characters collide with itself.
        string name = email[..email.IndexOf('@', StringComparison.Ordinal)];

        HttpResponseMessage created = await PostAsync(
            "/characters/", new { name, race = Race.Humanoid }, token);

        created.EnsureSuccessStatusCode();

        CharacterResponse character =
            (await created.Content.ReadFromJsonAsync<CharacterResponse>())!;

        return (token, character.Id);
    }

    /// <summary>
    /// Puts ore in the character's hangar and credits in their account.
    /// </summary>
    /// <remarks>
    /// <strong>The credits are not incidental.</strong> Every industry job charges a fee, and a
    /// freshly created character has a zero balance -- so without this, starting any job at all
    /// fails with <c>insufficient_funds</c> rather than for any reason the test is about.
    ///
    /// In the real game this is covered, but only just: <c>intro_gather_scrap</c> pays 500 cr and
    /// gathering costs nothing, so a player is solvent by the time the questline asks them to craft.
    /// That the tutorial does not soft-lock is a consequence of quest ordering rather than of
    /// anything enforcing it.
    /// </remarks>
    private async Task StockCharacterAsync(
        int characterId, int oreQuantity, bool grantCredits = true)
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        if (grantCredits)
        {
            Character character = context.Characters.Single(c => c.Id == characterId);
            character.Balance = Credits.FromMinorUnits(100_00);
        }

        var hangar = new Inventory
        {
            CharacterId = characterId,
            StationId = _stationId,
            Kind = InventoryKind.StationHangar,
            CapacityM3 = 0,
        };

        context.Inventories.Add(hangar);
        await context.SaveChangesAsync();

        context.InventoryItems.Add(new InventoryItem
        {
            InventoryId = hangar.Id,
            ItemDefId = _oreItemDefId,
            Quantity = oreQuantity,
            CostBasis = Credits.Zero,
        });

        await context.SaveChangesAsync();
    }

    private async Task<HttpResponseMessage> GetAsync(string path, string token)
    {
        using var request = new HttpRequestMessage(HttpMethod.Get, new Uri(path, UriKind.Relative));
        request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", token);

        return await _client.SendAsync(request);
    }

    private async Task<HttpResponseMessage> PostAsync(string path, object body, string token)
    {
        using var request = new HttpRequestMessage(HttpMethod.Post, new Uri(path, UriKind.Relative))
        {
            Content = JsonContent.Create(body),
        };

        request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", token);

        return await _client.SendAsync(request);
    }

    private async Task SeedRecipeAsync()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        var ore = new ItemDef
        {
            Key = "test_ore",
            Name = "Test Ore",
            Category = ItemCategory.Raw,
            VolumeM3 = 0.4,
        };

        var plate = new ItemDef
        {
            Key = "test_plate",
            Name = "Test Plate",
            Category = ItemCategory.Refined,
            VolumeM3 = 0.2,
        };

        var refining = new Skill
        {
            Key = "refining",
            Name = "Refining",
            Category = SkillCategory.Life,
        };

        context.ItemDefs.AddRange(ore, plate);
        context.Skills.Add(refining);
        await context.SaveChangesAsync();

        _oreItemDefId = ore.Id;

        var recipe = new Recipe
        {
            Key = "refine_test_plate",
            OutputItemDefId = plate.Id,
            OutputQuantity = 4,
            SkillId = refining.Id,
            RequiredLevel = 1,
            JobSeconds = 60,
            XpPerRun = 600,
        };

        context.Recipes.Add(recipe);
        await context.SaveChangesAsync();

        _recipeId = recipe.Id;

        context.RecipeInputs.Add(new RecipeInput
        {
            RecipeId = recipe.Id,
            ItemDefId = ore.Id,
            Quantity = 20,
        });

        // Seeded here rather than assumed: SeedStartingWorldAsync creates the system and the four
        // homeworlds but no stations, and a job has to run somewhere.
        Body home = context.Bodies.OrderBy(b => b.Id).First();

        var station = new Station
        {
            Key = "station_test_hub",
            Name = "Test Hub",
            StarSystemId = home.StarSystemId,
            BodyId = home.Id,
            Kind = StationKind.TradingHub,
        };

        context.Stations.Add(station);
        await context.SaveChangesAsync();

        _stationId = station.Id;
    }

    private sealed record SessionPayload(int AccountId, string Token, DateTimeOffset ExpiresAt);
}
