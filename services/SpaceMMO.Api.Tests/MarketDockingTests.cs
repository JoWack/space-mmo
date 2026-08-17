using System.Net;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using SpaceMMO.Data;
using SpaceMMO.Data.Entities;
using SpaceMMO.Domain.Characters;
using SpaceMMO.Domain.Economy;
using SpaceMMO.Domain.Items;
using SpaceMMO.Domain.Market;
using SpaceMMO.Domain.Universe;
using Xunit;

namespace SpaceMMO.Api.Tests;

/// <summary>
/// The market is a place, and being at it is what entitles you to use it.
/// </summary>
/// <remarks>
/// These are the tests that make docking mean something. Without them the gate could be removed,
/// or quietly pass everybody, and the only symptom would be an economy that works slightly too
/// well — no exception, no failing request, just a market reachable from orbit and hauling that
/// nobody needs to do (ADR-0008).
/// </remarks>
[Collection(SharedApiDatabase.Name)]
public sealed class MarketDockingTests(ApiDatabaseFixture fixture) : IAsyncLifetime, IDisposable
{
    private readonly ApiDatabaseFixture _fixture = fixture;

    private ApiFactory _factory = null!;
    private HttpClient _client = null!;

    private int _stationId;
    private int _otherStationId;
    private int _itemDefId;
    private int _characterId;
    private string _token = string.Empty;

    public async Task InitializeAsync()
    {
        await _fixture.ResetAsync();

        // Configured with a service secret, because docking is a service call. Without one the
        // API refuses every service caller, which is correct behaviour and looked like a broken
        // gate the first time this test ran.
        _factory = new ApiFactory(_fixture.ConnectionString, ApiFactory.TestServiceSecret);
        _client = _factory.CreateClient();

        await AuthorizationTests.SeedStartingWorldAsync(_fixture);
        await SeedMarketAsync();
    }

    public Task DisposeAsync() => Task.CompletedTask;

    public void Dispose()
    {
        _client?.Dispose();
        _factory?.Dispose();
    }

    [Fact]
    public async Task Listings_show_the_whole_tradeable_catalogue()
    {
        // The fix for task 105. A player who wants ferrite and holds none could not previously
        // discover that a market for it existed, because the book was only reachable for items they
        // already owned. Listing only what is for sale would fix half of that -- somebody placing a
        // buy order needs to find an item precisely because nobody is selling it.
        await using (SpaceMmoDbContext seed = _fixture.CreateContext())
        {
            // Stackable, so it can be ordered; and a hull, which cannot, because the book moves
            // quantities out of a hangar while a hull exists as an instance carrying its own
            // condition. Offering one would be offering something no player can act on.
            seed.ItemDefs.AddRange(
                new ItemDef
                {
                    Key = "silicate_dust",
                    Name = "Silicate Dust",
                    Category = ItemCategory.Raw,
                    VolumeM3 = 0.1,
                },
                new ItemDef
                {
                    Key = "shuttle_hull",
                    Name = "Shuttle Hull",
                    Category = ItemCategory.Hull,
                    VolumeM3 = 40,
                });

            await seed.SaveChangesAsync();
        }

        using var request = new HttpRequestMessage(
            HttpMethod.Get,
            new Uri($"/market/listings?stationId={_stationId}", UriKind.Relative));

        HttpResponseMessage response = await _client!.SendAsync(request);

        Assert.Equal(HttpStatusCode.OK, response.StatusCode);

        string body = await response.Content.ReadAsStringAsync();

        // Present although nobody has ever traded it here.
        Assert.Contains("Silicate Dust", body, StringComparison.Ordinal);
        Assert.Contains("Ferrite Plate", body, StringComparison.Ordinal);

        // Absent, because it cannot be ordered at all.
        Assert.DoesNotContain("Shuttle Hull", body, StringComparison.Ordinal);
    }

    [Fact]
    public async Task Listings_can_be_searched_by_name_or_key()
    {
        using var request = new HttpRequestMessage(
            HttpMethod.Get,
            new Uri($"/market/listings?stationId={_stationId}&search=FERR", UriKind.Relative));

        HttpResponseMessage response = await _client!.SendAsync(request);

        Assert.Equal(HttpStatusCode.OK, response.StatusCode);

        // Case-insensitive, and matching the key as well as the name, so a player who knows either
        // finds the item.
        Assert.Contains("Ferrite Plate", await response.Content.ReadAsStringAsync(), StringComparison.Ordinal);

        using var miss = new HttpRequestMessage(
            HttpMethod.Get,
            new Uri($"/market/listings?stationId={_stationId}&search=nothinglikethis", UriKind.Relative));

        HttpResponseMessage empty = await _client.SendAsync(miss);

        // A search that matches nothing is an answer, not a failure: nobody trades that here.
        Assert.Equal(HttpStatusCode.OK, empty.StatusCode);
        Assert.DoesNotContain("Ferrite", await empty.Content.ReadAsStringAsync(), StringComparison.Ordinal);
    }

    [Fact]
    public async Task Listing_from_orbit_is_refused()
    {
        HttpResponseMessage response = await PlaceOrderAsync(_stationId);

        Assert.Equal(HttpStatusCode.Conflict, response.StatusCode);
        Assert.Contains("not_docked", await response.Content.ReadAsStringAsync());
    }

    [Fact]
    public async Task Listing_while_docked_here_is_allowed()
    {
        await DockAsync(_stationId);

        HttpResponseMessage response = await PlaceOrderAsync(_stationId);

        Assert.Equal(HttpStatusCode.OK, response.StatusCode);
    }

