using Microsoft.EntityFrameworkCore;
using SpaceMMO.Data;

namespace SpaceMMO.Api.Endpoints;

/// <param name="LowColour">
/// What this body's ground looks like, or null throughout when nobody has painted it. Sent with the
/// body because a planet's look is content, the same as its radius — see BodyAppearanceContent.
/// </param>
public sealed record BodyResponse(
    int Id,
    string Key,
    string Name,
    int StarSystemId,
    double RadiusKm,
    string? LowColour,
    string? HighColour,
    string? RockColour,
    double? HeightFrom,
    double? HeightTo,
    double? SlopeFrom,
    double? SlopeTo,
    long? TerrainSeed,
    double? MaxElevationKm,
    double? BaseFrequency);

/// <param name="RequiredToolName">
/// Display name of the tool this deposit needs, or null for bare hands. Sent so a player can be
/// told what a rock demands before they swing at it: the refusal is correct but arrives after the
/// fact, and "you need a Crude Mining Laser" is worth knowing while there is still time to go and
/// craft one.
/// </param>
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
    double DirectionZ,
    string? RequiredToolKey = null,
    string? RequiredToolName = null);

/// <summary>
/// A station, and whichever way it is placed.
/// </summary>
/// <remarks>
/// Both position forms are on one record with nulls rather than split into two shapes, because a
/// client asking "where can I dock" wants one list. Exactly one of the pair is ever set — the
/// content validator refuses both, and refuses a direction without a body for it to be from.
/// </remarks>
public sealed record StationResponse(
    int Id,
    string Key,
    string Name,
    int StarSystemId,
    int? BodyId,
    string Kind,
    double? DirectionX,
    double? DirectionY,
    double? DirectionZ,
    double? SystemX,
    double? SystemY,
    double? SystemZ,
    double DockingRangeKm);

/// <summary>
/// The shape of the world: bodies, the deposits on them, and the stations you can dock at.
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
        group.MapGet("/stations", StationsAsync);
    }

    /// <summary>
    /// Every station in the system, with whichever position it has.
    /// </summary>
    /// <remarks>
    /// All of them at once rather than per body, because the ones that matter most to a pilot are
    /// the ones attached to no body at all, and a route keyed by body could never return those.
    /// A station with no position is still listed: the client draws nothing for it and docking
    /// refuses it, which is a station visibly missing rather than one silently absent.
    /// </remarks>
    private static async Task<IResult> StationsAsync(
        SpaceMmoDbContext database, CancellationToken cancellation)
    {
        List<StationResponse> stations = await database.Stations
            .OrderBy(s => s.Key)
            .Select(s => new StationResponse(
                s.Id,
                s.Key,
                s.Name,
                s.StarSystemId,
                s.BodyId,
                s.Kind.ToString(),
                s.DirectionX,
                s.DirectionY,
                s.DirectionZ,
                s.SystemX,
                s.SystemY,
                s.SystemZ,
                s.DockingRangeKilometres))
            .ToListAsync(cancellation);

        return Results.Ok(stations);
    }

    private static async Task<IResult> BodiesAsync(
        SpaceMmoDbContext database, CancellationToken cancellation)
    {
        List<BodyResponse> bodies = await database.Bodies
            .OrderBy(b => b.Key)
            .Select(b => new BodyResponse(
                b.Id,
                b.Key,
                b.Name,
                b.StarSystemId,
                b.RadiusKm,
                b.LowColour,
                b.HighColour,
                b.RockColour,
                b.HeightFrom,
                b.HeightTo,
                b.SlopeFrom,
                b.SlopeTo,
                b.TerrainSeed,
                b.MaxElevationKm,
                b.BaseFrequency))
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
            .Include(n => n.RequiredToolItemDef)
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
                n.DirectionZ,
                n.RequiredToolItemDef!.Key,
                n.RequiredToolItemDef.Name))
            .ToListAsync(cancellation);

        return Results.Ok(nodes);
    }
}
