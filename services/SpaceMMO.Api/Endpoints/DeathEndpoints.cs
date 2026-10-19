using SpaceMMO.Api.Auth;
using SpaceMMO.Data.Combat;
using SpaceMMO.Domain.Items;
using SpaceMMO.Domain.Universe;

namespace SpaceMMO.Api.Endpoints;

/// <summary>Where a dead character's body fell, from the game server: system kilometres, beside the body.</summary>
public sealed record DeathCostRequest(int CharacterId, double X, double Y, double Z);

/// <summary>One material a death took. <c>Style</c> is the one the goods show, as the inventory lists them.</summary>
public sealed record DeathLossResponse(
    int ItemDefId, string ItemKey, string Name, ItemStyle Style, int Dropped, int Destroyed);

/// <summary>One worn item a death wore down.</summary>
public sealed record WornResponse(long ItemInstanceId, string ItemKey, string Name, int Before, int After);

/// <summary>
/// One material in a container. <c>Style</c> is the stack's, which taking it names; <c>ShownStyle</c> the one the
/// goods show.
/// </summary>
public sealed record LootLineResponse(
    int ItemDefId, string ItemKey, string Name, ItemStyle Style, ItemStyle ShownStyle, int Quantity, double VolumeM3);

/// <summary>
/// A dropped container. <c>SecondsLeft</c> rather than a time, so the game server counts it down on its own clock,
/// which is the one its clients share.
/// </summary>
public sealed record LootContainerResponse(
    long Id,
    int DroppedByCharacterId,
    string DroppedBy,
    double X,
    double Y,
    double Z,
    double SecondsLeft,
    IReadOnlyList<LootLineResponse> Items);

/// <summary>What a death cost. <c>Applied</c> is false when nothing was owed.</summary>
public sealed record DeathCostResponse(
    bool Applied,
    bool AtStation,
    IReadOnlyList<DeathLossResponse> Lost,
    IReadOnlyList<WornResponse> Worn,
    LootContainerResponse? Container);

/// <summary>One stack to take: the item and the stack's style.</summary>
public sealed record LootWantedBody(int ItemDefId, ItemStyle Style);

/// <summary>Who is taking, and which stacks; none named takes everything that fits.</summary>
public sealed record LootRequest(int CharacterId, IReadOnlyList<LootWantedBody>? Items = null);

/// <summary>What a go at a container took. <c>Left</c> is null once it is empty.</summary>
public sealed record LootResponse(
    bool Took, string? Refusal, IReadOnlyList<LootLineResponse> Taken, LootContainerResponse? Left);

/// <summary>
/// What dying costs, and the containers the dropped half lies in (ADR-0016, task 103).
/// </summary>
/// <remarks>
/// <strong>Service only, all of it</strong>, as the rest of combat is. Where a body fell and who is standing at a
/// crate are positions, which only the game server knows (ADR-0003); a player taking from a crate goes through it,
/// as gathering does, so nobody loots one on another planet.
/// </remarks>
public static class DeathEndpoints
{
    public static void MapDeathEndpoints(this IEndpointRouteBuilder routes)
    {
        RouteGroupBuilder group = routes.MapGroup("/combat").WithTags("Combat");

        group.MapPost("/deaths", ApplyAsync);
        group.MapGet("/containers", ContainersAsync);
        group.MapPost("/containers/{containerId:long}/loot", LootAsync);
    }

    private static IResult NotTheServer() =>
        Results.Problem(
            title: "Deaths and what they drop are decided by the game server.",
            detail: "Only the simulation knows where a body fell and who is standing beside it.",
            statusCode: StatusCodes.Status401Unauthorized);

    /// <summary>Takes what a death owes, with the container beside where the body fell.</summary>
    private static async Task<IResult> ApplyAsync(
        DeathCostRequest request,
        HttpContext context,
        ServiceCredential service,
        DeathCostService deaths,
        CancellationToken cancellation)
    {
        ArgumentNullException.ThrowIfNull(request);

        if (!service.IsServiceCaller(context))
        {
            return NotTheServer();
        }

        if (!double.IsFinite(request.X) || !double.IsFinite(request.Y) || !double.IsFinite(request.Z))
        {
            return Results.BadRequest(new { error = "A death needs where the body fell." });
        }

        DeathCostResult result = await deaths.ApplyAsync(
            request.CharacterId, new SystemPosition(request.X, request.Y, request.Z), TimeProvider.System, cancellation);

        DateTimeOffset now = DateTimeOffset.UtcNow;

        return Results.Ok(new DeathCostResponse(
            result.Applied,
            result.AtStation,
            [.. result.Lost.Select(l => new DeathLossResponse(l.ItemDefId, l.ItemKey, l.Name, l.Style, l.Dropped, l.Destroyed))],
            [.. result.Worn.Select(w => new WornResponse(w.ItemInstanceId, w.ItemKey, w.Name, w.Before, w.After))],
            result.Container is { } container ? Container(container, now) : null));
    }

    /// <summary>Every container that can still be looted, for the game server to put in the world.</summary>
    private static async Task<IResult> ContainersAsync(
        HttpContext context,
        ServiceCredential service,
        DeathCostService deaths,
        CancellationToken cancellation)
    {
        if (!service.IsServiceCaller(context))
        {
            return NotTheServer();
        }

        IReadOnlyList<LootContainerView> live = await deaths.LiveContainersAsync(TimeProvider.System, cancellation);
        DateTimeOffset now = DateTimeOffset.UtcNow;

        return Results.Ok(live.Select(c => Container(c, now)).ToList());
    }

    /// <summary>Takes from a container into a looter's pockets. 200 either way: a refusal is an answer.</summary>
    private static async Task<IResult> LootAsync(
        long containerId,
        LootRequest request,
        HttpContext context,
        ServiceCredential service,
        DeathCostService deaths,
        CancellationToken cancellation)
    {
        ArgumentNullException.ThrowIfNull(request);

        if (!service.IsServiceCaller(context))
        {
            return NotTheServer();
        }

        LootResult result = await deaths.LootAsync(
            containerId,
            request.CharacterId,
            request.Items?.Select(i => new LootWanted(i.ItemDefId, i.Style)).ToList(),
            TimeProvider.System,
            cancellation);

        DateTimeOffset now = DateTimeOffset.UtcNow;

        return Results.Ok(new LootResponse(
            result.Refusal is null,
            result.Refusal,
            [.. result.Taken.Select(Line)],
            result.Left is { } left ? Container(left, now) : null));
    }

    private static LootContainerResponse Container(LootContainerView view, DateTimeOffset now) =>
        new(
            view.Id,
            view.DroppedByCharacterId,
            view.DroppedBy,
            view.Position.XKilometres,
            view.Position.YKilometres,
            view.Position.ZKilometres,
            Math.Max(0.0, (view.ExpiresAt - now).TotalSeconds),
            [.. view.Items.Select(Line)]);

    private static LootLineResponse Line(LootLine line) =>
        new(line.ItemDefId, line.ItemKey, line.Name, line.Style, line.ShownStyle, line.Quantity, line.VolumeM3);
}
