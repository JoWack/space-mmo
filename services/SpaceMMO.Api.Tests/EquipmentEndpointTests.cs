using System.Net;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Text.Json;
using SpaceMMO.Data;
using SpaceMMO.Data.Entities;
using SpaceMMO.Domain.Characters;
using SpaceMMO.Domain.Economy;
using SpaceMMO.Domain.Items;
using Xunit;

namespace SpaceMMO.Api.Tests;

/// <summary>
/// Equipping over the wire (task 193), read back the way the client reads it.
/// </summary>
/// <remarks>
/// The service's rules are tested in the data project. What only this can show is the JSON: that a slot
/// and a kind go out as the numbers EBackendEquipmentSlot and EBackendEquipKind mirror, that the Equipped
/// container is listed, and that a refusal carries the reason the client branches on.
/// </remarks>
[Collection(SharedApiDatabase.Name)]
public sealed class EquipmentEndpointTests(ApiDatabaseFixture fixture) : IAsyncLifetime, IDisposable
{
    private readonly ApiDatabaseFixture _fixture = fixture;

    private ApiFactory _factory = null!;
    private HttpClient _client = null!;

    private int _characterId;
    private long _laserInstanceId;
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
    public async Task An_equipped_tool_is_listed_in_its_slot_as_the_numbers_the_client_mirrors()
    {
        HttpResponseMessage equipped = await PostAsync("equip", new { itemInstanceId = _laserInstanceId });

        Assert.Equal(HttpStatusCode.OK, equipped.StatusCode);

        using JsonDocument result = (await equipped.Content.ReadFromJsonAsync<JsonDocument>())!;

        Assert.Equal(5, result.RootElement.GetProperty("slot").GetInt32());

        using JsonDocument inventory = await ReadInventoryAsync();

        JsonElement laser = inventory.RootElement.GetProperty("items").EnumerateArray()
            .Single(i => i.GetProperty("id").GetInt64() == _laserInstanceId);

        // MiningTool is 5 as a slot and 4 as a kind: the two enums differ because a weapon goes in either of
        // two slots, and a client that mixed them up would put a laser in Weapon 2's box.
        Assert.Equal(5, laser.GetProperty("equippedSlot").GetInt32());
        Assert.Equal(4, laser.GetProperty("equipsAs").GetInt32());

        JsonElement worn = inventory.RootElement.GetProperty("containers").EnumerateArray()
            .Single(c => c.GetProperty("inventoryId").GetInt64() == laser.GetProperty("inventoryId").GetInt64());

        Assert.Equal(3, worn.GetProperty("kind").GetInt32());
    }

    [Fact]
    public async Task An_unequipped_item_says_so_with_a_null_slot()
    {
        using JsonDocument inventory = await ReadInventoryAsync();

        JsonElement laser = inventory.RootElement.GetProperty("items").EnumerateArray()
            .Single(i => i.GetProperty("id").GetInt64() == _laserInstanceId);

        Assert.Equal(JsonValueKind.Null, laser.GetProperty("equippedSlot").ValueKind);
        Assert.Equal(4, laser.GetProperty("equipsAs").GetInt32());
    }

    [Fact]
    public async Task Taking_it_off_returns_it_to_the_pockets()
    {
        (await PostAsync("equip", new { itemInstanceId = _laserInstanceId })).EnsureSuccessStatusCode();

        HttpResponseMessage off = await PostAsync("unequip", new { slot = 5 });

        Assert.Equal(HttpStatusCode.OK, off.StatusCode);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        ItemInstance laser = verify.ItemInstances.Single(i => i.Id == _laserInstanceId);
        Inventory pockets = verify.Inventories.Single(i => i.Id == laser.InventoryId);

        Assert.Equal(InventoryKind.CharacterCarried, pockets.Kind);
        Assert.Null(laser.EquippedSlot);
    }

    [Fact]
    public async Task A_refusal_carries_its_reason()
    {
        HttpResponseMessage wrong = await PostAsync("equip", new { itemInstanceId = _laserInstanceId, slot = 0 });
        HttpResponseMessage empty = await PostAsync("unequip", new { slot = 2 });

        Assert.Equal(HttpStatusCode.Conflict, wrong.StatusCode);
        Assert.Contains("wrong_slot", await wrong.Content.ReadAsStringAsync(), StringComparison.Ordinal);
        Assert.Equal(HttpStatusCode.Conflict, empty.StatusCode);
        Assert.Contains("empty_slot", await empty.Content.ReadAsStringAsync(), StringComparison.Ordinal);
    }

