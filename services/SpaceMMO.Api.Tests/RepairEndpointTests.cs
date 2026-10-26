using System.Net;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Text.Json;
using Microsoft.EntityFrameworkCore;
using SpaceMMO.Api.Endpoints;
using SpaceMMO.Data;
using SpaceMMO.Data.Entities;
using SpaceMMO.Data.Inventories;
using SpaceMMO.Domain.Characters;
using SpaceMMO.Domain.Economy;
using SpaceMMO.Domain.Items;
using SpaceMMO.Domain.Progression;
using SpaceMMO.Domain.Universe;
using Xunit;
using Xunit.Abstractions;

namespace SpaceMMO.Api.Tests;

/// <summary>
/// The REPAIRS section's two calls, over the wire (task 196): the priced list, and the instant repair.
/// </summary>
/// <remarks>
/// The JSON is written to the test output as the API sends it, because the client parses it by name and a test that
/// only deserialised into these same records would agree with itself whatever the names were.
/// </remarks>
[Collection(SharedApiDatabase.Name)]
public sealed class RepairEndpointTests(ApiDatabaseFixture fixture, ITestOutputHelper output) : IAsyncLifetime, IDisposable
{
    private readonly ApiDatabaseFixture _fixture = fixture;
    private readonly ITestOutputHelper _output = output;

    private ApiFactory _factory = null!;
    private HttpClient _client = null!;

    private int _stationId;
    private int _scrapId;
    private int _laserId;

    public async Task InitializeAsync()
    {
        await _fixture.ResetAsync();

        _factory = new ApiFactory(_fixture.ConnectionString);
        _client = _factory.CreateClient();

        await AuthorizationTests.SeedStartingWorldAsync(_fixture);
        await SeedAsync();
    }

    public Task DisposeAsync() => Task.CompletedTask;

    public void Dispose()
    {
        _client?.Dispose();
        _factory?.Dispose();
    }

    [Fact]
    public async Task A_broken_laser_is_listed_priced_and_repaired()
    {
        (string token, int characterId) = await SignedInCharacterAsync("mender@example.com");
        long laser = await BrokenLaserAsync(characterId, scrap: 5);

        HttpResponseMessage listed = await GetAsync($"/industry/repairs?characterId={characterId}&stationId={_stationId}", token);
        string listedText = await listed.Content.ReadAsStringAsync();
        _output.WriteLine($"GET /industry/repairs: {listedText}");
        Assert.Equal(HttpStatusCode.OK, listed.StatusCode);

        JsonElement quote = Assert.Single(JsonDocument.Parse(listedText).RootElement.EnumerateArray());
        Assert.Equal(laser, quote.GetProperty("itemInstanceId").GetInt64());
        Assert.Equal(0, quote.GetProperty("condition").GetInt32());
        Assert.Equal(10_000, quote.GetProperty("maxConditionHundredths").GetInt32());
        Assert.Equal(82, quote.GetProperty("conditionAfter").GetInt32());
        Assert.Equal(8200, quote.GetProperty("maxConditionAfterHundredths").GetInt32());
        Assert.False(quote.GetProperty("wornOut").GetBoolean());
        Assert.True(quote.GetProperty("priced").GetBoolean());
        Assert.Equal(15_00, quote.GetProperty("creditsMinorUnits").GetInt64());

        JsonElement scrap = Assert.Single(quote.GetProperty("materials").EnumerateArray());
        Assert.Equal("scrap_alloy", scrap.GetProperty("itemKey").GetString());
        Assert.Equal((2, 5, 0), (scrap.GetProperty("units").GetInt32(), scrap.GetProperty("held").GetInt32(), scrap.GetProperty("owedAfterTenThousandths").GetInt32()));

        HttpResponseMessage repaired = await PostAsync(
            "/industry/repairs", new { characterId, stationId = _stationId, itemInstanceId = laser }, token);
        string repairedText = await repaired.Content.ReadAsStringAsync();
        _output.WriteLine($"POST /industry/repairs: {repairedText}");
        Assert.Equal(HttpStatusCode.OK, repaired.StatusCode);

        RepairResultResponse result = JsonSerializer.Deserialize<RepairResultResponse>(repairedText, JsonSerializerOptions.Web)!;
        Assert.False(result.Destroyed);
        Assert.Equal((82, 8200, 15_00L), (result.Condition, result.MaxConditionHundredths, result.CreditsMinorUnits));
        Assert.Equal(new RepairChargedResponse(_scrapId, 2), Assert.Single(result.Charged));

        // The inventory says the new maximum too, for "82 / 82" wherever the item is shown.
        InventoryResponse inventory = (await (await GetAsync($"/characters/{characterId}/inventory", token))
            .Content.ReadFromJsonAsync<InventoryResponse>())!;
        Assert.Equal(8200, inventory.Items.Single(i => i.Id == laser).MaxConditionHundredths);

        // And nothing is left to repair.
        Assert.Empty((await (await GetAsync($"/industry/repairs?characterId={characterId}&stationId={_stationId}", token))
            .Content.ReadFromJsonAsync<RepairQuoteResponse[]>())!);
    }

