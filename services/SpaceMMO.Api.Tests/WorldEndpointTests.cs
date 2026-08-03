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
    public async Task Deposits_on_a_body_are_returned_with_their_direction()
    {
        ResourceNodeResponse[] nodes = await GetNodesAsync(_bodyId);

        ResourceNodeResponse node = Assert.Single(nodes);

        Assert.Equal("node_test_a", node.Key);
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
        ResourceNodeResponse node = Assert.Single(await GetNodesAsync(_bodyId));

        double length = Math.Sqrt(
            (node.DirectionX * node.DirectionX)
            + (node.DirectionY * node.DirectionY)
            + (node.DirectionZ * node.DirectionZ));

        Assert.Equal(1.0, length, 9);
    }

    [Fact]
    public async Task Deposits_on_another_body_are_not_returned()
    {
        // The client asks per body because it only ever renders one at a time. A query that
        // leaked every deposit in the system would work in testing and fall over at scale.
        ResourceNodeResponse[] nodes = await GetNodesAsync(_otherBodyId);

        Assert.Empty(nodes);
    }

    [Fact]
    public async Task A_body_that_does_not_exist_has_no_deposits_rather_than_failing()
    {
        HttpResponseMessage response = await _client.GetAsync("/world/bodies/999999/nodes");

        // An empty list, not a 404. "That body has nothing on it" and "there is no such body"
        // are the same answer to a client deciding what to draw, and distinguishing them would
        // make the endpoint an oracle for which body ids are real.
        Assert.Equal(HttpStatusCode.OK, response.StatusCode);
        Assert.Empty((await response.Content.ReadFromJsonAsync<ResourceNodeResponse[]>())!);
    }

    private async Task<ResourceNodeResponse[]> GetNodesAsync(int bodyId)
    {
        HttpResponseMessage response = await _client.GetAsync($"/world/bodies/{bodyId}/nodes");

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

        await context.SaveChangesAsync();
    }
}
