using System.Net;
using System.Net.Http.Json;
using Microsoft.EntityFrameworkCore;
using SpaceMMO.Data;
using SpaceMMO.Data.Entities;
using SpaceMMO.Domain.Characters;
using SpaceMMO.Domain.Universe;
using Xunit;

namespace SpaceMMO.Api.Tests;

/// <summary>
/// The game server's credential, and the ways it must not work.
/// </summary>
/// <remarks>
/// This credential lets one caller act for a character it does not own, which is exactly the thing
/// the rest of the authorization code exists to prevent. It is justified — only the simulation knows
/// whether a player is standing next to a deposit, and that machine holds no player's token — but it
/// means the checks around it carry the same weight as the ownership checks themselves. A leaked or
/// bypassed service secret is an unbounded material faucet in a player-driven economy.
/// </remarks>
[Collection(SharedApiDatabase.Name)]
public sealed class ServiceCredentialTests(ApiDatabaseFixture fixture) : IAsyncLifetime, IDisposable
{
    private readonly ApiDatabaseFixture _fixture = fixture;

    private readonly List<IDisposable> _disposables = [];

    private int _characterId;
    private long _nodeId;
    private int _stationId;

    public async Task InitializeAsync()
    {
        await _fixture.ResetAsync();

        await AuthorizationTests.SeedStartingWorldAsync(_fixture);

        _characterId = await SeedCharacterAsync();

        await SeedDepositAsync();
    }

    public Task DisposeAsync() => Task.CompletedTask;

    public void Dispose()
    {
        foreach (IDisposable disposable in _disposables)
        {
            disposable.Dispose();
        }
    }

    /// <summary>
    /// With no secret configured, the header grants nothing.
    /// </summary>
    /// <remarks>
    /// The single most important test here. A credential check that treats "no secret configured"
    /// as "everything matches" turns a forgotten environment variable into an open door, and it
    /// would pass every other test in this file — because every other test sets a secret.
    /// </remarks>
    [Fact]
    public async Task An_unconfigured_service_secret_rejects_every_service_call()
    {
        HttpClient client = CreateClient(serviceSecret: null);

        HttpResponseMessage response = await GatherAsync(client, "anything-at-all");

        Assert.Equal(HttpStatusCode.Unauthorized, response.StatusCode);
    }

    [Fact]
    public async Task A_wrong_service_secret_is_rejected()
    {
        HttpClient client = CreateClient(ApiFactory.TestServiceSecret);

        HttpResponseMessage response = await GatherAsync(client, "not-the-real-secret");

        Assert.Equal(HttpStatusCode.Unauthorized, response.StatusCode);
    }

    [Fact]
    public async Task A_secret_that_is_merely_a_prefix_is_rejected()
    {
        HttpClient client = CreateClient(ApiFactory.TestServiceSecret);

        // Guards the comparison itself. A length-insensitive or short-circuiting compare would
        // accept this, and it is the first thing anyone probing the header would try.
        HttpResponseMessage response =
            await GatherAsync(client, ApiFactory.TestServiceSecret[..10]);

        Assert.Equal(HttpStatusCode.Unauthorized, response.StatusCode);
    }

    [Fact]
    public async Task The_game_server_may_gather_for_a_character_it_does_not_own()
    {
        HttpClient client = CreateClient(ApiFactory.TestServiceSecret);

        // No bearer token at all. The game server owns no account and never will.
        HttpResponseMessage response =
            await GatherAsync(client, ApiFactory.TestServiceSecret);

        // The point is that authorization passed. Whether this particular gather yielded anything
        // is the GatheringService's business and is tested against it directly.
        Assert.Equal(HttpStatusCode.OK, response.StatusCode);
    }

    [Fact]
    public async Task A_service_call_for_a_character_that_does_not_exist_reports_not_found()
    {
        HttpClient client = CreateClient(ApiFactory.TestServiceSecret);

        HttpResponseMessage response = await GatherAsync(
            client, ApiFactory.TestServiceSecret, characterId: 999999);

        // Not an unhandled failure, and not a silent success. The credential says who is asking,
        // never that the thing asked about exists.
        Assert.Equal(HttpStatusCode.NotFound, response.StatusCode);
    }