    [Fact]
    public async Task Worn_out_says_so_and_is_destroyed()
    {
        (string token, int characterId) = await SignedInCharacterAsync("wornout@example.com");
        long laser = await BrokenLaserAsync(characterId, scrap: 5, max: 1100);

        RepairQuoteResponse quote = Assert.Single((await (await GetAsync(
            $"/industry/repairs?characterId={characterId}&stationId={_stationId}", token))
            .Content.ReadFromJsonAsync<RepairQuoteResponse[]>())!);
        Assert.True(quote.WornOut);
        Assert.Equal(0, quote.CreditsMinorUnits);

        HttpResponseMessage repaired = await PostAsync(
            "/industry/repairs", new { characterId, stationId = _stationId, itemInstanceId = laser }, token);
        string text = await repaired.Content.ReadAsStringAsync();
        _output.WriteLine($"POST /industry/repairs (worn out): {text}");

        Assert.True(JsonDocument.Parse(text).RootElement.GetProperty("destroyed").GetBoolean());

        InventoryResponse inventory = (await (await GetAsync($"/characters/{characterId}/inventory", token))
            .Content.ReadFromJsonAsync<InventoryResponse>())!;
        Assert.DoesNotContain(inventory.Items, i => i.Id == laser);
    }

    [Theory]
    [InlineData(false, 5, "not_docked")]
    [InlineData(true, 1, "insufficient_items")]
    public async Task A_refusal_says_why(bool docked, int scrap, string reason)
    {
        (string token, int characterId) = await SignedInCharacterAsync($"refused{scrap}@example.com");
        long laser = await BrokenLaserAsync(characterId, scrap, docked: docked);

        HttpResponseMessage response = await PostAsync(
            "/industry/repairs", new { characterId, stationId = _stationId, itemInstanceId = laser }, token);
        string text = await response.Content.ReadAsStringAsync();

        Assert.Equal(HttpStatusCode.Conflict, response.StatusCode);
        Assert.Equal(reason, JsonDocument.Parse(text).RootElement.GetProperty("reason").GetString());
    }

    [Fact]
    public async Task Another_accounts_character_is_not_found()
    {
        (_, int characterId) = await SignedInCharacterAsync("owner@example.com");
        long laser = await BrokenLaserAsync(characterId, scrap: 5);
        (string thief, _) = await SignedInCharacterAsync("thief@example.com");

        Assert.Equal(HttpStatusCode.NotFound, (await GetAsync(
            $"/industry/repairs?characterId={characterId}&stationId={_stationId}", thief)).StatusCode);
        Assert.Equal(HttpStatusCode.NotFound, (await PostAsync(
            "/industry/repairs", new { characterId, stationId = _stationId, itemInstanceId = laser }, thief)).StatusCode);
    }

