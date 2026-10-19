using System.Globalization;
using SpaceMMO.Api.Auth;
using SpaceMMO.Data.Combat;
using SpaceMMO.Domain.Combat;
using SpaceMMO.Domain.Universe;

namespace SpaceMMO.Api.Endpoints;

/// <summary>A hit the game server traced (task 194). Position is the shooter's, in system kilometres.</summary>
public sealed record HitRequestBody(
    int AttackerCharacterId,
    int TargetCharacterId,
    HitZone Zone,
    HitFacing Facing,
    double X,
    double Y,
    double Z);

/// <summary>What a hit did. <c>Refusal</c> is null when it landed.</summary>
public sealed record HitResponse(
    bool Landed,
    string? Refusal,
    int Damage,
    int TargetHealth,
    int TargetMaxHealth,
    bool Killed,
    string? WeaponSkillKey,
    long WeaponXp);

public sealed record RespawnRequest(int CharacterId);

public sealed record RespawnResponse(int StationId, string StationKey, int Health, int MaxHealth);

/// <summary>A character's health now, and what they would fire: Weapon 1, or null.</summary>
/// <param name="SecondsSinceCombat">
/// How long ago they last dealt or took damage, or null if never: so the game server can replicate where health
/// stands in its regeneration, not only what it is this instant.
/// </param>
public sealed record CombatStatusResponse(
    int CharacterId, int Health, int MaxHealth, WeaponResponse? Weapon, double? SecondsSinceCombat = null);

public sealed record WeaponResponse(
    long ItemInstanceId, string ItemKey, string Name, string SkillKey, int Damage, int CooldownMs, int RangeMetres);

/// <summary>
/// Hits, health and respawning (task 194).
/// </summary>
/// <remarks>
/// <strong>Service only, all of it.</strong> A hit is a fact about where two bodies were and which way a shot went,
/// which only the game server knows; a player claiming to have hit somebody is a player asserting a position
/// (ADR-0003), as docking's remarks put it. Health is read the same way, by the server that replicates it to
/// every client: a client sees it on the pawn, never by asking here.
/// </remarks>
public static class CombatEndpoints
{
    public static void MapCombatEndpoints(this IEndpointRouteBuilder routes)
    {
        RouteGroupBuilder group = routes.MapGroup("/combat").WithTags("Combat");

        group.MapPost("/hits", HitAsync);
        group.MapPost("/respawn", RespawnAsync);
        group.MapGet("/{characterId:int}", StatusAsync);
    }

    private static IResult NotTheServer() =>
        Results.Problem(
            title: "Combat is decided by the game server.",
            detail: "Only the simulation knows what a shot hit.",
            statusCode: StatusCodes.Status401Unauthorized);

    /// <summary>
    /// One line per hit, saying where its time went (task 198): the game server waited 0.6–11 s for hits that took
    /// 20 ms with the game closed. "Between" is the API itself, between one round trip and the next.
    /// </summary>
    private static readonly Action<ILogger, int, int, string, string, Exception?> LogHitTiming =
        LoggerMessage.Define<int, int, string, string>(
            LogLevel.Information,
            new EventId(1, nameof(LogHitTiming)),
            "Hit by {Attacker} on {Target}, {Outcome}: {Breakdown}");

    private static async Task<IResult> HitAsync(
        HitRequestBody request,
        HttpContext context,
        ServiceCredential service,
        CombatService combat,
        ILoggerFactory loggers,
        CancellationToken cancellation)
    {
        ArgumentNullException.ThrowIfNull(request);

        if (!service.IsServiceCaller(context))
        {
            return NotTheServer();
        }

        if (!double.IsFinite(request.X) || !double.IsFinite(request.Y) || !double.IsFinite(request.Z)
            || !Enum.IsDefined(request.Zone) || !Enum.IsDefined(request.Facing))
        {
            return Results.BadRequest(new { error = "A hit needs a zone, a facing and a position." });
        }

        DateTimeOffset arrived = DateTimeOffset.UtcNow;

        HitResult result = await combat.HitAsync(
            new HitRequest(
                request.AttackerCharacterId,
                request.TargetCharacterId,
                request.Zone,
                request.Facing,
                new SystemPosition(request.X, request.Y, request.Z)),

            // A clock, read once the hit holds its lock: a hit that queued is judged when it is decided (task 198).
            TimeProvider.System,
            cancellation);

        if (result.Timing is { } timing)
        {
            LogHitTiming(
                loggers.CreateLogger("SpaceMMO.Combat"),
                request.AttackerCharacterId,
                request.TargetCharacterId,
                result.Refusal ?? "landed",
                Breakdown(timing, arrived),
                null);
        }

        // 200 either way: a refused hit is an answer the server acts on (no damage, no bolt flash), not a fault.
        return Results.Ok(new HitResponse(
            result.Landed,
            result.Refusal,
            result.Damage,
            result.TargetHealth,
            result.TargetMaxHealth,
            result.Killed,
            result.WeaponSkillKey,
            result.WeaponXp));
    }

