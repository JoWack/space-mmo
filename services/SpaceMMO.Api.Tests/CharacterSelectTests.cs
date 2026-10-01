using System.Net;
using System.Net.Http.Json;
using SpaceMMO.Api.Endpoints;
using SpaceMMO.Data;
using SpaceMMO.Data.Entities;
using SpaceMMO.Domain.Characters;
using SpaceMMO.Domain.Economy;
using SpaceMMO.Domain.Items;
using Xunit;

namespace SpaceMMO.Api.Tests;

/// <summary>
/// What the character select and new character screens read (task 110).
/// </summary>
/// <remarks>
/// The client parses both by hand, field by field, and a field it cannot find is left at its default
/// with no error anywhere. So the names are pinned here as text, from this end, and the client's
/// protocol tests pin them from the other.
/// </remarks>
[Collection(SharedApiDatabase.Name)]
public sealed class CharacterSelectTests(ApiDatabaseFixture fixture) : IAsyncLifetime, IDisposable
{
    private readonly ApiDatabaseFixture _fixture = fixture;

    private ApiFactory _factory = null!;
    private HttpClient _client = null!;

    private string _token = null!;
    private int _characterId;

    public async Task InitializeAsync()
    {
        await _fixture.ResetAsync();

        _factory = new ApiFactory(_fixture.ConnectionString, ApiFactory.TestServiceSecret);
        _client = _factory.CreateClient();

        await AuthorizationTests.SeedStartingWorldAsync(_fixture);

        HttpResponseMessage registered = await _client.PostAsJsonAsync(
            "/accounts/register",
            new { email = "selector@example.com", password = "a-sufficiently-long-password" });

        registered.EnsureSuccessStatusCode();

        SessionResponse session = (await registered.Content.ReadFromJsonAsync<SessionResponse>())!;

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        var character = new Character
        {
            AccountId = session.AccountId,
            Name = "Selector",
            Race = Race.Martian,
            HomeBodyId = context.Bodies.First().Id,
            CreatedAt = DateTimeOffset.UtcNow,
        };

        context.Characters.Add(character);
        await context.SaveChangesAsync();

        _token = session.Token;
        _characterId = character.Id;
    }

    public Task DisposeAsync() => Task.CompletedTask;

    public void Dispose()
    {
        _client?.Dispose();
        _factory?.Dispose();
    }

    [Fact]
    public async Task A_character_who_has_never_played_was_last_seen_nowhere()
    {
        CharacterResponse listed = await ListOneAsync();

        Assert.Null(listed.LastSeenWorld);
        Assert.False(listed.LastSeenFlying);
        Assert.Null(listed.LastSeenShip);
    }

    /// <summary>
    /// Nearest by surface, not by centre.
    /// </summary>
    /// <remarks>
    /// The positions are chosen so the two disagree: 100 km above a big world's surface is 300 km
    /// from a small moon's centre and 700 km from the world's. Measuring centres names the moon.
    /// </remarks>
    [Fact]
    public async Task Last_seen_names_the_world_whose_surface_was_nearest()
    {
        await using (SpaceMmoDbContext context = _fixture.CreateContext())
        {
            List<Body> bodies = [.. context.Bodies.OrderBy(b => b.Id)];

            bodies[0].Name = "Big World";
            bodies[0].RadiusKm = 600.0;
            bodies[0].SystemX = 0.0;
            bodies[0].SystemY = 0.0;
            bodies[0].SystemZ = 0.0;

            bodies[1].Name = "Small Moon";
            bodies[1].RadiusKm = 10.0;
            bodies[1].SystemX = 1000.0;
            bodies[1].SystemY = 0.0;
            bodies[1].SystemZ = 0.0;

            Character character = context.Characters.Single(c => c.Id == _characterId);

            character.LastSystemX = 700.0;
            character.LastSystemY = 0.0;
            character.LastSystemZ = 0.0;

            await context.SaveChangesAsync();
        }

        CharacterResponse listed = await ListOneAsync();

        Assert.Equal("Big World", listed.LastSeenWorld);
    }

