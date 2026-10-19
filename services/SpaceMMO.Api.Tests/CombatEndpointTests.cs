using System.Globalization;
using System.Net;
using System.Net.Http.Json;
using System.Text.Json;
using System.Text.RegularExpressions;
using Microsoft.EntityFrameworkCore;
using SpaceMMO.Data;
using SpaceMMO.Data.Entities;
using SpaceMMO.Data.Inventories;
using SpaceMMO.Domain.Characters;
using SpaceMMO.Domain.Combat;
using SpaceMMO.Domain.Economy;
using SpaceMMO.Domain.Items;
using SpaceMMO.Domain.Progression;
using Xunit;

namespace SpaceMMO.Api.Tests;

/// <summary>
/// Combat over the wire (task 194): only the game server may report a hit, and a hit reported as the numbers
/// the server sends lands as intended.
/// </summary>
/// <remarks>
/// The rules are tested in the data project. What only this can show is the boundary: a client cannot claim a
/// hit, and the zone and facing arrive as the integers the server's C++ mirrors (head 0, back 2) and are read as
/// those, not as some other member.
/// </remarks>
[Collection(SharedApiDatabase.Name)]
public sealed class CombatEndpointTests(ApiDatabaseFixture fixture) : IAsyncLifetime, IDisposable
{
    private readonly ApiDatabaseFixture _fixture = fixture;

    private ApiFactory _factory = null!;
    private HttpClient _client = null!;

    private int _local;
    private int _intruder;

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
    public async Task Nobody_but_the_game_server_may_report_a_hit_respawn_or_read_health()
    {
        HttpResponseMessage hit = await _client.PostAsJsonAsync(new Uri("/combat/hits", UriKind.Relative), Hit(0, 2));
        HttpResponseMessage respawn = await _client.PostAsJsonAsync(
            new Uri("/combat/respawn", UriKind.Relative), new { characterId = _intruder });
        HttpResponseMessage status = await _client.GetAsync(new Uri($"/combat/{_intruder}", UriKind.Relative));

        Assert.Equal(HttpStatusCode.Unauthorized, hit.StatusCode);
        Assert.Equal(HttpStatusCode.Unauthorized, respawn.StatusCode);
        Assert.Equal(HttpStatusCode.Unauthorized, status.StatusCode);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.Null((await verify.Characters.SingleAsync(c => c.Id == _intruder)).Health);
    }

    [Fact]
    public async Task A_head_shot_from_behind_sent_as_numbers_lands_as_one()
    {
        HttpResponseMessage response = await AsServer(HttpMethod.Post, "/combat/hits", Hit(zone: 0, facing: 2));

        Assert.Equal(HttpStatusCode.OK, response.StatusCode);

        using JsonDocument body = (await response.Content.ReadFromJsonAsync<JsonDocument>())!;

        Assert.True(body.RootElement.GetProperty("landed").GetBoolean());

        // 55 x 1.5 x 1.2. Read as body and front, it would be 55; as legs, 41.
        Assert.Equal(99, body.RootElement.GetProperty("damage").GetInt32());
        Assert.Equal(901, body.RootElement.GetProperty("targetHealth").GetInt32());
    }

    [Fact]
    public async Task A_refused_hit_says_why()
    {
        // The intruder firing first in the local's half (ADR-0009).
        HttpResponseMessage response = await AsServer(HttpMethod.Post, "/combat/hits", new
        {
            attackerCharacterId = _intruder,
            targetCharacterId = _local,
            zone = 1,
            facing = 0,
            x = -200.0,
            y = 0.0,
            z = 0.0,
        });

        using JsonDocument body = (await response.Content.ReadFromJsonAsync<JsonDocument>())!;

        Assert.False(body.RootElement.GetProperty("landed").GetBoolean());
        Assert.Equal("not_allowed", body.RootElement.GetProperty("refusal").GetString());
    }

    [Fact]
    public async Task The_server_can_read_health_and_the_weapon_in_hand()
    {
        HttpResponseMessage response = await AsServer(HttpMethod.Get, $"/combat/{_local}");

        using JsonDocument body = (await response.Content.ReadFromJsonAsync<JsonDocument>())!;

        Assert.Equal(1_000, body.RootElement.GetProperty("health").GetInt32());
        Assert.Equal(1_000, body.RootElement.GetProperty("maxHealth").GetInt32());

        JsonElement weapon = body.RootElement.GetProperty("weapon");

        Assert.Equal("laser_rifle", weapon.GetProperty("skillKey").GetString());
        Assert.Equal(500, weapon.GetProperty("cooldownMs").GetInt32());
        Assert.Equal(150, weapon.GetProperty("rangeMetres").GetInt32());
    }

