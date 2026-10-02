using System.Net;
using System.Net.Http.Json;
using SpaceMMO.Api.Endpoints;
using SpaceMMO.Data;
using SpaceMMO.Data.Entities;
using SpaceMMO.Domain.Characters;
using SpaceMMO.Domain.Gathering;
using SpaceMMO.Domain.Items;
using SpaceMMO.Domain.Progression;
using SpaceMMO.Domain.Universe;
using Xunit;

namespace SpaceMMO.Api.Tests;

/// <summary>
/// The world endpoints a client reads to place things it must not invent positions for.
/// </summary>
/// <remarks>
/// These are the seam where "content decides where the ore is" becomes true in practice. If the
/// direction the client receives is not the direction content authored, the deposit will render
/// somewhere the server does not think it is, and every gather attempt against it will be refused
/// for a reason no one can see.
/// </remarks>
[Collection(SharedApiDatabase.Name)]
public sealed class WorldEndpointTests(ApiDatabaseFixture fixture) : IAsyncLifetime, IDisposable
{
    private readonly ApiDatabaseFixture _fixture = fixture;

    private ApiFactory _factory = null!;
    private HttpClient _client = null!;

    private int _bodyId;
    private int _otherBodyId;

    public async Task InitializeAsync()
    {
        await _fixture.ResetAsync();

        _factory = new ApiFactory(_fixture.ConnectionString);
        _client = _factory.CreateClient();

        await AuthorizationTests.SeedStartingWorldAsync(_fixture);
        await SeedDepositsAsync();
    }

    public Task DisposeAsync() => Task.CompletedTask;

    public void Dispose()
    {
        _client?.Dispose();
        _factory?.Dispose();
    }

    [Fact]
    public async Task Bodies_are_readable_without_signing_in()
    {
        // Deliberately no token. Where the planets are is not a secret, and requiring a login
        // would only mean everyone reads it with one.
        HttpResponseMessage response = await _client.GetAsync("/world/bodies");

        Assert.Equal(HttpStatusCode.OK, response.StatusCode);

        BodyResponse[] bodies =
            (await response.Content.ReadFromJsonAsync<BodyResponse[]>())!;

        Assert.NotEmpty(bodies);
        Assert.All(bodies, body => Assert.True(body.RadiusKm > 0.0));
    }

    [Fact]
    public async Task A_bodys_system_position_is_served_when_it_has_one()
    {
        // The wire half of task 157. A body with no position is why the client had one planet and
        // skipped four of the six seeded stations: it could not draw a world it did not know the
        // location of, so those stations had nothing to stand on.
        await using (SpaceMmoDbContext context = _fixture.CreateContext())
        {
            Body placed = context.Bodies.OrderBy(b => b.Id).First();

            // Three different values, so a response that served one component three times -- or
            // read Y where it meant Z -- fails rather than passing on symmetry.
            placed.SystemX = -120.0;
            placed.SystemY = 60.5;
            placed.SystemZ = 7.25;

            await context.SaveChangesAsync();
        }

        BodyResponse[] bodies =
            (await _client.GetFromJsonAsync<BodyResponse[]>("/world/bodies"))!;

        BodyResponse served = bodies.Single(b => b.SystemX == -120.0);

        Assert.Equal(60.5, served.SystemY);
        Assert.Equal(7.25, served.SystemZ);
    }

    [Fact]
    public async Task A_body_nobody_has_placed_is_served_with_no_position()
    {
        // Unplaced has to arrive as null rather than as the origin. A body silently at (0,0,0)
        // would be drawn inside the star, and the client cannot tell that from a world somebody
        // deliberately put there.
        BodyResponse[] bodies =
            (await _client.GetFromJsonAsync<BodyResponse[]>("/world/bodies"))!;

        Assert.All(bodies, body =>
        {
            Assert.Null(body.SystemX);
            Assert.Null(body.SystemY);
            Assert.Null(body.SystemZ);
        });
    }

