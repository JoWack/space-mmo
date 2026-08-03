using System.Net;
using System.Net.Http.Json;
using SpaceMMO.Api.Endpoints;
using SpaceMMO.Data;
using SpaceMMO.Data.Entities;
using SpaceMMO.Domain.Characters;
using Xunit;

namespace SpaceMMO.Api.Tests;

/// <summary>
/// Binding a connection to a character.
/// </summary>
/// <remarks>
/// The seam that decides whose ore is whose. Until now the game server was told which character to
/// credit on the command line, which is fine for one player and wrong for two. From here it is
/// proven: a client claims a character, and the claim only stands if the session token it presents
/// really belongs to the account that owns it.
///
/// These tests matter for the same reason AuthorizationTests do. If this check is weakened, any
/// player can join and play as any character in the game — inventory, wealth and all — and no unit
/// test of a service would notice, because every service would be behaving correctly when asked.
/// </remarks>
[Collection(SharedApiDatabase.Name)]
public sealed class ResolveCharacterTests(ApiDatabaseFixture fixture) : IAsyncLifetime, IDisposable
{
    private readonly ApiDatabaseFixture _fixture = fixture;

    private ApiFactory _factory = null!;
    private HttpClient _client = null!;

    private string _ownerToken = null!;
    private int _ownedCharacterId;
    private int _strangersCharacterId;

    public async Task InitializeAsync()
    {
        await _fixture.ResetAsync();

        _factory = new ApiFactory(_fixture.ConnectionString, ApiFactory.TestServiceSecret);
        _client = _factory.CreateClient();

        await AuthorizationTests.SeedStartingWorldAsync(_fixture);

        (_ownerToken, _ownedCharacterId) = await CreatePlayerAsync("owner@example.com", "Owner");
        (_, _strangersCharacterId) = await CreatePlayerAsync("stranger@example.com", "Stranger");
    }

    public Task DisposeAsync() => Task.CompletedTask;

    public void Dispose()
    {
        _client?.Dispose();
        _factory?.Dispose();
    }

    [Fact]
    public async Task A_valid_token_resolves_its_own_character()
    {
        HttpResponseMessage response =
            await ResolveAsync(ApiFactory.TestServiceSecret, _ownerToken, _ownedCharacterId);

        Assert.Equal(HttpStatusCode.OK, response.StatusCode);

        ResolvedCharacter resolved =
            (await response.Content.ReadFromJsonAsync<ResolvedCharacter>())!;

        Assert.Equal(_ownedCharacterId, resolved.CharacterId);
        Assert.Equal("Owner", resolved.CharacterName);
    }

    /// <summary>
    /// The whole point: a real token does not let you claim somebody else's character.
    /// </summary>
    /// <remarks>
    /// This is the attack the endpoint exists to stop. Character ids are sequential integers, so a
    /// logged-in player who could name any of them would be able to join as the wealthiest
    /// character on the server and spend its inventory.
    /// </remarks>
    [Fact]
    public async Task A_valid_token_cannot_claim_another_accounts_character()
    {
        HttpResponseMessage response =
            await ResolveAsync(ApiFactory.TestServiceSecret, _ownerToken, _strangersCharacterId);

        Assert.Equal(HttpStatusCode.NotFound, response.StatusCode);
    }

    [Fact]
    public async Task A_forged_token_is_rejected()
    {
        HttpResponseMessage response = await ResolveAsync(
            ApiFactory.TestServiceSecret, "not-a-real-token", _ownedCharacterId);

        Assert.Equal(HttpStatusCode.Unauthorized, response.StatusCode);
    }

    [Fact]
    public async Task A_tampered_token_is_rejected()
    {
        // One character changed in the middle. The signature must fail before any payload in it is
        // believed, which is what stops an account id being edited upward.
        char[] characters = _ownerToken.ToCharArray();
        characters[^3] = characters[^3] == 'A' ? 'B' : 'A';

        HttpResponseMessage response = await ResolveAsync(
            ApiFactory.TestServiceSecret, new string(characters), _ownedCharacterId);

        Assert.Equal(HttpStatusCode.Unauthorized, response.StatusCode);
    }

    [Fact]
    public async Task Without_the_service_credential_it_answers_nothing()
    {
        // A player holding a perfectly good token still may not ask this question. It is the game
        // server's, and opening it up would make it a quiet way to test whether a stolen token is
        // still live.
        HttpResponseMessage response =
            await ResolveAsync(presentedSecret: null, _ownerToken, _ownedCharacterId);

        Assert.Equal(HttpStatusCode.Unauthorized, response.StatusCode);
    }

    [Fact]
    public async Task A_wrong_service_credential_answers_nothing()
    {
        HttpResponseMessage response =
            await ResolveAsync("not-the-secret", _ownerToken, _ownedCharacterId);

        Assert.Equal(HttpStatusCode.Unauthorized, response.StatusCode);
    }

    private async Task<HttpResponseMessage> ResolveAsync(
        string? presentedSecret, string token, int characterId)
    {
        var request = new HttpRequestMessage(HttpMethod.Post, "/accounts/resolve-character")
        {
            Content = JsonContent.Create(new { token, characterId }),
        };

        if (presentedSecret is not null)
        {
            request.Headers.Add("X-SpaceMMO-Service", presentedSecret);
        }

        return await _client.SendAsync(request);
    }

    private async Task<(string Token, int CharacterId)> CreatePlayerAsync(
        string email, string name)
    {
        HttpResponseMessage registered = await _client.PostAsJsonAsync(
            "/accounts/register",
            new { email, password = "a-sufficiently-long-password" });

        registered.EnsureSuccessStatusCode();

        SessionResponse session =
            (await registered.Content.ReadFromJsonAsync<SessionResponse>())!;

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        Body home = context.Bodies.First();

        var character = new Character
        {
            AccountId = session.AccountId,
            Name = name,
            Race = Race.Humanoid,
            HomeBodyId = home.Id,
            CreatedAt = DateTimeOffset.UtcNow,
        };

        context.Characters.Add(character);
        await context.SaveChangesAsync();

        return (session.Token, character.Id);
    }
}