    [Fact]
    public async Task Dragging_an_item_into_the_equipped_container_is_refused()
    {
        // The drag route exists for every container the inventory lists, and Equipped is now one of them, so
        // a client that offered it as a destination must be told no rather than get a slotless worn laser.
        (await PostAsync("equip", new { itemInstanceId = _laserInstanceId })).EnsureSuccessStatusCode();

        long spare;
        long equippedId;

        await using (SpaceMmoDbContext seed = _fixture.CreateContext())
        {
            ItemInstance worn = seed.ItemInstances.Single(i => i.Id == _laserInstanceId);
            Inventory pockets = seed.Inventories.Single(
                i => i.CharacterId == _characterId && i.Kind == InventoryKind.CharacterCarried);

            var another = new ItemInstance
            {
                ItemDefId = worn.ItemDefId,
                InventoryId = pockets.Id,
                Condition = 100,
                AcquisitionValue = Credits.FromWholeCredits(100),
                CreatedAt = DateTimeOffset.UtcNow,
            };

            seed.ItemInstances.Add(another);
            await seed.SaveChangesAsync();

            spare = another.Id;
            equippedId = worn.InventoryId!.Value;
        }

        using var request = new HttpRequestMessage(
            HttpMethod.Post,
            new Uri($"/characters/{_characterId}/inventory/transfer-instance", UriKind.Relative))
        {
            Content = JsonContent.Create(new { itemInstanceId = spare, toInventoryId = equippedId }),
        };

        request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", _token);

        HttpResponseMessage response = await _client.SendAsync(request);

        Assert.Equal(HttpStatusCode.BadRequest, response.StatusCode);
        Assert.Contains("Equip or unequip", await response.Content.ReadAsStringAsync(), StringComparison.Ordinal);
    }

    private async Task<HttpResponseMessage> PostAsync(string action, object body)
    {
        using var request = new HttpRequestMessage(
            HttpMethod.Post,
            new Uri($"/characters/{_characterId}/equipment/{action}", UriKind.Relative))
        {
            Content = JsonContent.Create(body),
        };

        request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", _token);

        return await _client.SendAsync(request);
    }

    private async Task<JsonDocument> ReadInventoryAsync()
    {
        using var request = new HttpRequestMessage(
            HttpMethod.Get, new Uri($"/characters/{_characterId}/inventory", UriKind.Relative));

        request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", _token);

        HttpResponseMessage response = await _client.SendAsync(request);

        response.EnsureSuccessStatusCode();

        return (await response.Content.ReadFromJsonAsync<JsonDocument>())!;
    }

    private async Task SeedAsync()
    {
        HttpResponseMessage registered = await _client.PostAsJsonAsync(
            new Uri("/accounts/register", UriKind.Relative),
            new { email = "wearer@local.test", password = "a-sufficiently-long-password" });

        registered.EnsureSuccessStatusCode();

        SessionPayload session = (await registered.Content.ReadFromJsonAsync<SessionPayload>())!;

        _token = session.Token;

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        Body body = context.Bodies.First();

        var laser = new ItemDef
        {
            Key = "crude_mining_laser",
            Name = "Crude Mining Laser",
            Category = ItemCategory.Tool,
            VolumeM3 = 2,
            EquipsAs = EquipKind.MiningTool,
        };

        context.ItemDefs.Add(laser);

        var character = new Character
        {
            AccountId = session.AccountId,
            Name = "Wearer",
            Race = Race.Humanoid,
            HomeBodyId = body.Id,
            Balance = Credits.FromWholeCredits(5_000),
            CreatedAt = DateTimeOffset.UtcNow,
        };

        context.Characters.Add(character);
        await context.SaveChangesAsync();

        var pockets = new Inventory
        {
            CharacterId = character.Id,
            Kind = InventoryKind.CharacterCarried,
            CapacityM3 = 6,
        };

        context.Inventories.Add(pockets);
        await context.SaveChangesAsync();

        var instance = new ItemInstance
        {
            ItemDefId = laser.Id,
            InventoryId = pockets.Id,
            Condition = 100,
            AcquisitionValue = Credits.FromWholeCredits(200),
            CreatedAt = DateTimeOffset.UtcNow,
        };

        context.ItemInstances.Add(instance);
        await context.SaveChangesAsync();

        _characterId = character.Id;
        _laserInstanceId = instance.Id;
    }

    private sealed record SessionPayload(int AccountId, string Token, DateTimeOffset ExpiresAt);
}