    [Fact]
    public async Task Last_seen_names_the_ship_they_were_sitting_in()
    {
        await using (SpaceMmoDbContext context = _fixture.CreateContext())
        {
            var shuttle = new ItemDef
            {
                Key = "hull_shuttle",
                Name = "Shuttle",
                Category = ItemCategory.Hull,
                VolumeM3 = 200.0,
                HoldCapacityM3 = 80.0,
            };

            context.ItemDefs.Add(shuttle);
            await context.SaveChangesAsync();

            var hull = new ItemInstance
            {
                ItemDefId = shuttle.Id,
                Condition = 100,
                AcquisitionValue = Credits.Zero,
                CreatedAt = DateTimeOffset.UtcNow,
            };

            context.ItemInstances.Add(hull);
            await context.SaveChangesAsync();

            Character character = context.Characters.Single(c => c.Id == _characterId);

            character.AboardShipItemInstanceId = hull.Id;
            character.LastSeenFlying = true;

            await context.SaveChangesAsync();
        }

        CharacterResponse listed = await ListOneAsync();

        Assert.True(listed.LastSeenFlying);
        Assert.Equal("Shuttle", listed.LastSeenShip);
    }

    [Fact]
    public async Task The_character_list_field_names_are_the_ones_the_game_client_parses()
    {
        string json = await GetTextAsync("/characters/", _token);

        Assert.Contains("\"lastSeenWorld\":", json);
        Assert.Contains("\"lastSeenFlying\":", json);
        Assert.Contains("\"lastSeenShip\":", json);
    }

    [Fact]
    public async Task Every_race_is_served_once_with_its_faction_and_home_named()
    {
        // No token: what the races are is not a secret, and the creation screen is shown to an
        // account with no character yet.
        RaceResponse[] races = (await _client.GetFromJsonAsync<RaceResponse[]>("/world/races"))!;

        Assert.Equal(Enum.GetValues<Race>().Order(), races.Select(r => r.Race).Order());

        RaceResponse orc = races.Single(r => r.Race == Race.SpaceOrc);

        Assert.Equal("Space Orc", orc.Name);
        Assert.Equal(Faction.B, orc.Faction);
        Assert.Equal("Tusk and Thorn", orc.FactionName);
        Assert.Equal("body_grimhold", orc.HomeBodyKey);

        // The seeded test world names each body after its race, so this is the body's name as
        // stored, joined by key -- not a name the endpoint made up.
        Assert.Equal(nameof(Race.SpaceOrc), orc.HomeBodyName);
    }

    [Fact]
    public async Task The_race_field_names_are_the_ones_the_game_client_parses()
    {
        string json = await GetTextAsync("/world/races", token: null);

        Assert.Contains("\"race\":", json);
        Assert.Contains("\"name\":", json);
        Assert.Contains("\"faction\":", json);
        Assert.Contains("\"factionName\":", json);
        Assert.Contains("\"homeBodyKey\":", json);
        Assert.Contains("\"homeBodyName\":", json);
    }

    /// <summary>
    /// The new character screen shows the server's refusal verbatim, so its words are on the wire.
    /// </summary>
    /// <remarks>
    /// A validation problem's title is "One or more validation errors occurred.", and the sentence a
    /// player needs is under <c>errors.name</c>. The client reads that path; this pins it, and the
    /// client's protocol test parses this exact body.
    /// </remarks>
    [Fact]
    public async Task A_refused_name_says_why_where_the_client_reads_it()
    {
        using var request = new HttpRequestMessage(HttpMethod.Post, "/characters/")
        {
            Content = JsonContent.Create(new { name = "Al", race = Race.Martian }),
        };

        request.Headers.Add("Authorization", $"Bearer {_token}");

        HttpResponseMessage response = await _client.SendAsync(request);

        Assert.Equal(HttpStatusCode.BadRequest, response.StatusCode);

        string json = await response.Content.ReadAsStringAsync();

        Assert.Contains(
            "\"errors\":{\"name\":[\"Name must be between 3 and 20 characters.\"]}", json);
    }

    private async Task<CharacterResponse> ListOneAsync()
    {
        using var request = new HttpRequestMessage(HttpMethod.Get, "/characters/");

        request.Headers.Add("Authorization", $"Bearer {_token}");

        HttpResponseMessage response = await _client.SendAsync(request);

        response.EnsureSuccessStatusCode();

        CharacterResponse[] characters =
            (await response.Content.ReadFromJsonAsync<CharacterResponse[]>())!;

        return Assert.Single(characters);
    }

    private async Task<string> GetTextAsync(string path, string? token)
    {
        using var request = new HttpRequestMessage(HttpMethod.Get, path);

        if (token is not null)
        {
            request.Headers.Add("Authorization", $"Bearer {token}");
        }

        HttpResponseMessage response = await _client.SendAsync(request);

        Assert.Equal(HttpStatusCode.OK, response.StatusCode);

        return await response.Content.ReadAsStringAsync();
    }
}
