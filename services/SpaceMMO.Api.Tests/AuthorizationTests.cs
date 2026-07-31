using System.Net;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using Microsoft.EntityFrameworkCore;
using SpaceMMO.Data;
using SpaceMMO.Data.Entities;
using SpaceMMO.Domain.Characters;
using SpaceMMO.Domain.Universe;
using Xunit;

namespace SpaceMMO.Api.Tests;

/// <summary>
/// Whether one account can reach another account's character.
/// </summary>
/// <remarks>
/// The most important tests in this project. Every gameplay endpoint takes a character id from
/// the request body, and a character id is trivially guessable — they are sequential integers. If
/// the ownership check is ever dropped from one endpoint, any logged-in player can gather with,
/// spend from, or sell the inventory of any character in the game, and no unit test of a service
/// would notice, because the services are behaving correctly when asked.
/// </remarks>
[Collection(SharedApiDatabase.Name)]
public sealed class AuthorizationTests(ApiDatabaseFixture fixture) : IAsyncLifetime, IDisposable
{
    private readonly ApiDatabaseFixture _fixture = fixture;

    private ApiFactory _factory = null!;
    private HttpClient _client = null!;

    private int _victimCharacterId;

    public async Task InitializeAsync()
    {
        await _fixture.ResetAsync();

        _factory = new ApiFactory(_fixture.ConnectionString);
        _client = _factory.CreateClient();

        await SeedStartingWorldAsync(_fixture);

        // A character belonging to somebody else entirely.
        _victimCharacterId = await CreateForeignCharacterAsync();
    }

    public Task DisposeAsync() => Task.CompletedTask;

    /// <summary>
    /// Disposes the client and host.
    /// </summary>
    /// <remarks>
    /// IDisposable rather than only xUnit's DisposeAsync, because CA1001 is right: a type holding
    /// an HttpClient and a web host owns them, and the analyzer cannot see that xUnit would have
    /// called the async one.
    /// </remarks>
    public void Dispose()
    {
        _client?.Dispose();
        _factory?.Dispose();
    }

    [Fact]
    public async Task Reading_another_accounts_skills_reports_not_found()
    {
        string token = await RegisterAsync("attacker@example.com");

        HttpResponseMessage response =
            await GetAsync($"/characters/{_victimCharacterId}/skills", token);

        // Not Forbidden. Telling the caller "that exists but is not yours" turns the endpoint
        // into an oracle for which character ids are real.
        Assert.Equal(HttpStatusCode.NotFound, response.StatusCode);
    }

    [Fact]
    public async Task Reading_another_accounts_inventory_reports_not_found()
    {
        string token = await RegisterAsync("attacker2@example.com");

        HttpResponseMessage response =
            await GetAsync($"/characters/{_victimCharacterId}/inventory", token);

        Assert.Equal(HttpStatusCode.NotFound, response.StatusCode);
    }

    [Fact]
    public async Task Gathering_with_another_accounts_character_reports_not_found()
    {
        string token = await RegisterAsync("attacker3@example.com");

        HttpResponseMessage response = await PostAsync(
            "/gathering/gather",
            new { characterId = _victimCharacterId, resourceNodeId = 1L, stationId = 1 },
            token);

        Assert.Equal(HttpStatusCode.NotFound, response.StatusCode);
    }

    [Fact]
    public async Task Placing_an_order_as_another_accounts_character_reports_not_found()
    {
        string token = await RegisterAsync("attacker4@example.com");

        HttpResponseMessage response = await PostAsync(
            "/market/orders",
            new
            {
                characterId = _victimCharacterId,
                stationId = 1,
                itemDefId = 1,
                side = 1,
                limitPriceMinorUnits = 100L,
                quantity = 1,
            },
            token);

        Assert.Equal(HttpStatusCode.NotFound, response.StatusCode);
    }

    [Fact]
    public async Task Starting_a_job_as_another_accounts_character_reports_not_found()
    {
        string token = await RegisterAsync("attacker5@example.com");

        HttpResponseMessage response = await PostAsync(
            "/industry/jobs",
            new { characterId = _victimCharacterId, recipeId = 1, stationId = 1, runs = 1 },
            token);

        Assert.Equal(HttpStatusCode.NotFound, response.StatusCode);
    }

