using System.Net;
using System.Net.Http.Json;
using System.Text.Json;
using Microsoft.EntityFrameworkCore;
using SpaceMMO.Data;
using SpaceMMO.Data.Entities;
using SpaceMMO.Data.Inventories;
using SpaceMMO.Domain.Characters;
using SpaceMMO.Domain.Combat;
using SpaceMMO.Domain.Economy;
using SpaceMMO.Domain.Items;
using SpaceMMO.Domain.Progression;
using SpaceMMO.Domain.Universe;
using Xunit;
using Xunit.Abstractions;

namespace SpaceMMO.Api.Tests;

/// <summary>
/// What dying costs, over the wire (task 103): only the game server may cost a death or take from a crate, and what
/// it sends -- a position as three numbers, a stack as an item id and a style number -- is read as meant.
/// </summary>
/// <remarks>
/// The rules are tested in the data project. Each answer here is also written to the test output, which is where the
/// client's parse tests take their JSON from: run with <c>--logger "console;verbosity=detailed"</c> to see it.
/// </remarks>
[Collection(SharedApiDatabase.Name)]
public sealed class DeathEndpointTests(ApiDatabaseFixture fixture, ITestOutputHelper output) : IAsyncLifetime, IDisposable
{
    private readonly ApiDatabaseFixture _fixture = fixture;
    private readonly ITestOutputHelper _output = output;

    private ApiFactory _factory = null!;
    private HttpClient _client = null!;

    private int _local;
    private int _intruder;
    private int _oreId;
    private int _plateId;

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
    public async Task Nobody_but_the_game_server_may_cost_a_death_list_crates_or_take_from_one()
    {
        HttpResponseMessage cost = await _client.PostAsJsonAsync(
            new Uri("/combat/deaths", UriKind.Relative), new { characterId = _intruder, x = 0.0, y = 0.0, z = 0.0 });
        HttpResponseMessage list = await _client.GetAsync(new Uri("/combat/containers", UriKind.Relative));
        HttpResponseMessage loot = await _client.PostAsJsonAsync(
            new Uri("/combat/containers/1/loot", UriKind.Relative), new { characterId = _local });

        Assert.Equal(HttpStatusCode.Unauthorized, cost.StatusCode);
        Assert.Equal(HttpStatusCode.Unauthorized, list.StatusCode);
        Assert.Equal(HttpStatusCode.Unauthorized, loot.StatusCode);
    }

    [Fact]
    public async Task A_kill_costed_where_the_server_says_the_body_fell()
    {
        await KillIntruderAsync();

        // As the game server writes it (FSpaceMMODeathWire::MakeDeathBody), to nine places.
        using JsonDocument body = await JsonAsync(await AsServerRaw(
            HttpMethod.Post,
            "/combat/deaths",
            $$"""{"characterId":{{_intruder}},"x":-180.250000000,"y":3.500000000,"z":-1.125000000}"""));

        JsonElement root = body.RootElement;

        Assert.True(root.GetProperty("applied").GetBoolean());
        Assert.False(root.GetProperty("atStation").GetBoolean());

        // Ore shows its own world (Terran, 1); plate the stack's (Martian, 2). Five plate: three drop.
        Assert.Equal(
            [("ferrite_ore", 1, 3, 3), ("ferrite_plate", 2, 3, 2)],
            root.GetProperty("lost").EnumerateArray()
                .Select(l => (
                    l.GetProperty("itemKey").GetString(), l.GetProperty("style").GetInt32(),
                    l.GetProperty("dropped").GetInt32(), l.GetProperty("destroyed").GetInt32()))
                .OrderBy(l => l.Item1));

        JsonElement worn = Assert.Single(root.GetProperty("worn").EnumerateArray());

        Assert.Equal(100, worn.GetProperty("before").GetInt32());
        Assert.Equal(90, worn.GetProperty("after").GetInt32());

        JsonElement container = root.GetProperty("container");

        Assert.Equal(-180.25, container.GetProperty("x").GetDouble());
        Assert.Equal(3.5, container.GetProperty("y").GetDouble());
        Assert.Equal(-1.125, container.GetProperty("z").GetDouble());
        Assert.Equal("Intruder", container.GetProperty("droppedBy").GetString());
        Assert.InRange(container.GetProperty("secondsLeft").GetDouble(), 590.0, 600.0);
        Assert.Equal(2, container.GetProperty("items").GetArrayLength());

        // Asked again, nothing is owed.
        using JsonDocument again = await JsonAsync(await AsServer(
            HttpMethod.Post, "/combat/deaths", new { characterId = _intruder, x = 0.0, y = 0.0, z = 0.0 }));

        Assert.False(again.RootElement.GetProperty("applied").GetBoolean());
        Assert.Equal(JsonValueKind.Null, again.RootElement.GetProperty("container").ValueKind);
    }

