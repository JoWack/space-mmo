using Microsoft.EntityFrameworkCore;
using SpaceMMO.Api.Auth;
using SpaceMMO.Data;
using SpaceMMO.Data.Entities;
using SpaceMMO.Domain.Characters;
using SpaceMMO.Domain.Economy;
using SpaceMMO.Domain.Progression;

namespace SpaceMMO.Api.Endpoints;

public sealed record CreateCharacterRequest(string Name, Race Race);

public sealed record CharacterResponse(
    int Id,
    string Name,
    Race Race,
    Faction Faction,
    int HomeBodyId,
    long BalanceMinorUnits);

public sealed record SkillResponse(string Key, string Name, SkillCategory Category, long Xp, int Level);

/// <param name="FactionBuyPriceMinorUnits">
/// What a faction standing order pays per unit, or null if none buys it. Carried on the stack so a
/// client can tell at a glance what it could turn into credits without a second request per item.
/// </param>
public sealed record InventoryItemResponse(
    int ItemDefId,
    string ItemKey,
    string Name,
    int Quantity,
    long? FactionBuyPriceMinorUnits);

/// <summary>
/// Character creation and read-only views of a character's progression and holdings.
/// </summary>
public static class CharacterEndpoints
{
    public static void MapCharacterEndpoints(this IEndpointRouteBuilder routes)
    {
        RouteGroupBuilder group = routes.MapGroup("/characters").WithTags("Characters");

        group.MapPost("/", CreateAsync);
        group.MapGet("/", ListAsync);
        group.MapGet("/{characterId:int}/skills", SkillsAsync);
        group.MapGet("/{characterId:int}/inventory", InventoryAsync);
    }

    /// <summary>
    /// Creates a character, deriving faction and starting planet from race.
    /// </summary>
    /// <remarks>
    /// Faction and home body are never taken from the request. They are functions of race
    /// (<see cref="Races"/>), so a client cannot ask for a Space Orc that starts on Terra.
    /// </remarks>
    private static async Task<IResult> CreateAsync(
        CreateCharacterRequest request,
        HttpContext context,
        Caller caller,
        SpaceMmoDbContext database,
        CancellationToken cancellation)
    {
        int? accountId = caller.AccountId(context);

        if (accountId is null)
        {
            return Results.Unauthorized();
        }

        if (string.IsNullOrWhiteSpace(request.Name) || request.Name.Trim().Length is < 3 or > 20)
        {
            return Results.ValidationProblem(new Dictionary<string, string[]>
            {
                ["name"] = ["Name must be between 3 and 20 characters."],
            });
        }

        if (!Enum.IsDefined(request.Race))
        {
            return Results.ValidationProblem(new Dictionary<string, string[]>
            {
                ["race"] = ["Unknown race."],
            });
        }

        string name = request.Name.Trim();

        if (await database.Characters.AnyAsync(c => c.Name == name, cancellation))
        {
            return Results.Conflict(new { error = "That character name is taken." });
        }

        string homeBodyKey = Races.HomeBodyKeyFor(request.Race);

        Body? home = await database.Bodies
            .SingleOrDefaultAsync(b => b.Key == homeBodyKey, cancellation);

        if (home is null)
        {
            // Content is missing, not the caller's mistake. A 400 here would send a player looking
            // for a problem with their request that does not exist.
            return Results.Problem(
                $"Starting body '{homeBodyKey}' is not seeded.", statusCode: StatusCodes.Status500InternalServerError);
        }

        DateTimeOffset now = DateTimeOffset.UtcNow;

        var character = new Character
        {
            AccountId = accountId.Value,
            Name = name,
            Race = request.Race,
            HomeBodyId = home.Id,
            CreatedAt = now,
            Balance = Economy.StartingStake,
        };

        database.Characters.Add(character);
        await database.SaveChangesAsync(cancellation);

        // Written through the ledger like every other credit movement (ADR-0005). A balance that
        // appeared without an entry would make the books irreconcilable from the very first row,
        // and reconciliation is the one property that catches a dupe before players do.
        database.LedgerEntries.Add(new LedgerEntry
        {
            CharacterId = character.Id,
            DeltaCredits = Economy.StartingStake,
            Reason = LedgerReason.StartingStake,
            CreatedAt = now,
        });

        await database.SaveChangesAsync(cancellation);

        return Results.Created($"/characters/{character.Id}", ToResponse(character));
    }

