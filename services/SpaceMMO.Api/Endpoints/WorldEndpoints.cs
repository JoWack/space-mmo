using Microsoft.EntityFrameworkCore;
using SpaceMMO.Data;

namespace SpaceMMO.Api.Endpoints;

public sealed record BodyResponse(
    int Id,
    string Key,
    string Name,
    int StarSystemId,
    double RadiusKm);

public sealed record ResourceNodeResponse(
    long Id,
    string Key,
    int BodyId,
    string ItemKey,
    string ItemName,
    string SkillKey,
    int RequiredLevel,
    int QuantityMax,
    double DirectionX,
    double DirectionY,
    double DirectionZ);

/// <summary>
/// The shape of the world: bodies, and the deposits on them.
/// </summary>
/// <remarks>
/// <para>
/// Read-only and unauthenticated, like the market book. Where a planet is and where its ore is are
/// things every player is meant to know, and putting them behind a login would only mean everyone
/// scrapes them with one.
/// </para>
/// <para>
/// <strong>The client asks rather than deciding.</strong> A deposit's position could be generated
/// client-side from a seed, and that would even be cheaper — but then two clients could disagree
/// about where the ore is, and the server would have no opinion at all. Positions live in content,
/// the server serves them, and the client places what it is told.
/// </para>
/// </remarks>
public static class WorldEndpoints
{
    public static void MapWorldEndpoints(this IEndpointRouteBuilder routes)
    {
        RouteGroupBuilder group = routes.MapGroup("/world").WithTags("World");

        group.MapGet("/bodies", BodiesAsync);
        group.MapGet("/bodies/{bodyId:int}/nodes", NodesAsync);
    }

    private static async Task<IResult> BodiesAsync(
        SpaceMmoDbContext database, CancellationToken cancellation)
    {
        List<BodyResponse> bodies = await database.Bodies
            .OrderBy(b => b.Key)
            .Select(b => new BodyResponse(b.Id, b.Key, b.Name, b.StarSystemId, b.RadiusKm))
            .ToListAsync(cancellation);

        return Results.Ok(bodies);
    }

    /// <summary>
    /// Every deposit on a body, with the direction from its centre that locates it.
    /// </summary>
    /// <remarks>
    /// Direction only, never a position. How high the ground is at that direction is a question the
    /// terrain function already answers on both sides, and sending an altitude would be a second
    /// answer free to disagree with it — the same mistake as letting a mesh diverge from the height
    /// field.
    /// </remarks>
    private static async Task<IResult> NodesAsync(
        int bodyId, SpaceMmoDbContext database, CancellationToken cancellation)
    {
        List<ResourceNodeResponse> nodes = await database.ResourceNodes
            .Where(n => n.BodyId == bodyId)
            .Include(n => n.ItemDef)
            .Include(n => n.Skill)
            .OrderBy(n => n.Key)
            .Select(n => new ResourceNodeResponse(
                n.Id,
                n.Key,
                n.BodyId,
                n.ItemDef!.Key,
                n.ItemDef.Name,
                n.Skill!.Key,
                n.RequiredLevel,
                n.QuantityMax,
                n.DirectionX,
                n.DirectionY,
                n.DirectionZ))
            .ToListAsync(cancellation);

        return Results.Ok(nodes);
    }
}