    [Fact]
    public async Task A_station_that_levels_ground_is_served_with_its_body()
    {
        // Task 168. The pad has to arrive with the body, because the client shapes a planet from the
        // bodies response before anything is placed on it (task 129) -- and centred on the station's
        // own direction, which is the only one content authors.
        await using (SpaceMmoDbContext context = _fixture.CreateContext())
        {
            Station ground = context.Stations.Single(s => s.Key == "station_test_ground");
            ground.PadFlatRadiusKm = 0.42;
            ground.PadBlendKm = 0.15;
            ground.PadElevationKm = 0.172;

            await context.SaveChangesAsync();
        }

        BodyResponse[] bodies =
            (await _client.GetFromJsonAsync<BodyResponse[]>("/world/bodies"))!;

        BodyResponse levelled = bodies.Single(b => b.Id == _bodyId);
        TerrainPadResponse pad = Assert.Single(levelled.TerrainPads);

        Assert.Equal("station_test_ground", pad.StationKey);
        Assert.Equal(0.42, pad.FlatRadiusKm);
        Assert.Equal(0.15, pad.BlendKm);
        Assert.Equal(0.172, pad.ElevationKm);

        // The station's direction, component by component, so a swapped axis fails.
        await using SpaceMmoDbContext verify = _fixture.CreateContext();
        Station stored = verify.Stations.Single(s => s.Key == "station_test_ground");

        Assert.Equal(stored.DirectionX, pad.DirectionX);
        Assert.Equal(stored.DirectionY, pad.DirectionY);
        Assert.Equal(stored.DirectionZ, pad.DirectionZ);

        // And nowhere else: a pad served under the wrong body would level some other world's ground.
        Assert.All(bodies.Where(b => b.Id != _bodyId), b => Assert.Empty(b.TerrainPads));
    }

    [Fact]
    public async Task Deposits_are_returned_with_their_direction()
    {
        ResourceNodeResponse node =
            Assert.Single(await GetNodesAsync(), n => n.Key == "node_test_a");

        Assert.Equal(_bodyId, node.BodyId);
        Assert.Equal("ferrite_ore", node.ItemKey);
        Assert.Equal("mining", node.SkillKey);
        Assert.Equal(200, node.QuantityMax);
    }

    /// <summary>
    /// The direction served is a unit vector.
    /// </summary>
    /// <remarks>
    /// The client turns this straight into a surface point by scaling it by the altitude the
    /// terrain function reports. A direction of any other length would silently move the deposit
    /// off the surface — above it or inside it — with nothing in the response looking wrong.
    /// </remarks>
    [Fact]
    public async Task A_served_direction_is_normalised()
    {
        ResourceNodeResponse node =
            Assert.Single(await GetNodesAsync(), n => n.Key == "node_test_a");

        double length = Math.Sqrt(
            (node.DirectionX * node.DirectionX)
            + (node.DirectionY * node.DirectionY)
            + (node.DirectionZ * node.DirectionZ));

        Assert.Equal(1.0, length, 9);
    }

    [Fact]
    public async Task Deposits_on_every_body_are_returned_together_each_naming_its_own()
    {
        // The wire half of task 158. This route was per body, and the test standing here said the
        // client "only ever renders one at a time" -- true until content placed five bodies (task
        // 157), after which four of them were reachable worlds with no ore on them, because the
        // client asked for one body's deposits and the route could answer for no more.
        //
        // Two bodies with one deposit each, so a response filtered to either -- or one serving
        // every node under the first body's id -- fails rather than passing on the single-body
        // fixture the old test used.
        ResourceNodeResponse[] nodes = await GetNodesAsync();

        ResourceNodeResponse first = Assert.Single(nodes, n => n.Key == "node_test_a");
        ResourceNodeResponse second = Assert.Single(nodes, n => n.Key == "node_test_b");

        Assert.Equal(_bodyId, first.BodyId);
        Assert.Equal(_otherBodyId, second.BodyId);
        Assert.NotEqual(first.BodyId, second.BodyId);
    }

