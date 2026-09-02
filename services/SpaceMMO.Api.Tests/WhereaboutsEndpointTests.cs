using System.Net;
using System.Net.Http.Json;
using System.Text;
using SpaceMMO.Api.Endpoints;
using SpaceMMO.Data;
using SpaceMMO.Data.Entities;
using SpaceMMO.Domain.Characters;
using Xunit;

namespace SpaceMMO.Api.Tests;

/// <summary>
/// Recording where a player was, and handing it back when they sign in (task 147).
/// </summary>
/// <remarks>
/// <para>
/// The authorisation test here is the one that carries the design. "Wherever you are when you quit
/// is where you come back" is only safe because a player cannot choose the answer: if a client
/// could write this column it could write any position into it, and coming back would be a teleport
/// to anywhere in the game.
/// </para>
/// <para>
/// The round trip goes through <c>/accounts/resolve-character</c> rather than the read endpoint,
/// because that is the request the game server actually makes when somebody connects. A test that
/// wrote here and read from the tidier endpoint would prove the column works and leave the wire the
/// game uses untested.
/// </para>
/// </remarks>
[Collection(SharedApiDatabase.Name)]
public sealed class WhereaboutsEndpointTests(ApiDatabaseFixture fixture) : IAsyncLifetime, IDisposable
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
            new { email = "traveller@example.com", password = "a-sufficiently-long-password" });

        registered.EnsureSuccessStatusCode();

        SessionResponse session = (await registered.Content.ReadFromJsonAsync<SessionResponse>())!;

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        Body home = context.Bodies.First();

        var character = new Character
        {
            AccountId = session.AccountId,
            Name = "Traveller",
            Race = Race.Humanoid,
            HomeBodyId = home.Id,
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
    public async Task Signing_in_gets_back_the_place_the_world_last_saw_you()
    {
        HttpResponseMessage recorded = await RecordAsync(
            ApiFactory.TestServiceSecret, 640.5, -12.25, 3.75, flying: true);

        Assert.Equal(HttpStatusCode.OK, recorded.StatusCode);

        ResolvedCharacter resolved = await ResolveAsync();

        Assert.Equal(640.5, resolved.LastSystemX);
        Assert.Equal(-12.25, resolved.LastSystemY);
        Assert.Equal(3.75, resolved.LastSystemZ);
        Assert.True(resolved.LastSeenFlying);
    }

    /// <summary>
    /// A player cannot say where they were, which is the whole of "no free teleports".
    /// </summary>
    /// <remarks>
    /// The refusal is what makes restoring a position safe rather than exploitable. Somebody who
    /// could write this would quit, name a place across the system, and sign back in there.
    /// </remarks>
    [Fact]
    public async Task A_player_cannot_write_their_own_position()
    {
        var request = new HttpRequestMessage(HttpMethod.Post, "/whereabouts")
        {
            Content = JsonContent.Create(
                new { characterId = _characterId, x = 1.0, y = 2.0, z = 3.0, flying = false }),
        };

        // Their own valid session token, on their own character. Still refused: this is not a
        // question of who you are, but of which party is entitled to state a position.
        request.Headers.Add("Authorization", $"Bearer {_token}");

        HttpResponseMessage response = await _client.SendAsync(request);

        Assert.Equal(HttpStatusCode.Unauthorized, response.StatusCode);

        // And nothing was written, so the refusal is not merely an unhelpful status code on a
        // change that happened anyway.
        ResolvedCharacter resolved = await ResolveAsync();

        Assert.Null(resolved.LastSystemX);
    }

    [Fact]
    public async Task A_character_who_has_never_been_anywhere_comes_back_with_no_position()
    {
        // Nulls rather than zeroes, all the way out to the wire. The game server reads this as
        // "send them to a starting point"; a zero would read as the centre of the star system.
        ResolvedCharacter resolved = await ResolveAsync();

        Assert.Null(resolved.LastSystemX);
        Assert.Null(resolved.LastSystemY);
        Assert.Null(resolved.LastSystemZ);
        Assert.False(resolved.LastSeenFlying);
    }

    [Fact]
    public async Task A_position_that_is_not_a_position_never_reaches_the_column()
    {
        // Sent as raw text, because it cannot be sent any other way: System.Text.Json refuses to
        // write NaN or an infinity as a JSON number at all, so a game server with a NaN in it
        // cannot produce a body this endpoint would parse. The wire rejects it before the service
        // guard is reached -- and the guard stays, because WhereaboutsService is also called in
        // process, where there is no JSON writer standing in the way.
        //
        // The status is asserted as "not a success" rather than as 400, which is what it ought to
        // be: model binding throws on the malformed body and this host is not in Development, so
        // the framework answers 500. That is true of every endpoint in the API and is not this
        // one's to fix; what belongs here is that the column is untouched.
        var request = new HttpRequestMessage(HttpMethod.Post, "/whereabouts")
        {
            Content = new StringContent(
                $$"""{"characterId":{{_characterId}},"x":NaN,"y":0,"z":0,"flying":false}""",
                Encoding.UTF8,
                "application/json"),
        };

        request.Headers.Add("X-SpaceMMO-Service", ApiFactory.TestServiceSecret);

        HttpResponseMessage response = await _client.SendAsync(request);

        Assert.False(
            response.IsSuccessStatusCode,
            $"NaN should not be accepted; got {(int)response.StatusCode}");

        // The half that matters: nothing was stored. A refusal that wrote the row anyway would
        // only be discovered by the player it put nowhere.
        ResolvedCharacter resolved = await ResolveAsync();

        Assert.Null(resolved.LastSystemX);
    }

    /// <summary>
    /// The names on the wire, pinned from this end.
    /// </summary>
    /// <remarks>
    /// The game client parses these by hand, field by field, and a parser that finds nothing
    /// leaves every field at its default -- which reads as "this character has never been
    /// anywhere" and puts them back at a spawn. There is no error in that path on either side, so
    /// a rename here would be found in a playtest rather than in a build.
    /// SpaceMMO.Whereabouts.Parse asserts the same names from the other end.
    /// </remarks>
    [Fact]
    public async Task The_field_names_are_the_ones_the_game_client_parses()
    {
        await RecordAsync(ApiFactory.TestServiceSecret, 1.5, 2.5, 3.5, flying: true);

        var request = new HttpRequestMessage(HttpMethod.Post, "/accounts/resolve-character")
        {
            Content = JsonContent.Create(new { token = _token, characterId = _characterId }),
        };

        request.Headers.Add("X-SpaceMMO-Service", ApiFactory.TestServiceSecret);

        HttpResponseMessage response = await _client.SendAsync(request);

        response.EnsureSuccessStatusCode();

        string json = await response.Content.ReadAsStringAsync();

        Assert.Contains("\"lastSystemX\":", json);
        Assert.Contains("\"lastSystemY\":", json);
        Assert.Contains("\"lastSystemZ\":", json);
        Assert.Contains("\"lastSeenFlying\":", json);
    }

    [Fact]
    public async Task An_unknown_character_is_not_found()
    {
        HttpResponseMessage response = await RecordAsync(
            ApiFactory.TestServiceSecret, 1.0, 1.0, 1.0, flying: false, characterId: 999_999);

        Assert.Equal(HttpStatusCode.NotFound, response.StatusCode);
    }

    private async Task<HttpResponseMessage> RecordAsync(
        string? presentedSecret,
        double x,
        double y,
        double z,
        bool flying,
        int? characterId = null)
    {
        var request = new HttpRequestMessage(HttpMethod.Post, "/whereabouts")
        {
            Content = JsonContent.Create(
                new { characterId = characterId ?? _characterId, x, y, z, flying }),
        };

        if (presentedSecret is not null)
        {
            request.Headers.Add("X-SpaceMMO-Service", presentedSecret);
        }

        return await _client.SendAsync(request);
    }

    private async Task<ResolvedCharacter> ResolveAsync()
    {
        var request = new HttpRequestMessage(HttpMethod.Post, "/accounts/resolve-character")
        {
            Content = JsonContent.Create(new { token = _token, characterId = _characterId }),
        };

        request.Headers.Add("X-SpaceMMO-Service", ApiFactory.TestServiceSecret);

        HttpResponseMessage response = await _client.SendAsync(request);

        response.EnsureSuccessStatusCode();

        return (await response.Content.ReadFromJsonAsync<ResolvedCharacter>())!;
    }
}