    [Fact]
    public async Task A_death_the_server_never_costed_is_costed_by_the_respawn()
    {
        await KillIntruderAsync();

        HttpResponseMessage respawn = await AsServer(HttpMethod.Post, "/combat/respawn", new { characterId = _intruder });

        Assert.Equal(HttpStatusCode.OK, respawn.StatusCode);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.NotNull((await verify.DeathRecords.SingleAsync()).CostsAppliedAt);
        Assert.False(await verify.InventoryItems.AnyAsync(i => i.Inventory!.CharacterId == _intruder));

        // And the crate lies where the world last saw them.
        LootContainer crate = await verify.LootContainers.SingleAsync();

        Assert.Equal((-199.0, 2.0, 3.0), (crate.SystemX, crate.SystemY, crate.SystemZ));
    }

    [Fact]
    public async Task The_server_lists_the_live_crates_and_takes_a_stack_named_by_its_numbers()
    {
        await KillIntruderAsync();
        await AsServer(HttpMethod.Post, "/combat/deaths", new { characterId = _intruder, x = -180.25, y = 3.5, z = -1.125 });

        using JsonDocument list = await JsonAsync(await AsServer(HttpMethod.Get, "/combat/containers"));

        JsonElement crate = Assert.Single(list.RootElement.EnumerateArray());
        long id = crate.GetProperty("id").GetInt64();

        // The plate, as the stack it fell as: Martian, 2. Written as the game server writes it
        // (FSpaceMMODeathWire::MakeLootBody), which the client's own test pins to the same string.
        using JsonDocument taken = await JsonAsync(await AsServerRaw(
            HttpMethod.Post, $"/combat/containers/{id}/loot",
            $$"""{"characterId":{{_local}},"items":[{"itemDefId":{{_plateId}},"style":2}]}"""));

        JsonElement root = taken.RootElement;

        Assert.True(root.GetProperty("took").GetBoolean());
        Assert.Equal(JsonValueKind.Null, root.GetProperty("refusal").ValueKind);
        Assert.Equal(
            [("ferrite_plate", 3)],
            root.GetProperty("taken").EnumerateArray()
                .Select(t => (t.GetProperty("itemKey").GetString(), t.GetProperty("quantity").GetInt32())));
        Assert.Equal(
            [("ferrite_ore", 3)],
            root.GetProperty("left").GetProperty("items").EnumerateArray()
                .Select(t => (t.GetProperty("itemKey").GetString(), t.GetProperty("quantity").GetInt32())));

        // Then the rest, which empties it: nothing named takes everything.
        using JsonDocument rest = await JsonAsync(await AsServerRaw(
            HttpMethod.Post, $"/combat/containers/{id}/loot", $$"""{"characterId":{{_local}}}"""));

        Assert.Equal(JsonValueKind.Null, rest.RootElement.GetProperty("left").ValueKind);

        using JsonDocument gone = await JsonAsync(await AsServer(
            HttpMethod.Post, $"/combat/containers/{id}/loot", new { characterId = _local }));

        Assert.False(gone.RootElement.GetProperty("took").GetBoolean());
        Assert.Equal("gone", gone.RootElement.GetProperty("refusal").GetString());

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.Equal(
            6,
            await verify.InventoryItems
                .Where(i => i.Inventory!.CharacterId == _local && i.Inventory.Kind == InventoryKind.CharacterCarried)
                .SumAsync(i => i.Quantity));
    }

    private async Task KillIntruderAsync()
    {
        await using (SpaceMmoDbContext context = _fixture.CreateContext())
        {
            Character intruder = await context.Characters.SingleAsync(c => c.Id == _intruder);
            intruder.Health = 30;
            intruder.LastCombatAt = DateTimeOffset.UtcNow;

            await context.SaveChangesAsync();
        }

        using JsonDocument hit = await JsonAsync(await AsServer(HttpMethod.Post, "/combat/hits", new
        {
            attackerCharacterId = _local,
            targetCharacterId = _intruder,
            zone = 1,
            facing = 0,

            // Faction B's half: the local is the Space Orc.
            x = -200.0,
            y = 0.0,
            z = 0.0,
        }));

        Assert.True(hit.RootElement.GetProperty("killed").GetBoolean());
    }

    private async Task<JsonDocument> JsonAsync(HttpResponseMessage response)
    {
        Assert.Equal(HttpStatusCode.OK, response.StatusCode);

        string text = await response.Content.ReadAsStringAsync();

        _output.WriteLine($"{response.RequestMessage!.Method} {response.RequestMessage.RequestUri}: {text}");

        return JsonDocument.Parse(text);
    }

    private async Task<HttpResponseMessage> AsServer(HttpMethod method, string path, object? body = null)
    {
        using var request = new HttpRequestMessage(method, new Uri(path, UriKind.Relative));

        if (body is not null)
        {
            request.Content = JsonContent.Create(body);
        }

        request.Headers.Add("X-SpaceMMO-Service", ApiFactory.TestServiceSecret);

        HttpResponseMessage response = await _client.SendAsync(request);

        return response;
    }