    /// <summary>
    /// Where a hit's time went, as parts that add up to the whole: <c>30.0 ms in all = 8.0 reading first (4 round
    /// trips) + 2.0 taking the lock + 6.0 holding it (2 round trips) + connecting 0.0 + between 14.0</c>, then every
    /// round trip in order and when the hit arrived.
    /// </summary>
    private static string Breakdown(HitTiming timing, DateTimeOffset arrived) =>
        string.Create(
            CultureInfo.InvariantCulture,
            $"{timing.Total.TotalMilliseconds:0.0} ms in all = {timing.Reading.TotalMilliseconds:0.0} reading first"
            + $" ({timing.ReadsBeforeLock} round trips) + {timing.Locking.TotalMilliseconds:0.0} taking the lock"
            + $" + {timing.Holding.TotalMilliseconds:0.0} holding it ({timing.HoldingRoundTrips} round trips)"
            + $" + connecting {timing.Connecting.TotalMilliseconds:0.0} + between {timing.Between.TotalMilliseconds:0.0}."
            + $" Round trips, ms: {string.Join(' ', timing.RoundTrips.Select(Milliseconds))}."
            + $" Arrived {arrived:HH.mm.ss:fff}.");

    private static string Milliseconds(TimeSpan span) =>
        span.TotalMilliseconds.ToString("0.0", CultureInfo.InvariantCulture);

    private static async Task<IResult> RespawnAsync(
        RespawnRequest request,
        HttpContext context,
        ServiceCredential service,
        CombatService combat,
        DeathCostService deaths,
        CancellationToken cancellation)
    {
        ArgumentNullException.ThrowIfNull(request);

        if (!service.IsServiceCaller(context))
        {
            return NotTheServer();
        }

        // A death the game server never costed -- it crashed, or the call failed -- is costed now, where they were
        // last seen, so no death goes free (task 103). Nothing, when it was.
        await deaths.ApplyAsync(request.CharacterId, null, TimeProvider.System, cancellation);

        try
        {
            RespawnResult result = await combat.RespawnAsync(request.CharacterId, DateTimeOffset.UtcNow, cancellation);

            return Results.Ok(new RespawnResponse(result.StationId, result.StationKey, result.Health, result.MaxHealth));
        }
        catch (RespawnException error)
        {
            return Results.Conflict(new { error = error.Message, reason = error.Reason });
        }
    }

    private static async Task<IResult> StatusAsync(
        int characterId,
        HttpContext context,
        ServiceCredential service,
        CombatService combat,
        CancellationToken cancellation)
    {
        if (!service.IsServiceCaller(context))
        {
            return NotTheServer();
        }

        DateTimeOffset now = DateTimeOffset.UtcNow;

        CombatStatus? status = await combat.StatusAsync(characterId, now, cancellation);

        if (status is null)
        {
            return Results.Problem(title: "No such character.", statusCode: StatusCodes.Status404NotFound);
        }

        WeaponResponse? weapon = status.Weapon is { } w
            ? new WeaponResponse(w.ItemInstanceId, w.ItemKey, w.Name, w.SkillKey, w.Damage, w.CooldownMs, w.RangeMetres)
            : null;

        double? since = status.LastCombatAt is { } at ? Math.Max(0.0, (now - at).TotalSeconds) : null;

        return Results.Ok(new CombatStatusResponse(characterId, status.Health, status.MaxHealth, weapon, since));
    }
}
