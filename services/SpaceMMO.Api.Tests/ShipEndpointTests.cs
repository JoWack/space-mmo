using System.Net;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using SpaceMMO.Data;
using SpaceMMO.Data.Entities;
using SpaceMMO.Domain.Characters;
using SpaceMMO.Domain.Economy;
using SpaceMMO.Domain.Items;
using SpaceMMO.Domain.Universe;
using Xunit;

namespace SpaceMMO.Api.Tests;

/// <summary>
/// Summoning a ship, over the wire (ADR-0012).
/// </summary>
/// <remarks>
/// <para>
/// The service tests cover the rules; these cover the things only the wire can get wrong — the
/// status a refusal comes back as, whether a player's own token is enough, and whether "you have no
/// ship here" arrives as an answer or as an error.
/// </para>
/// <para>
/// A player's own token rather than the service credential, unlike docking: docking records where a
/// ship is and only the simulation knows that, while every fact summoning depends on is a row the
/// server checks for itself.
/// </para>
/// </remarks>
[Collection(SharedApiDatabase.Name)]
public sealed class ShipEndpointTests(ApiDatabaseFixture fixture) : IAsyncLifetime, IDisposable
{
    private readonly ApiDatabaseFixture _fixture = fixture;

    private ApiFactory _factory = null!;
    private HttpClient _client = null!;

    private int _characterId;
    private int _spaceportId;
    private int _marketId;
    private long _hullId;
    private string _token = string.Empty;

    public async Task InitializeAsync()
    {
        await _fixture.ResetAsync();

        _factory = new ApiFactory(_fixture.ConnectionString, ApiFactory.TestServiceSecret);
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

    private sealed record HoldPayload(long? HoldInventoryId, double CapacityM3);

    /// <summary>Its own copy, like every other test class here: the API does not publish one.</summary>
    private sealed record SessionPayload(int AccountId, string Token, DateTimeOffset ExpiresAt);

    private async Task<HttpResponseMessage> SummonAsync(long hullId)
    {
        var request = new HttpRequestMessage(
            HttpMethod.Post, new Uri("/ships/summon", UriKind.Relative))
        {
            Content = JsonContent.Create(
                new { characterId = _characterId, hullItemInstanceId = hullId }),
        };

        request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", _token);

        return await _client.SendAsync(request);
    }

    private async Task<HttpResponseMessage> HoldAsync()
    {
        var request = new HttpRequestMessage(
            HttpMethod.Get, new Uri($"/ships/{_characterId}/hold", UriKind.Relative));

        request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", _token);

        return await _client.SendAsync(request);
    }

    private async Task DockAsync(int? stationId)
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        Character character = context.Characters.Single(c => c.Id == _characterId);
        character.DockedStationId = stationId;

        await context.SaveChangesAsync();
    }

    [Fact]
    public async Task Summoning_at_a_spaceport_returns_the_hold()
    {
        await DockAsync(_spaceportId);

        HttpResponseMessage response = await SummonAsync(_hullId);

        Assert.Equal(HttpStatusCode.OK, response.StatusCode);

        HoldPayload hold = (await response.Content.ReadFromJsonAsync<HoldPayload>())!;

        Assert.NotNull(hold.HoldInventoryId);
        Assert.Equal(80.0, hold.CapacityM3, 3);
    }

    [Fact]
    public async Task Summoning_at_a_market_is_a_conflict_a_player_can_act_on()
    {
        // 409 rather than 400: nothing about the request is malformed, and the fix is to walk
        // somewhere. The message is written to be shown as it stands.
        await DockAsync(_marketId);

        HttpResponseMessage response = await SummonAsync(_hullId);

        Assert.Equal(HttpStatusCode.Conflict, response.StatusCode);

        string body = await response.Content.ReadAsStringAsync();

        Assert.Contains("cannot_summon", body, StringComparison.Ordinal);
        Assert.Contains("TradingHub", body, StringComparison.Ordinal);
    }