    [Fact]
    public async Task Docked_somewhere_else_does_not_open_this_book()
    {
        // The check that a laxer "is docked at all" would let through. A character sitting at
        // Deepdock has no business listing into the capital's book, and the reason the capital
        // has the only global market is that you have to fly there.
        await DockAsync(_otherStationId);

        HttpResponseMessage response = await PlaceOrderAsync(_stationId);

        Assert.Equal(HttpStatusCode.Conflict, response.StatusCode);
        Assert.Contains("not_docked", await response.Content.ReadAsStringAsync());
    }

    [Fact]
    public async Task Undocking_closes_the_book_again()
    {
        await DockAsync(_stationId);
        await UndockAsync();

        HttpResponseMessage response = await PlaceOrderAsync(_stationId);

        Assert.Equal(HttpStatusCode.Conflict, response.StatusCode);
    }

    [Fact]
    public async Task Selling_to_the_faction_needs_a_counter_to_stand_at()
    {
        // The faction standing order is the one guaranteed way to turn goods into credits. Left
        // open from anywhere it would be the way to play the economy without ever arriving
        // somewhere, which is the opposite of why it exists.
        HttpResponseMessage response = await PostAsync(
            "/market/faction-orders/sell",
            new { characterId = _characterId, stationId = _stationId, itemDefId = _itemDefId, quantity = 1 });

        Assert.Equal(HttpStatusCode.Conflict, response.StatusCode);
        Assert.Contains("not_docked", await response.Content.ReadAsStringAsync());
    }

    [Fact]
    public async Task A_player_cannot_dock_themselves()
    {
        // Docking asserts a position, and a position is not a client's to assert. Without this
        // the gate is decorative: anyone refused for not being docked could simply say they were.
        HttpResponseMessage response = await PostAsync(
            "/docking/dock", new { characterId = _characterId, stationId = _stationId });

        Assert.Equal(HttpStatusCode.Unauthorized, response.StatusCode);
    }

    private async Task<HttpResponseMessage> PlaceOrderAsync(int stationId) =>
        await PostAsync("/market/orders", new
        {
            characterId = _characterId,
            stationId,
            itemDefId = _itemDefId,
            side = OrderSide.Buy,
            limitPriceMinorUnits = 100L,
            quantity = 1,
            goodForDays = 30,
        });

    private async Task<HttpResponseMessage> PostAsync(string path, object body)
    {
        using var request = new HttpRequestMessage(HttpMethod.Post, new Uri(path, UriKind.Relative))
        {
            Content = JsonContent.Create(body),
        };

        request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", _token);

        return await _client.SendAsync(request);
    }

    /// <summary>Docks as the game server would, with the service credential.</summary>
    private async Task DockAsync(int stationId)
    {
        using var request = new HttpRequestMessage(
            HttpMethod.Post, new Uri("/docking/dock", UriKind.Relative))
        {
            Content = JsonContent.Create(new { characterId = _characterId, stationId }),
        };

        request.Headers.Add("X-SpaceMMO-Service", ApiFactory.TestServiceSecret);

        HttpResponseMessage response = await _client.SendAsync(request);

        response.EnsureSuccessStatusCode();
    }

    private async Task UndockAsync()
    {
        using var request = new HttpRequestMessage(
            HttpMethod.Post, new Uri("/docking/undock", UriKind.Relative))
        {
            Content = JsonContent.Create(new { characterId = _characterId }),
        };

        request.Headers.Add("X-SpaceMMO-Service", ApiFactory.TestServiceSecret);

        HttpResponseMessage response = await _client.SendAsync(request);

        response.EnsureSuccessStatusCode();
    }

    private async Task SeedMarketAsync()
    {
        HttpResponseMessage registered = await _client.PostAsJsonAsync(
            new Uri("/accounts/register", UriKind.Relative),
            new { email = "trader@local.test", password = "a-sufficiently-long-password" });

        registered.EnsureSuccessStatusCode();

        SessionPayload session =
            (await registered.Content.ReadFromJsonAsync<SessionPayload>())!;

        _token = session.Token;

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        StarSystem system = context.StarSystems.First();
        Body body = context.Bodies.First();

        var here = new Station
        {
            Key = "station_here",
            Name = "Here",
            StarSystemId = system.Id,
            BodyId = body.Id,
            Kind = StationKind.TradingHub,
            DirectionX = -1.0,
            DirectionY = 0.0,
            DirectionZ = 0.0,
            DockingRangeKilometres = 5.0,
        };

        var elsewhere = new Station
        {
            Key = "station_elsewhere",
            Name = "Elsewhere",
            StarSystemId = system.Id,
            Kind = StationKind.Spaceport,
            SystemX = 30.0,
            SystemY = 12.0,
            SystemZ = 4.0,
            DockingRangeKilometres = 8.0,
        };

        var item = new ItemDef
        {
            Key = "ferrite_plate",
            Name = "Ferrite Plate",
            Category = ItemCategory.Refined,
            VolumeM3 = 0.2,
        };

        context.Stations.AddRange(here, elsewhere);
        context.ItemDefs.Add(item);
        await context.SaveChangesAsync();

        var character = new Character
        {
            AccountId = session.AccountId,
            Name = "Trader",
            Race = Race.Humanoid,
            HomeBodyId = body.Id,
            Balance = Credits.FromWholeCredits(5_000),
            CreatedAt = DateTimeOffset.UtcNow,
        };

        context.Characters.Add(character);
        await context.SaveChangesAsync();

        _stationId = here.Id;
        _otherStationId = elsewhere.Id;
        _itemDefId = item.Id;
        _characterId = character.Id;
    }

    private sealed record SessionPayload(int AccountId, string Token, DateTimeOffset ExpiresAt);
}
