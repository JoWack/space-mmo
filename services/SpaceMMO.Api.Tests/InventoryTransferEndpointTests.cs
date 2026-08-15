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
/// Goods move between a character's containers, and only where the character is.
/// </summary>
/// <remarks>
/// The rule worth testing is presence. Without it, hauling planet-locked materials (ADR-0008) would
/// be a request rather than a flight: a pilot could reach into a hangar four worlds away, and the
/// four-world economy would collapse into one warehouse with no error and no failing request. That
/// is the same failure the market's docking gate exists to prevent, so it is tested the same way.
/// </remarks>
[Collection(SharedApiDatabase.Name)]
public sealed class InventoryTransferEndpointTests(ApiDatabaseFixture fixture)
    : IAsyncLifetime, IDisposable
{
    private readonly ApiDatabaseFixture _fixture = fixture;

    private ApiFactory _factory = null!;
    private HttpClient _client = null!;

    private int _stationId;
    private int _otherStationId;
    private int _itemDefId;
    private int _characterId;
    private long _hangarId;
    private long _holdId;
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

    [Fact]
    public async Task Loading_a_hold_from_orbit_is_refused()
    {
        HttpResponseMessage response = await TransferAsync(_hangarId, _holdId, 5);

        Assert.Equal(HttpStatusCode.Conflict, response.StatusCode);
        Assert.Contains("not_docked", await response.Content.ReadAsStringAsync(), StringComparison.Ordinal);
    }

    [Fact]
    public async Task Loading_a_hold_while_docked_here_is_allowed()
    {
        await DockAsync(_stationId);

        HttpResponseMessage response = await TransferAsync(_hangarId, _holdId, 5);

        Assert.Equal(HttpStatusCode.OK, response.StatusCode);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        InventoryItem stack = verify.InventoryItems.Single(i => i.InventoryId == _holdId);

        Assert.Equal(5, stack.Quantity);
    }

    [Fact]
    public async Task Docked_somewhere_else_does_not_open_this_hangar()
    {
        // The check a laxer "is docked at all" would let through, and the one hauling depends on:
        // a pilot at Elsewhere must fly back before they can load anything from Here.
        await DockAsync(_otherStationId);

        HttpResponseMessage response = await TransferAsync(_hangarId, _holdId, 5);

        Assert.Equal(HttpStatusCode.Conflict, response.StatusCode);
        Assert.Contains("not_docked", await response.Content.ReadAsStringAsync(), StringComparison.Ordinal);
    }

    [Fact]
    public async Task Unloading_into_a_hangar_needs_the_same_presence()
    {
        await DockAsync(_stationId);
        await TransferAsync(_hangarId, _holdId, 5);
        await UndockAsync();

        // Outbound was allowed while docked; inbound must be refused once they have left, or the
        // gate would only be half a rule.
        HttpResponseMessage response = await TransferAsync(_holdId, _hangarId, 5);

        Assert.Equal(HttpStatusCode.Conflict, response.StatusCode);
        Assert.Contains("not_docked", await response.Content.ReadAsStringAsync(), StringComparison.Ordinal);
    }

    [Fact]
    public async Task Moving_more_than_is_held_is_refused_with_a_reason()
    {
        await DockAsync(_stationId);

        HttpResponseMessage response = await TransferAsync(_hangarId, _holdId, 500);

        Assert.Equal(HttpStatusCode.Conflict, response.StatusCode);
        Assert.Contains(
            "insufficient_items", await response.Content.ReadAsStringAsync(), StringComparison.Ordinal);
    }

    [Fact]
    public async Task A_tool_appears_in_the_inventory_it_is_sitting_in()
    {
        // The bug this asserts against: the endpoint read InventoryItems, which is stacks only, and
        // every category carrying condition is an ItemInstance instead. So a player crafted the
        // mining laser the onboarding questline exists to give them and saw nothing anywhere --
        // owned, usable, and absent from their own inventory.
        await using (SpaceMmoDbContext seed = _fixture.CreateContext())
        {
            var laser = new ItemDef
            {
                Key = "crude_mining_laser",
                Name = "Crude Mining Laser",
                Category = ItemCategory.Tool,
                VolumeM3 = 2,
            };

            seed.ItemDefs.Add(laser);
            await seed.SaveChangesAsync();

            seed.ItemInstances.AddRange(
                new ItemInstance
                {
                    ItemDefId = laser.Id,
                    InventoryId = _hangarId,
                    Condition = 87,
                    AcquisitionValue = Credits.FromWholeCredits(250),
                    CreatedAt = DateTimeOffset.UtcNow,
                },

                // Destroyed, and must not be listed: somebody looking at the wreck of a thing they
                // lost would reasonably conclude they still had it.
                new ItemInstance
                {
                    ItemDefId = laser.Id,
                    InventoryId = _hangarId,
                    Condition = 0,
                    AcquisitionValue = Credits.FromWholeCredits(250),
                    CreatedAt = DateTimeOffset.UtcNow,
                    DestroyedAt = DateTimeOffset.UtcNow,
                });

            await seed.SaveChangesAsync();
        }

        using var request = new HttpRequestMessage(
            HttpMethod.Get,
            new Uri($"/characters/{_characterId}/inventory", UriKind.Relative));

        request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", _token);

        HttpResponseMessage response = await _client.SendAsync(request);

        Assert.Equal(HttpStatusCode.OK, response.StatusCode);

        string body = await response.Content.ReadAsStringAsync();

        Assert.Contains("Crude Mining Laser", body, StringComparison.Ordinal);
        Assert.Contains("\"condition\":87", body, StringComparison.Ordinal);
        Assert.DoesNotContain("\"condition\":0", body, StringComparison.Ordinal);

        // The stacks are still there alongside it, because this replaced a bare array with an
        // envelope and dropping one half would be a different bug of the same shape.
        Assert.Contains("Ferrite Plate", body, StringComparison.Ordinal);
    }

    [Fact]
    public async Task An_empty_container_is_still_listed_and_named()
    {
        // The gap this closes: transfer is addressed by inventory id, and the response described
        // only contents -- so a container holding nothing could not be named, and the most likely
        // first haul anybody makes is into a hold that is empty by definition. A client could see
        // goods it was unable to move anywhere.
        long emptyId;

        await using (SpaceMmoDbContext seed = _fixture.CreateContext())
        {
            var carried = new Inventory
            {
                CharacterId = _characterId,
                Kind = InventoryKind.CharacterCarried,
            };

            seed.Inventories.Add(carried);
            await seed.SaveChangesAsync();

            emptyId = carried.Id;
        }

        using var request = new HttpRequestMessage(
            HttpMethod.Get,
            new Uri($"/characters/{_characterId}/inventory", UriKind.Relative));

        request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", _token);

        HttpResponseMessage response = await _client.SendAsync(request);

        Assert.Equal(HttpStatusCode.OK, response.StatusCode);

        string body = await response.Content.ReadAsStringAsync();

        // Present despite holding nothing, and addressable.
        Assert.Contains($"\"inventoryId\":{emptyId}", body, StringComparison.Ordinal);

        // And every stack says which container it is in, which is the other half of addressing a
        // transfer: naming a destination is useless without naming a source.
        Assert.Contains($"\"inventoryId\":{_hangarId}", body, StringComparison.Ordinal);
    }

    private async Task<HttpResponseMessage> TransferAsync(long from, long to, int quantity)
    {
        using var request = new HttpRequestMessage(
            HttpMethod.Post,
            new Uri($"/characters/{_characterId}/inventory/transfer", UriKind.Relative))
        {
            Content = JsonContent.Create(new
            {
                fromInventoryId = from,
                toInventoryId = to,
                itemDefId = _itemDefId,
                quantity,
            }),
        };

        request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", _token);

        return await _client.SendAsync(request);
    }

    private async Task DockAsync(int stationId)
    {
        using var request = new HttpRequestMessage(
            HttpMethod.Post, new Uri("/docking/dock", UriKind.Relative))
        {
            Content = JsonContent.Create(new { characterId = _characterId, stationId }),
        };

        request.Headers.Add("X-SpaceMMO-Service", ApiFactory.TestServiceSecret);

        (await _client.SendAsync(request)).EnsureSuccessStatusCode();
    }

    private async Task UndockAsync()
    {
        using var request = new HttpRequestMessage(
            HttpMethod.Post, new Uri("/docking/undock", UriKind.Relative))
        {
            Content = JsonContent.Create(new { characterId = _characterId }),
        };

        request.Headers.Add("X-SpaceMMO-Service", ApiFactory.TestServiceSecret);

        (await _client.SendAsync(request)).EnsureSuccessStatusCode();
    }

    private async Task SeedAsync()
    {
        HttpResponseMessage registered = await _client.PostAsJsonAsync(
            new Uri("/accounts/register", UriKind.Relative),
            new { email = "hauler@local.test", password = "a-sufficiently-long-password" });

        registered.EnsureSuccessStatusCode();

        SessionPayload session = (await registered.Content.ReadFromJsonAsync<SessionPayload>())!;

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
            Name = "Hauler",
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
            StationId = here.Id,
            CapacityM3 = 0,
        };

        var hold = new Inventory
        {
            CharacterId = character.Id,
            Kind = InventoryKind.ShipHold,
            CapacityM3 = 0,
        };

        context.Inventories.AddRange(hangar, hold);
        await context.SaveChangesAsync();

        context.InventoryItems.Add(new InventoryItem
        {
            InventoryId = hangar.Id,
            ItemDefId = item.Id,
            Quantity = 40,
            CostBasis = Credits.FromWholeCredits(400),
        });

        await context.SaveChangesAsync();

        _stationId = here.Id;
        _otherStationId = elsewhere.Id;
        _itemDefId = item.Id;
        _characterId = character.Id;
        _hangarId = hangar.Id;
        _holdId = hold.Id;
    }

    private sealed record SessionPayload(int AccountId, string Token, DateTimeOffset ExpiresAt);
}