    private async Task<HttpResponseMessage> AsServerRaw(HttpMethod method, string path, string json)
    {
        using var request = new HttpRequestMessage(method, new Uri(path, UriKind.Relative));

        request.Content = new StringContent(json, System.Text.Encoding.UTF8, "application/json");
        request.Headers.Add("X-SpaceMMO-Service", ApiFactory.TestServiceSecret);

        return await _client.SendAsync(request);
    }

    private async Task SeedAsync()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        StarSystem system = await context.StarSystems.SingleAsync();
        Body capital = await context.Bodies.FirstAsync();

        capital.SystemX = 0;
        capital.SystemY = 0;
        capital.SystemZ = 0;

        system.FactionCapitalBodyId = capital.Id;
        system.FactionSafeRadiusKm = 40;
        system.FactionContestedRadiusKm = 120;
        system.FactionADirectionX = 1;
        system.FactionADirectionY = 0;
        system.FactionADirectionZ = 0;
        system.FactionRetaliationSeconds = 300;

        // Somewhere to come back at.
        context.Stations.Add(new Station
        {
            Key = "station_capital", Name = "Capital Station", StarSystemId = system.Id, BodyId = capital.Id,
            Kind = StationKind.TradingHub, DirectionX = 1.0, DockingRangeKilometres = 5.0,
        });

        var rifleSkill = new Skill
        {
            Key = "laser_rifle", Name = "Laser Rifle", Category = SkillCategory.Combat,
            WeaponFamily = WeaponFamily.Laser, WeaponType = WeaponType.Ranged, WeaponMount = WeaponMount.OnFoot,
            XpPerHundredDamage = 12,
        };

        context.Skills.AddRange(rifleSkill, new Skill { Key = "constitution", Name = "Constitution", Category = SkillCategory.Combat });

        var account = new Account { Email = "server@local.test", PasswordHash = "x", CreatedAt = DateTimeOffset.UtcNow };

        context.Accounts.Add(account);
        await context.SaveChangesAsync();

        var rifle = new ItemDef
        {
            Key = "laser_rifle", Name = "Laser Rifle", Category = ItemCategory.Weapon, VolumeM3 = 2,
            EquipsAs = EquipKind.Weapon, WeaponSkillId = rifleSkill.Id, WeaponDamage = 55, WeaponCooldownMs = 500,
            WeaponRangeMetres = 150,
        };

        var ore = new ItemDef
        {
            Key = "ferrite_ore", Name = "Ferrite Ore", Category = ItemCategory.Raw, VolumeM3 = 0.4, Style = ItemStyle.Terran,
        };

        var plate = new ItemDef
        {
            Key = "ferrite_plate", Name = "Structural Plate", Category = ItemCategory.Refined, VolumeM3 = 0.2,
        };

        var local = new Character
        {
            AccountId = account.Id, Name = "Local", Race = Race.SpaceOrc, HomeBodyId = capital.Id,
            Balance = Credits.FromWholeCredits(100), CreatedAt = DateTimeOffset.UtcNow,
        };

        // Last seen by the world beside where she will fall, for the respawn's costing.
        var intruder = new Character
        {
            AccountId = account.Id, Name = "Intruder", Race = Race.Humanoid, HomeBodyId = capital.Id,
            Balance = Credits.FromWholeCredits(100), CreatedAt = DateTimeOffset.UtcNow,
            LastSystemX = -199.0, LastSystemY = 2.0, LastSystemZ = 3.0,
        };

        context.ItemDefs.AddRange(rifle, ore, plate);
        context.Characters.AddRange(local, intruder);
        await context.SaveChangesAsync();

        _local = local.Id;
        _intruder = intruder.Id;
        _oreId = ore.Id;
        _plateId = plate.Id;

        var inventories = new InventoryService(context);

        // No main-story quests exist here, so nobody is under new-player protection.
        foreach (Character fighter in new[] { local, intruder })
        {
            Inventory pockets = await inventories.GetOrCreateCarriedAsync(fighter.Id);

            var instance = new ItemInstance
            {
                ItemDefId = rifle.Id, InventoryId = pockets.Id, Condition = 100,
                AcquisitionValue = Credits.FromWholeCredits(100), CreatedAt = DateTimeOffset.UtcNow,
            };

            context.ItemInstances.Add(instance);
            await context.SaveChangesAsync();

            await new EquipmentService(context).EquipAsync(fighter.Id, instance.Id, EquipmentSlot.Weapon1);
        }

        Inventory hers = await inventories.GetOrCreateCarriedAsync(_intruder);

        await inventories.AddAsync(hers.Id, _oreId, 6, Credits.FromWholeCredits(30));
        await inventories.AddAsync(hers.Id, _plateId, 5, Credits.Zero, ItemStyle.Martian);
        await context.SaveChangesAsync();
    }
}