    /// <summary>A broken mining laser in the character's pockets, scrap in this station's hangar, docked here.</summary>
    private async Task<long> BrokenLaserAsync(
        int characterId, int scrap, int max = Domain.Industry.Repairs.NewMaxHundredths, bool docked = true)
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();
        var inventories = new InventoryService(context);

        Character character = await context.Characters.SingleAsync(c => c.Id == characterId);
        character.DockedStationId = docked ? _stationId : null;
        character.Balance = Credits.FromWholeCredits(100);

        Inventory carried = await inventories.GetOrCreateCarriedAsync(characterId);
        Inventory hangar = await inventories.GetOrCreateStationHangarAsync(characterId, _stationId);
        await context.SaveChangesAsync();

        await inventories.AddAsync(hangar.Id, _scrapId, scrap, Credits.Zero);

        var laser = new ItemInstance
        {
            ItemDefId = _laserId,
            InventoryId = carried.Id,
            Condition = 0,
            MaxConditionHundredths = max,
            AcquisitionValue = Credits.Zero,
            CreatedAt = DateTimeOffset.UtcNow,
        };
        context.ItemInstances.Add(laser);
        await context.SaveChangesAsync();

        return laser.Id;
    }

    private async Task<(string Token, int CharacterId)> SignedInCharacterAsync(string email)
    {
        HttpResponseMessage registered = await _client.PostAsJsonAsync(
            new Uri("/accounts/register", UriKind.Relative),
            new { email, password = "a-sufficiently-long-password" });

        registered.EnsureSuccessStatusCode();

        string token = (await registered.Content.ReadFromJsonAsync<SessionPayload>())!.Token;
        string name = email[..email.IndexOf('@', StringComparison.Ordinal)];

        HttpResponseMessage created = await PostAsync("/characters/", new { name, race = Race.Humanoid }, token);
        created.EnsureSuccessStatusCode();

        return (token, (await created.Content.ReadFromJsonAsync<CharacterResponse>())!.Id);
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

    private async Task SeedAsync()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        var scrap = new ItemDef { Key = "scrap_alloy", Name = "Scrap Alloy", Category = ItemCategory.Raw, VolumeM3 = 0.1 };
        var laser = new ItemDef
        {
            Key = "crude_mining_laser", Name = "Crude Mining Laser", Category = ItemCategory.Tool, VolumeM3 = 2,
            EquipsAs = EquipKind.MiningTool,
        };
        var toolcrafting = new Skill { Key = "toolcrafting", Name = "Toolcrafting", Category = SkillCategory.Life };

        context.ItemDefs.AddRange(scrap, laser);
        context.Skills.Add(toolcrafting);
        await context.SaveChangesAsync();

        var recipe = new Recipe
        {
            Key = "craft_crude_mining_laser", OutputItemDefId = laser.Id, OutputQuantity = 1, SkillId = toolcrafting.Id,
            RequiredLevel = 1, JobSeconds = 30, XpPerRun = 300,
        };
        context.Recipes.Add(recipe);
        await context.SaveChangesAsync();

        context.RecipeInputs.Add(new RecipeInput { RecipeId = recipe.Id, ItemDefId = scrap.Id, Quantity = 8 });

        Body home = context.Bodies.OrderBy(b => b.Id).First();
        var station = new Station
        {
            Key = "station_test_hub", Name = "Test Hub", StarSystemId = home.StarSystemId, BodyId = home.Id,
            Kind = StationKind.TradingHub,
        };
        context.Stations.Add(station);
        await context.SaveChangesAsync();

        (_stationId, _scrapId, _laserId) = (station.Id, scrap.Id, laser.Id);
    }

    private sealed record SessionPayload(int AccountId, string Token, DateTimeOffset ExpiresAt);
}