    [Fact]
    public async Task A_player_without_the_credential_still_cannot_reach_another_character()
    {
        HttpClient client = CreateClient(ApiFactory.TestServiceSecret);

        // A logged-in player, sending no service header. Adding the service path must not have
        // weakened the ordinary ownership check that runs when it is absent.
        string token = await RegisterAsync(client, "outsider@example.com");

        var request = new HttpRequestMessage(HttpMethod.Post, "/gathering/gather")
        {
            Content = JsonContent.Create(
                new { characterId = _characterId, resourceNodeId = 1L, stationId = 1 }),
        };

        request.Headers.Add("Authorization", $"Bearer {token}");

        HttpResponseMessage response = await client.SendAsync(request);

        Assert.Equal(HttpStatusCode.NotFound, response.StatusCode);
    }

    private HttpClient CreateClient(string? serviceSecret)
    {
        var factory = new ApiFactory(_fixture.ConnectionString, serviceSecret);

        _disposables.Add(factory);

        HttpClient client = factory.CreateClient();

        _disposables.Add(client);

        return client;
    }

    private async Task<HttpResponseMessage> GatherAsync(
        HttpClient client, string presentedSecret, int? characterId = null)
    {
        var request = new HttpRequestMessage(HttpMethod.Post, "/gathering/gather")
        {
            Content = JsonContent.Create(new
            {
                characterId = characterId ?? _characterId,
                resourceNodeId = _nodeId,
                stationId = _stationId,
            }),
        };

        request.Headers.Add("X-SpaceMMO-Service", presentedSecret);

        return await client.SendAsync(request);
    }

    private static async Task<string> RegisterAsync(HttpClient client, string email)
    {
        HttpResponseMessage response = await client.PostAsJsonAsync(
            "/accounts/register", new { email, password = "a-sufficiently-long-password" });

        response.EnsureSuccessStatusCode();

        System.Text.Json.JsonDocument body =
            await response.Content.ReadFromJsonAsync<System.Text.Json.JsonDocument>()
            ?? throw new InvalidOperationException("No body.");

        return body.RootElement.GetProperty("token").GetString()!;
    }

    /// <summary>
    /// A real, hand-gatherable deposit, so the success test proves gathering rather than a 404.
    /// </summary>
    private async Task SeedDepositAsync()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        Body body = context.Bodies.First();

        var item = new ItemDef
        {
            Key = "ferrite_ore",
            Name = "Ferrite Ore",
            Category = SpaceMMO.Domain.Items.ItemCategory.Raw,
            VolumeM3 = 0.1,
        };

        var skill = new Skill
        {
            Key = "mining",
            Name = "Mining",
            Category = SpaceMMO.Domain.Progression.SkillCategory.Life,
        };

        var station = new Station
        {
            Key = "station_test",
            Name = "Test Hub",
            StarSystemId = body.StarSystemId,
            BodyId = body.Id,
            Kind = StationKind.TradingHub,
        };

        context.ItemDefs.Add(item);
        context.Skills.Add(skill);
        context.Stations.Add(station);
        await context.SaveChangesAsync();

        var node = new ResourceNode
        {
            Key = "node_service_test",
            StarSystemId = body.StarSystemId,
            BodyId = body.Id,
            ItemDefId = item.Id,
            SkillId = skill.Id,

            // Level 1 and no tool, so a brand-new character can work it. Anything stricter would
            // make this test fail for a reason that has nothing to do with the credential.
            RequiredLevel = 1,
            RequiredToolItemDefId = null,
            QuantityMax = 200,
            RespawnSeconds = 1_200,
            DirectionX = 1.0,
            SharingModel = SpaceMMO.Domain.Gathering.NodeSharingModel.Shared,
        };

        context.ResourceNodes.Add(node);
        await context.SaveChangesAsync();

        _nodeId = node.Id;
        _stationId = station.Id;
    }

    private async Task<int> SeedCharacterAsync()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        var account = new Account
        {
            Email = "owner@example.com",
            PasswordHash = "x",
        };

        context.Accounts.Add(account);
        await context.SaveChangesAsync();

        Body home = context.Bodies.First();

        var character = new Character
        {
            AccountId = account.Id,
            Name = "Miner",
            Race = Race.Humanoid,
            HomeBodyId = home.Id,
            CreatedAt = DateTimeOffset.UtcNow,
        };

        context.Characters.Add(character);
        await context.SaveChangesAsync();

        return character.Id;
    }
}
