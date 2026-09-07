using System.Net;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using Microsoft.EntityFrameworkCore;
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

    private sealed record ActiveShipPayload(
        long? HullItemInstanceId, string? Name, int? StationId, bool Aboard);

    /// <summary>
    /// A player cannot say they are sitting in their ship.
    /// </summary>
    /// <remarks>
    /// <para>
    /// <strong>The refusal that keeps a hold a place rather than an account.</strong> Being aboard
    /// opens the hold from anywhere in the game, by design — a ship in flight is docked nowhere.
    /// So a client that could assert it could reach its cargo from a rock on a planet, which is
    /// exactly the rule ADR-0012 point 4 exists to stop.
    /// </para>
    /// <para>
    /// Summoning takes a player token and this does not, and the line between them is the one
    /// docking already draws: summoning is a request whose every fact the server checks from its
    /// own rows, and being aboard is a fact about where a body is in the world.
    /// </para>
    /// </remarks>
    [Fact]
    public async Task A_player_cannot_claim_to_be_aboard_their_own_ship()
    {
        await DockAsync(_spaceportId);
        (await SummonAsync(_hullId)).EnsureSuccessStatusCode();

        var request = new HttpRequestMessage(
            HttpMethod.Post, new Uri("/ships/board", UriKind.Relative))
        {
            Content = JsonContent.Create(
                new { characterId = _characterId, hullItemInstanceId = _hullId }),
        };

        // Their own valid token, their own character, their own hull. Still refused.
        request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", _token);

        HttpResponseMessage response = await _client.SendAsync(request);

        Assert.Equal(HttpStatusCode.Unauthorized, response.StatusCode);

        // And nothing was recorded, so the refusal is not a status code on a change that happened
        // anyway. Undocked, the hold is only reachable to somebody actually sitting in the ship.
        await DockAsync(null);

        HttpResponseMessage hold = await HoldAsync();

        hold.EnsureSuccessStatusCode();

        HoldPayload? payload = await hold.Content.ReadFromJsonAsync<HoldPayload>();

        Assert.Null(payload!.HoldInventoryId);
    }

    /// <summary>
    /// The game server boards somebody, and the hold travels with them.
    /// </summary>
    /// <remarks>
    /// The end-to-end version of ADR-0012 point 4, over the wire the game actually uses: board,
    /// leave the station entirely, and the hold is still there.
    /// </remarks>
    [Fact]
    public async Task The_game_server_can_board_somebody_and_the_hold_goes_with_them()
    {
        await DockAsync(_spaceportId);
        (await SummonAsync(_hullId)).EnsureSuccessStatusCode();

        var board = new HttpRequestMessage(
            HttpMethod.Post, new Uri("/ships/board", UriKind.Relative))
        {
            Content = JsonContent.Create(
                new { characterId = _characterId, hullItemInstanceId = _hullId }),
        };

        board.Headers.Add("X-SpaceMMO-Service", ApiFactory.TestServiceSecret);

        (await _client.SendAsync(board)).EnsureSuccessStatusCode();

        // Away from every station.
        await DockAsync(null);

        HttpResponseMessage hold = await HoldAsync();

        hold.EnsureSuccessStatusCode();

        HoldPayload? payload = await hold.Content.ReadFromJsonAsync<HoldPayload>();

        Assert.NotNull(payload!.HoldInventoryId);

        // Stepping out closes it again, wherever they are standing. Without this the flag is a
        // latch, and a latch opens the hold from a planet for the rest of the character's life.
        var step = new HttpRequestMessage(
            HttpMethod.Post, new Uri("/ships/disembark", UriKind.Relative))
        {
            Content = JsonContent.Create(new { characterId = _characterId }),
        };

        step.Headers.Add("X-SpaceMMO-Service", ApiFactory.TestServiceSecret);

        (await _client.SendAsync(step)).EnsureSuccessStatusCode();

        HttpResponseMessage after = await HoldAsync();

        after.EnsureSuccessStatusCode();

        Assert.Null((await after.Content.ReadFromJsonAsync<HoldPayload>())!.HoldInventoryId);
    }

    /// <summary>
    /// A player cannot park their own ship in a hangar they are nowhere near.
    /// </summary>
    /// <remarks>
    /// The same line docking and boarding draw, for the same reason: this asserts that a ship has
    /// been taken out of the world and put inside a building, and the only party that can know that
    /// is the one that removed the pawn. A player token that worked here would let a client teleport
    /// its own hull across the system by asserting it had docked.
    /// </remarks>
    [Fact]
    public async Task A_player_cannot_park_their_own_ship()
    {
        await DockAsync(_spaceportId);
        (await SummonAsync(_hullId)).EnsureSuccessStatusCode();

        var request = new HttpRequestMessage(
            HttpMethod.Post, new Uri("/ships/stow", UriKind.Relative))
        {
            Content = JsonContent.Create(
                new { characterId = _characterId, hullItemInstanceId = _hullId, stationId = _marketId }),
        };

        request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", _token);

        HttpResponseMessage response = await _client.SendAsync(request);

        Assert.Equal(HttpStatusCode.Unauthorized, response.StatusCode);

        // And nothing moved, so the refusal is not a status code on a change that happened anyway.
        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        ItemInstance hull = await verify.ItemInstances
            .Include(i => i.Inventory)
            .SingleAsync(i => i.Id == _hullId);

        Assert.Equal(_spaceportId, hull.Inventory!.StationId);
    }

    /// <summary>
    /// Docking a ship parks it, over the wire the game client actually sends (task 153).
    /// </summary>
    /// <remarks>
    /// <para>
    /// <strong>The body is written out by hand rather than built from the request record.</strong>
    /// The client composes this JSON with <c>FString::Printf</c> in
    /// <c>USpaceMMOBackendClient::StowAsServer</c>, so a field renamed on this side binds to a
    /// default on the other — <c>hullItemInstanceId</c> becomes 0 and the refusal reads as "that
    /// hull is not yours", for a hull that is. A test that posted a C# object would agree with
    /// itself and catch none of it.
    /// </para>
    /// <para>
    /// The hold is asked for afterwards, because that is the consequence a player meets: it opens
    /// where the ship now is and not where it used to be.
    /// </para>
    /// </remarks>
    [Fact]
    public async Task The_game_server_parks_a_docked_ship_and_its_hold_opens_there()
    {
        await DockAsync(_spaceportId);
        (await SummonAsync(_hullId)).EnsureSuccessStatusCode();

        // Flown to the market and docked there.
        await DockAsync(_marketId);

        var stow = new HttpRequestMessage(
            HttpMethod.Post, new Uri("/ships/stow", UriKind.Relative))
        {
            Content = new StringContent(
                $"{{\"characterId\":{_characterId},\"hullItemInstanceId\":{_hullId},"
                + $"\"stationId\":{_marketId}}}",
                System.Text.Encoding.UTF8,
                "application/json"),
        };

        stow.Headers.Add("X-SpaceMMO-Service", ApiFactory.TestServiceSecret);

        (await _client.SendAsync(stow)).EnsureSuccessStatusCode();

        await using (SpaceMmoDbContext verify = _fixture.CreateContext())
        {
            ItemInstance hull = await verify.ItemInstances
                .Include(i => i.Inventory)
                .SingleAsync(i => i.Id == _hullId);

            Assert.Equal(_marketId, hull.Inventory!.StationId);
        }

        HttpResponseMessage hold = await HoldAsync();

        hold.EnsureSuccessStatusCode();

        Assert.NotNull((await hold.Content.ReadFromJsonAsync<HoldPayload>())!.HoldInventoryId);
    }

    /// <summary>
    /// What the game server reads to decide what to put in the world.
    /// </summary>
    /// <remarks>
    /// The station is the load-bearing field, and the field names are pinned here because the game
    /// client parses them by hand: a parser that finds nothing leaves the hull id at zero, which
    /// reads as "this character owns no ship" and silently spawns nothing at all.
    /// </remarks>
    [Fact]
    public async Task The_active_ship_says_what_to_spawn_and_where()
    {
        await DockAsync(_spaceportId);
        (await SummonAsync(_hullId)).EnsureSuccessStatusCode();

        var request = new HttpRequestMessage(
            HttpMethod.Get, new Uri($"/ships/{_characterId}/active", UriKind.Relative));

        request.Headers.Add("X-SpaceMMO-Service", ApiFactory.TestServiceSecret);

        HttpResponseMessage response = await _client.SendAsync(request);

        Assert.Equal(HttpStatusCode.OK, response.StatusCode);

        string json = await response.Content.ReadAsStringAsync();

        Assert.Contains("\"hullItemInstanceId\":", json);
        Assert.Contains("\"stationId\":", json);

        ActiveShipPayload? active = await response.Content.ReadFromJsonAsync<ActiveShipPayload>();

        Assert.Equal(_hullId, active!.HullItemInstanceId);
        Assert.Equal(_spaceportId, active.StationId);
        Assert.False(active.Aboard);
        Assert.False(string.IsNullOrWhiteSpace(active.Name));
    }

    [Fact]
    public async Task Owning_no_ship_answers_with_nulls_rather_than_a_missing_resource()
    {
        var request = new HttpRequestMessage(
            HttpMethod.Get, new Uri($"/ships/{_characterId}/active", UriKind.Relative));

        request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", _token);

        HttpResponseMessage response = await _client.SendAsync(request);

        // 200 with nulls, like the hold. Owning no ship is the ordinary state for most of the
        // opening, and a 404 would have callers treating it as a fault.
        Assert.Equal(HttpStatusCode.OK, response.StatusCode);

        ActiveShipPayload? active = await response.Content.ReadFromJsonAsync<ActiveShipPayload>();

        Assert.Null(active!.HullItemInstanceId);
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