    [Fact]
    public async Task Every_hit_is_logged_with_where_its_time_went()
    {
        string folder = Path.Combine(Path.GetTempPath(), $"spacemmo-log-{Guid.NewGuid():N}");
        string log = Path.Combine(folder, "Api.log");

        try
        {
            using (var logged = new ApiFactory(_fixture.ConnectionString, ApiFactory.TestServiceSecret) { LogFile = log })
            using (HttpClient client = logged.CreateClient())
            using (var request = new HttpRequestMessage(HttpMethod.Post, new Uri("/combat/hits", UriKind.Relative)))
            {
                request.Content = JsonContent.Create(Hit(zone: 1, facing: 0));
                request.Headers.Add("X-SpaceMMO-Service", ApiFactory.TestServiceSecret);

                Assert.Equal(HttpStatusCode.OK, (await client.SendAsync(request)).StatusCode);
            }

            string line = Assert.Single(
                await File.ReadAllLinesAsync(log),
                l => l.Contains($"SpaceMMO.Combat: Hit by {_local} on {_intruder}, landed:", StringComparison.Ordinal));

            // Stamped as Unreal stamps its logs, and the parts named, each with its count of round trips.
            Match parts = Regex.Match(
                line,
                @"^\[\d{4}\.\d{2}\.\d{2}-\d{2}\.\d{2}\.\d{2}:\d{3}\] Information SpaceMMO\.Combat: .* ms in all ="
                + @" [\d.]+ reading first \((?<reads>\d+) round trips\) \+ [\d.]+ taking the lock"
                + @" \+ [\d.]+ holding it \((?<holding>\d+) round trips\) \+ connecting [\d.]+ \+ between -?[\d.]+\."
                + @" Round trips, ms: (?<trips>[\d. ]+)\.");

            Assert.True(parts.Success, line);

            int reads = int.Parse(parts.Groups["reads"].Value, CultureInfo.InvariantCulture);
            int holding = int.Parse(parts.Groups["holding"].Value, CultureInfo.InvariantCulture);
            string[] trips = parts.Groups["trips"].Value.Split(' ', StringSplitOptions.RemoveEmptyEntries);

            // The lock is one round trip; the list holds it and the others. And the API's own hit, as the game server
            // sends it, holds the lock for three (task 198): a read, the save and the commit.
            Assert.Equal(reads + 1 + holding, trips.Length);
            Assert.Equal(3, holding);
        }
        finally
        {
            Directory.Delete(folder, recursive: true);
        }
    }

    private object Hit(int zone, int facing) => new
    {
        attackerCharacterId = _local,
        targetCharacterId = _intruder,
        zone,
        facing,

        // Faction B's half: the local is the Space Orc.
        x = -200.0,
        y = 0.0,
        z = 0.0,
    };

    private async Task<HttpResponseMessage> AsServer(HttpMethod method, string path, object? body = null)
    {
        using var request = new HttpRequestMessage(method, new Uri(path, UriKind.Relative));

        if (body is not null)
        {
            request.Content = JsonContent.Create(body);
        }

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

        var local = new Character
        {
            AccountId = account.Id, Name = "Local", Race = Race.SpaceOrc, HomeBodyId = capital.Id,
            Balance = Credits.FromWholeCredits(100), CreatedAt = DateTimeOffset.UtcNow,
        };

        var intruder = new Character
        {
            AccountId = account.Id, Name = "Intruder", Race = Race.Humanoid, HomeBodyId = capital.Id,
            Balance = Credits.FromWholeCredits(100), CreatedAt = DateTimeOffset.UtcNow,
        };

        context.ItemDefs.Add(rifle);
        context.Characters.AddRange(local, intruder);
        await context.SaveChangesAsync();

        _local = local.Id;
        _intruder = intruder.Id;

        // No main-story quests exist here, so nobody is under new-player protection.
        foreach (Character fighter in new[] { local, intruder })
        {
            Inventory pockets = await new InventoryService(context).GetOrCreateCarriedAsync(fighter.Id);

            var instance = new ItemInstance
            {
                ItemDefId = rifle.Id, InventoryId = pockets.Id, Condition = 100,
                AcquisitionValue = Credits.FromWholeCredits(100), CreatedAt = DateTimeOffset.UtcNow,
            };

            context.ItemInstances.Add(instance);
            await context.SaveChangesAsync();

            await new EquipmentService(context).EquipAsync(fighter.Id, instance.Id, EquipmentSlot.Weapon1);
        }
    }
}
