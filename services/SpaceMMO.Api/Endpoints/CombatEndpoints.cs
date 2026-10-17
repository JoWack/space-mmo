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

    private static async Task<IResult> HitAsync(
        HitRequestBody request,
        HttpContext context,
        ServiceCredential service,
        CombatService combat,
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

        HitResult result = await combat.HitAsync(
            new HitRequest(
                request.AttackerCharacterId,
                request.TargetCharacterId,
                request.Zone,
                request.Facing,
                new SystemPosition(request.X, request.Y, request.Z)),
            DateTimeOffset.UtcNow,
            cancellation);

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

    private static async Task<IResult> RespawnAsync(
        RespawnRequest request,
        HttpContext context,
        ServiceCredential service,
        CombatService combat,
        CancellationToken cancellation)
    {
        ArgumentNullException.ThrowIfNull(request);

        if (!service.IsServiceCaller(context))
        {
            return NotTheServer();
        }

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