    [Fact]
    public async Task Summoning_without_a_token_is_refused()
    {
        await DockAsync(_spaceportId);

        HttpResponseMessage response = await _client.PostAsJsonAsync(
            new Uri("/ships/summon", UriKind.Relative),
            new { characterId = _characterId, hullItemInstanceId = _hullId });

        Assert.Equal(HttpStatusCode.Unauthorized, response.StatusCode);
    }

    [Fact]
    public async Task No_ship_here_is_an_answer_rather_than_an_error()
    {
        // The client asks this every time an inventory screen opens, and having no ship to hand is
        // an ordinary state. A 404 would have callers treating it as a fault.
        await DockAsync(_spaceportId);

        HttpResponseMessage response = await HoldAsync();

        Assert.Equal(HttpStatusCode.OK, response.StatusCode);

        HoldPayload hold = (await response.Content.ReadFromJsonAsync<HoldPayload>())!;

        Assert.Null(hold.HoldInventoryId);
        Assert.Equal(0.0, hold.CapacityM3, 3);
    }

    [Fact]
    public async Task The_hold_is_reachable_where_the_ship_is_and_not_elsewhere()
    {
        await DockAsync(_spaceportId);
        await SummonAsync(_hullId);

        HoldPayload here =
            (await (await HoldAsync()).Content.ReadFromJsonAsync<HoldPayload>())!;

        Assert.NotNull(here.HoldInventoryId);

        // Flew to the market and left the ship at the yard.
        await DockAsync(_marketId);

        HoldPayload elsewhere =
            (await (await HoldAsync()).Content.ReadFromJsonAsync<HoldPayload>())!;

        Assert.Null(elsewhere.HoldInventoryId);
    }

    private async Task SeedAsync()
    {
        HttpResponseMessage registered = await _client.PostAsJsonAsync(
            new Uri("/accounts/register", UriKind.Relative),
            new { email = "pilot@local.test", password = "a-sufficiently-long-password" });

        registered.EnsureSuccessStatusCode();

        SessionPayload session = (await registered.Content.ReadFromJsonAsync<SessionPayload>())!;

        _token = session.Token;

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        StarSystem system = context.StarSystems.First();
        Body body = context.Bodies.First();

        var yard = new Station
        {
            Key = "station_yard",
            Name = "The Yard",
            StarSystemId = system.Id,
            BodyId = body.Id,
            Kind = StationKind.Spaceport,
            DirectionX = -1.0,
            DockingRangeKilometres = 5.0,
        };

        var market = new Station
        {
            Key = "station_market",
            Name = "The Market",
            StarSystemId = system.Id,
            Kind = StationKind.TradingHub,
            SystemX = 30.0,
            DockingRangeKilometres = 8.0,
        };

        var shuttle = new ItemDef
        {
            Key = "hull_shuttle",
            Name = "Shuttle",
            Category = ItemCategory.Hull,
            VolumeM3 = 200.0,
            HoldCapacityM3 = 80.0,
        };

        context.Stations.AddRange(yard, market);
        context.ItemDefs.Add(shuttle);
        await context.SaveChangesAsync();

        var character = new Character
        {
            AccountId = session.AccountId,
            Name = "Pilot",
            Race = Race.Humanoid,
            HomeBodyId = body.Id,
            Balance = Credits.FromWholeCredits(5_000),
            CreatedAt = DateTimeOffset.UtcNow,
        };

        context.Characters.Add(character);
        await context.SaveChangesAsync();

        var hangar = new Inventory
        {
            CharacterId = character.Id,
            Kind = InventoryKind.StationHangar,
            StationId = yard.Id,
            CapacityM3 = 0,
        };

        context.Inventories.Add(hangar);
        await context.SaveChangesAsync();

        var hull = new ItemInstance
        {
            ItemDefId = shuttle.Id,
            InventoryId = hangar.Id,
            Condition = 100,
            AcquisitionValue = Credits.Zero,
            CreatedAt = DateTimeOffset.UtcNow,
        };

        context.ItemInstances.Add(hull);
        await context.SaveChangesAsync();

        _characterId = character.Id;
        _spaceportId = yard.Id;
        _marketId = market.Id;
        _hullId = hull.Id;
    }
}