    private static async Task<IResult> ListAsync(
        HttpContext context,
        Caller caller,
        SpaceMmoDbContext database,
        CancellationToken cancellation)
    {
        int? accountId = caller.AccountId(context);

        if (accountId is null)
        {
            return Results.Unauthorized();
        }

        List<Character> characters = await database.Characters
            .Where(c => c.AccountId == accountId.Value)
            .OrderBy(c => c.Id)
            .ToListAsync(cancellation);

        return Results.Ok(characters.Select(ToResponse).ToList());
    }

    /// <summary>
    /// A character's skills — <em>every</em> skill, including ones they have never trained.
    /// </summary>
    /// <remarks>
    /// <para>
    /// XP rows are created lazily, on the first award. That is right for storage — a fresh
    /// character would otherwise get a row per skill, all of them zero — but it is wrong for
    /// reading, because a new player's skill panel would be empty until they happened to gather
    /// something. In a game where everyone has every skill at level 1 from creation, the absence
    /// of a row means zero XP, not the absence of a skill.
    /// </para>
    /// <para>
    /// So the catalog is the source of truth here and stored XP is joined onto it. A skill added
    /// to <c>data/skills/</c> then appears for every existing character with no backfill.
    /// </para>
    /// <para>
    /// Level is derived through <see cref="SkillCurve"/> on read rather than stored. Storing both
    /// would allow a row whose level disagrees with its XP, and there is no version of that bug
    /// that is not player-visible.
    /// </para>
    /// </remarks>
    private static async Task<IResult> SkillsAsync(
        int characterId,
        HttpContext context,
        Caller caller,
        SpaceMmoDbContext database,
        CancellationToken cancellation)
    {
        OwnershipResult owned = await caller.OwnedCharacterAsync(context, characterId, cancellation);

        if (owned.Status != OwnershipStatus.Owned)
        {
            return owned.ToProblem();
        }

        var rows = await database.Skills
            .OrderBy(s => s.Key)
            .Select(s => new
            {
                s.Key,
                s.Name,
                s.Category,
                Xp = database.CharacterSkills
                    .Where(cs => cs.CharacterId == characterId && cs.SkillId == s.Id)
                    .Select(cs => (long?)cs.Xp)
                    .FirstOrDefault(),
            })
            .ToListAsync(cancellation);

        return Results.Ok(rows
            .Select(r =>
            {
                long xp = r.Xp ?? 0;

                return new SkillResponse(r.Key, r.Name, r.Category, xp, SkillCurve.LevelForXp(xp));
            })
            .ToList());
    }

    private static async Task<IResult> InventoryAsync(
        int characterId,
        HttpContext context,
        Caller caller,
        SpaceMmoDbContext database,
        CancellationToken cancellation)
    {
        OwnershipResult owned = await caller.OwnedCharacterAsync(context, characterId, cancellation);

        if (owned.Status != OwnershipStatus.Owned)
        {
            return owned.ToProblem();
        }

        List<InventoryItemResponse> items = await database.InventoryItems
            .Where(ii => ii.Inventory!.CharacterId == characterId)
            .Include(ii => ii.ItemDef)
            .OrderBy(ii => ii.ItemDef!.Key)
            .Select(ii => new InventoryItemResponse(
                ii.ItemDefId,
                ii.ItemDef!.Key,
                ii.ItemDef.Name,
                ii.Quantity,
                ii.ItemDef.FactionBuyPrice != null
                    ? ii.ItemDef.FactionBuyPrice!.Value.MinorUnits
                    : null))
            .ToListAsync(cancellation);

        return Results.Ok(items);
    }

    private static CharacterResponse ToResponse(Character character) => new(
        character.Id,
        character.Name,
        character.Race,
        Races.FactionFor(character.Race),
        character.HomeBodyId,
        character.Balance.MinorUnits);
}