    [Fact]
    public async Task Accepting_a_quest_as_another_accounts_character_reports_not_found()
    {
        string token = await RegisterAsync("attacker6@example.com");

        HttpResponseMessage response = await PostAsync(
            "/quests/accept",
            new { characterId = _victimCharacterId, questKey = "quest_first_steps" },
            token);

        Assert.Equal(HttpStatusCode.NotFound, response.StatusCode);
    }

    [Fact]
    public async Task Every_gameplay_endpoint_refuses_an_unauthenticated_caller()
    {
        // Without a token at all, rather than with somebody else's. A missing check would show up
        // here as a 404 or a 200 instead of a 401.
        Assert.Equal(
            HttpStatusCode.Unauthorized,
            (await _client.GetAsync(new Uri($"/characters/{_victimCharacterId}/skills", UriKind.Relative))).StatusCode);

        Assert.Equal(
            HttpStatusCode.Unauthorized,
            (await _client.PostAsJsonAsync(
                new Uri("/gathering/gather", UriKind.Relative),
                new { characterId = _victimCharacterId, resourceNodeId = 1L, stationId = 1 })).StatusCode);

        Assert.Equal(
            HttpStatusCode.Unauthorized,
            (await _client.PostAsJsonAsync(
                new Uri("/characters/", UriKind.Relative),
                new { name = "Nobody", race = Race.Humanoid })).StatusCode);
    }

    [Fact]
    public async Task A_forged_token_is_rejected()
    {
        string token = await RegisterAsync("victim-of-forgery@example.com");

        // Flip the signature. The payload still parses and still names a real account, so this
        // only fails if the signature is genuinely verified.
        string[] parts = token.Split('.');
        string forged = $"{parts[0]}.{new string(parts[1].Reverse().ToArray())}";

        HttpResponseMessage response = await GetAsync("/characters/", forged);

        Assert.Equal(HttpStatusCode.Unauthorized, response.StatusCode);
    }

    private async Task<string> RegisterAsync(string email)
    {
        HttpResponseMessage response = await _client.PostAsJsonAsync(
            new Uri("/accounts/register", UriKind.Relative),
            new { email, password = "a-sufficiently-long-password" });

        response.EnsureSuccessStatusCode();

        SessionPayload? session = await response.Content.ReadFromJsonAsync<SessionPayload>();

        return session!.Token;
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

    /// <summary>Creates a character owned by an account the test client never authenticates as.</summary>
    private async Task<int> CreateForeignCharacterAsync()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        var account = new Account
        {
            Email = "victim@example.com",
            PasswordHash = "not-a-real-hash",
            CreatedAt = DateTimeOffset.UtcNow,
        };

        context.Accounts.Add(account);
        await context.SaveChangesAsync();

        Body home = await context.Bodies.FirstAsync(b => b.Key == "body_terra");

        var character = new Character
        {
            AccountId = account.Id,
            Name = "Victim",
            Race = Race.Humanoid,
            HomeBodyId = home.Id,
            CreatedAt = DateTimeOffset.UtcNow,
        };

        context.Characters.Add(character);
        await context.SaveChangesAsync();

        return character.Id;
    }

    /// <summary>
    /// Seeds the four starting bodies, so character creation has somewhere to put a character.
    /// </summary>
    internal static async Task SeedStartingWorldAsync(ApiDatabaseFixture fixture)
    {
        await using SpaceMmoDbContext context = fixture.CreateContext();

        var system = new StarSystem
        {
            Key = "system_origin",
            Name = "Origin",
            Seed = 42,
            GeneratorVersion = 1,
            SecurityLevel = SecurityLevel.Secure,
        };

        context.StarSystems.Add(system);
        await context.SaveChangesAsync();

        foreach (Race race in Enum.GetValues<Race>())
        {
            context.Bodies.Add(new Body
            {
                Key = Races.HomeBodyKeyFor(race),
                Name = race.ToString(),
                StarSystemId = system.Id,
                Kind = BodyKind.Planet,
                SecurityLevel = SecurityLevel.Secure,
                RadiusKm = 637.1,
            });
        }

        await context.SaveChangesAsync();
    }

    private sealed record SessionPayload(int AccountId, string Token, DateTimeOffset ExpiresAt);
}