    [Fact]
    public async Task Stations_are_returned_with_whichever_position_they_have()
    {
        HttpResponseMessage response = await _client.GetAsync("/world/stations");

        Assert.Equal(HttpStatusCode.OK, response.StatusCode);

        StationResponse[] stations =
            (await response.Content.ReadFromJsonAsync<StationResponse[]>())!;

        Assert.NotEmpty(stations);

        // Placed on a body means a direction and no system position; placed in deep space means
        // the reverse. A station carrying both would be one the client could draw in two places,
        // and whichever it read first would win.
        foreach (StationResponse station in stations.Where(s => s.DirectionX is not null))
        {
            Assert.NotNull(station.BodyId);
            Assert.Null(station.SystemX);
        }

        foreach (StationResponse station in stations.Where(s => s.SystemX is not null))
        {
            Assert.Null(station.BodyId);
            Assert.Null(station.DirectionX);
        }

        // Docking range has to be usable as sent. Zero would be a station nobody can reach, which
        // is how an unmigrated column would present.
        Assert.All(stations, s => Assert.True(s.DockingRangeKm > 0.0));
    }

    private async Task<ResourceNodeResponse[]> GetNodesAsync()
    {
        // Deliberately no token, like the bodies: the dedicated server places the world's ore
        // without holding any player's session.
        HttpResponseMessage response = await _client.GetAsync("/world/nodes");

        Assert.Equal(HttpStatusCode.OK, response.StatusCode);

        return (await response.Content.ReadFromJsonAsync<ResourceNodeResponse[]>())!;
    }

    private async Task SeedDepositsAsync()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        Body body = context.Bodies.OrderBy(b => b.Id).First();
        Body other = context.Bodies.OrderBy(b => b.Id).Skip(1).First();

        _bodyId = body.Id;
        _otherBodyId = other.Id;

        var item = new ItemDef
        {
            Key = "ferrite_ore",
            Name = "Ferrite Ore",
            Category = ItemCategory.Raw,
            VolumeM3 = 0.1,
        };

        var skill = new Skill
        {
            Key = "mining",
            Name = "Mining",
            Category = SkillCategory.Life,
        };

        context.ItemDefs.Add(item);
        context.Skills.Add(skill);
        await context.SaveChangesAsync();

        // Stored already normalised, as the content loader does. The endpoint serves what is
        // stored — it is the loader's job to normalise, not the reader's.
        var direction = new[] { -1.0, 0.02, 0.0 };
        double length = Math.Sqrt(direction.Sum(c => c * c));

        context.ResourceNodes.Add(new ResourceNode
        {
            Key = "node_test_a",
            StarSystemId = body.StarSystemId,
            BodyId = body.Id,
            ItemDefId = item.Id,
            SkillId = skill.Id,
            RequiredLevel = 1,
            QuantityMax = 200,
            RespawnSeconds = 1200,
            DirectionX = direction[0] / length,
            DirectionY = direction[1] / length,
            DirectionZ = direction[2] / length,
            SharingModel = NodeSharingModel.Shared,
        });

        // A second deposit on a second body, so the route is read against a system with ore in
        // more than one place -- the state task 158 was about, and one the fixture never had.
        context.ResourceNodes.Add(new ResourceNode
        {
            Key = "node_test_b",
            StarSystemId = other.StarSystemId,
            BodyId = other.Id,
            ItemDefId = item.Id,
            SkillId = skill.Id,
            RequiredLevel = 15,
            QuantityMax = 150,
            RespawnSeconds = 1800,
            DirectionX = 0.0,
            DirectionY = 0.0,
            DirectionZ = 1.0,
            SharingModel = NodeSharingModel.Shared,
        });

        // One of each kind of station, so the endpoint is read against both branches rather than
        // only the one the shipped content happens to use most.
        context.Stations.Add(new Station
        {
            Key = "station_test_ground",
            Name = "Test Outpost",
            StarSystemId = body.StarSystemId,
            BodyId = body.Id,
            Kind = StationKind.TradingHub,
            DirectionX = direction[0] / length,
            DirectionY = direction[1] / length,
            DirectionZ = direction[2] / length,
            DockingRangeKilometres = 5.0,
        });

        context.Stations.Add(new Station
        {
            Key = "station_test_deep",
            Name = "Test Deepdock",
            StarSystemId = body.StarSystemId,
            BodyId = null,
            Kind = StationKind.Spaceport,
            SystemX = 30.0,
            SystemY = 12.0,
            SystemZ = 4.0,
            DockingRangeKilometres = 8.0,
        });

        await context.SaveChangesAsync();
    }
}
