using Microsoft.EntityFrameworkCore;
using SpaceMMO.Api.Auth;
using SpaceMMO.Data;
using SpaceMMO.Data.Entities;
using SpaceMMO.Domain.Characters;
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

public sealed record InventoryItemResponse(int ItemDefId, string ItemKey, string Name, int Quantity);

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

        var character = new Character
        {
            AccountId = accountId.Value,
            Name = name,
            Race = request.Race,
            HomeBodyId = home.Id,
            CreatedAt = DateTimeOffset.UtcNow,
        };

        database.Characters.Add(character);
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
    /// A character's skills.
    /// </summary>
    /// <remarks>
    /// XP is stored; level is derived through <see cref="SkillCurve"/> on read. Storing both
    /// would allow a row whose level disagrees with its XP, and there is no version of that bug
    /// that is not player-visible.
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

        var rows = await database.CharacterSkills
            .Where(cs => cs.CharacterId == characterId)
            .Include(cs => cs.Skill)
            .OrderBy(cs => cs.Skill!.Key)
            .Select(cs => new { cs.Skill!.Key, cs.Skill.Name, cs.Skill.Category, cs.Xp })
            .ToListAsync(cancellation);

        return Results.Ok(rows
            .Select(r => new SkillResponse(r.Key, r.Name, r.Category, r.Xp, SkillCurve.LevelForXp(r.Xp)))
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
                ii.ItemDefId, ii.ItemDef!.Key, ii.ItemDef.Name, ii.Quantity))
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
