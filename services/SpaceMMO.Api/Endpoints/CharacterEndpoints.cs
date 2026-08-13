using Microsoft.EntityFrameworkCore;
using SpaceMMO.Api.Auth;
using SpaceMMO.Data;
using SpaceMMO.Data.Docking;
using SpaceMMO.Data.Entities;
using SpaceMMO.Data.Inventories;
using SpaceMMO.Domain.Characters;
using SpaceMMO.Domain.Economy;
using SpaceMMO.Domain.Items;
using SpaceMMO.Domain.Progression;

namespace SpaceMMO.Api.Endpoints;

public sealed record CreateCharacterRequest(string Name, Race Race);

/// <summary>Moving a quantity of a stackable item between two of one character's containers.</summary>
public sealed record TransferRequest(
    long FromInventoryId, long ToInventoryId, int ItemDefId, int Quantity);

/// <summary>Moving one non-stackable item, which carries its own condition and value.</summary>
public sealed record TransferInstanceRequest(long ItemInstanceId, long ToInventoryId);

public sealed record CharacterResponse(
    int Id,
    string Name,
    Race Race,
    Faction Faction,
    int HomeBodyId,
    long BalanceMinorUnits);

/// <param name="XpToNextLevel">
/// XP still needed to reach the next level, or 0 at the cap.
/// </param>
/// <param name="ProgressToNextLevel">
/// How far through the current level, 0 to 1, for a progress bar. 1 at the cap.
/// </param>
/// <remarks>
/// Progress is served rather than derived on the client because the XP curve is game rules.
/// <see cref="SkillCurve"/> is deliberate about the order its floors are applied — the comment on
/// its threshold table says doing the division and the accumulation the other way round produces
/// different values at some levels — and a second implementation in C++ would reproduce that
/// subtlety or silently disagree with it. The same reasoning ADR-0002 applies to the height
/// function applies here: one implementation, on the side that owns the rule.
/// </remarks>
public sealed record SkillResponse(
    string Key,
    string Name,
    SkillCategory Category,
    long Xp,
    int Level,
    long XpToNextLevel,
    double ProgressToNextLevel);

/// <param name="FactionBuyPriceMinorUnits">
/// What a faction standing order pays per unit, or null if none buys it. Carried on the stack so a
/// client can tell at a glance what it could turn into credits without a second request per item.
/// </param>
/// <param name="Kind">Which sort of container this stack is in.</param>
/// <param name="StationId">
/// Where it is, for a station hangar; null for a ship hold, which is wherever the ship is.
/// </param>
/// <remarks>
/// <strong>A stack now says where it is, which it did not.</strong> The query returns every
/// inventory a character owns, so a player holding ore in a ship and more at a station got two rows
/// that were indistinguishable — and selling needs to know, because an order can only be placed
/// against goods at the station it is placed at. It was a latent duplicate while everything was
/// happening at one station; the market makes it load-bearing.
/// </remarks>
public sealed record InventoryItemResponse(
    int ItemDefId,
    string ItemKey,
    string Name,
    int Quantity,
    long? FactionBuyPriceMinorUnits,
    InventoryKind Kind,
    int? StationId);

/// <summary>
/// One item that does not stack — a tool, a weapon, a hull — with the state that makes it itself.
/// </summary>
/// <param name="Condition">0 to 100. Below a threshold the item is unusable until repaired.</param>
/// <remarks>
/// Separate from <see cref="InventoryItemResponse"/> rather than folded into it with nullable
/// fields, because these are not stacks and pretending otherwise loses the distinction that
/// matters: two lasers at different condition are two things, and a quantity of 2 says they are
/// one. ADR-0006 pays insurance against acquisition value per instance, so the difference is
/// load-bearing rather than cosmetic.
/// </remarks>
public sealed record ItemInstanceResponse(
    long Id,
    int ItemDefId,
    string ItemKey,
    string Name,
    int Condition,
    InventoryKind Kind,
    int? StationId);

/// <summary>
/// Everything a character owns, in the two shapes owning something can take.
/// </summary>
/// <remarks>
/// <strong>This used to be a bare array of stacks, and so half of what a player owned was
/// invisible.</strong> Every category carrying condition — tools, modules, armour, weapons, hulls
/// — is an <c>ItemInstance</c> rather than a stack, so a player could craft the mining laser the
/// onboarding questline exists to give them and see nothing anywhere: owned, usable, and absent
/// from their own inventory, which reads exactly like a craft that silently failed.
///
/// One response rather than a second endpoint, so a client cannot show half a player's possessions
/// and believe it is finished — which is the bug being fixed, one layer up.
/// </remarks>
public sealed record InventoryResponse(
    IReadOnlyList<InventoryItemResponse> Stacks,
    IReadOnlyList<ItemInstanceResponse> Items);

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

        // Two routes rather than one taking either shape, because they are two operations. A stack
        // moves a quantity and splits its cost basis; an instance moves as itself, carrying its own
        // condition and acquisition value. One endpoint switching on which field was populated
        // would hide that in a null check.
        group.MapPost("/{characterId:int}/inventory/transfer", TransferAsync);
        group.MapPost("/{characterId:int}/inventory/transfer-instance", TransferInstanceAsync);
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

                // Describe rather than LevelForXp: it returns the level and both progress figures
                // from one binary search, which is what it exists for.
                SkillProgress progress = SkillCurve.Describe(xp);

                return new SkillResponse(
                    r.Key,
                    r.Name,
                    r.Category,
                    xp,
                    progress.Level,
                    progress.XpToNextLevel,
                    progress.ProgressToNextLevel);
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

        List<InventoryItemResponse> stacks = await database.InventoryItems
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
                    : null,
                ii.Inventory!.Kind,
                ii.Inventory.StationId))
            .ToListAsync(cancellation);

        // Destroyed instances keep their rows so history survives them (ADR-0006), and must not
        // appear in an inventory: a player looking at the wreck of something they lost would
        // reasonably conclude they still had it.
        List<ItemInstanceResponse> instances = await database.ItemInstances
            .Where(i => i.Inventory!.CharacterId == characterId && i.DestroyedAt == null)
            .Include(i => i.ItemDef)
            .Include(i => i.Inventory)
            .OrderBy(i => i.ItemDef!.Key).ThenBy(i => i.Id)
            .Select(i => new ItemInstanceResponse(
                i.Id,
                i.ItemDefId,
                i.ItemDef!.Key,
                i.ItemDef.Name,
                i.Condition,
                i.Inventory!.Kind,
                i.Inventory.StationId))
            .ToListAsync(cancellation);

        return Results.Ok(new InventoryResponse(stacks, instances));
    }

    /// <summary>
    /// Moves a quantity of a stackable item between two of the character's own containers.
    /// </summary>
    /// <remarks>
    /// Ownership of both inventories is enforced in <see cref="InventoryService"/> rather than
    /// here, so this endpoint cannot be the one that forgets. What it adds is presence: goods do
    /// not move in or out of a station hangar unless their owner is standing in that station.
    /// </remarks>
    private static async Task<IResult> TransferAsync(
        int characterId,
        TransferRequest request,
        HttpContext context,
        Caller caller,
        SpaceMmoDbContext database,
        DockingService docking,
        CancellationToken cancellation)
    {
        ArgumentNullException.ThrowIfNull(request);

        OwnershipResult owned = await caller.OwnedCharacterAsync(context, characterId, cancellation);

        if (owned.Status != OwnershipStatus.Owned)
        {
            return owned.ToProblem();
        }

        IResult? refusal = await RefuseIfNotPresentAsync(
            database, docking, characterId,
            [request.FromInventoryId, request.ToInventoryId], cancellation);

        if (refusal is not null)
        {
            return refusal;
        }

        var inventories = new InventoryService(database);

        try
        {
            await inventories.TransferAsync(
                request.FromInventoryId,
                request.ToInventoryId,
                request.ItemDefId,
                request.Quantity,
                cancellation);

            await database.SaveChangesAsync(cancellation);
        }
        catch (InventoryTransferException error)
        {
            return Results.BadRequest(new { error = error.Message, reason = "bad_transfer" });
        }
        catch (InsufficientItemsException error)
        {
            return Results.Conflict(new { error = error.Message, reason = "insufficient_items" });
        }
        catch (ArgumentOutOfRangeException error)
        {
            return Results.BadRequest(new { error = error.Message, reason = "bad_quantity" });
        }

        return Results.Ok();
    }

    /// <summary>Moves one non-stackable item — a tool, a weapon, a ship — between containers.</summary>
    private static async Task<IResult> TransferInstanceAsync(
        int characterId,
        TransferInstanceRequest request,
        HttpContext context,
        Caller caller,
        SpaceMmoDbContext database,
        DockingService docking,
        CancellationToken cancellation)
    {
        ArgumentNullException.ThrowIfNull(request);

        OwnershipResult owned = await caller.OwnedCharacterAsync(context, characterId, cancellation);

        if (owned.Status != OwnershipStatus.Owned)
        {
            return owned.ToProblem();
        }

        // The source is wherever the instance currently is, which the service resolves — so only
        // the destination can be checked before the call. The service refuses a cross-owner move
        // regardless, which is the rule that actually protects anything.
        IResult? refusal = await RefuseIfNotPresentAsync(
            database, docking, characterId, [request.ToInventoryId], cancellation);

        if (refusal is not null)
        {
            return refusal;
        }

        var inventories = new InventoryService(database);

        try
        {
            await inventories.TransferInstanceAsync(
                request.ItemInstanceId, request.ToInventoryId, cancellation);

            await database.SaveChangesAsync(cancellation);
        }
        catch (InventoryTransferException error)
        {
            return Results.BadRequest(new { error = error.Message, reason = "bad_transfer" });
        }

        return Results.Ok();
    }

    /// <summary>
    /// Refuses unless the character is docked at every station hangar involved.
    /// </summary>
    /// <remarks>
    /// The same rule the market runs on, and for the same reason: a character docked at Grimhold
    /// has no business reaching into a hangar on Terra. Without it, hauling planet-locked materials
    /// (ADR-0008) would be a request rather than a flight, and the four-world economy would
    /// collapse into one warehouse.
    ///
    /// Only hangars are checked. A ship's hold and what a character is carrying travel with them,
    /// so there is nowhere else they could be.
    /// </remarks>
    private static async Task<IResult?> RefuseIfNotPresentAsync(
        SpaceMmoDbContext database,
        DockingService docking,
        int characterId,
        long[] inventoryIds,
        CancellationToken cancellation)
    {
        List<int> stationIds = await database.Inventories
            .Where(i => inventoryIds.Contains(i.Id)
                && i.Kind == InventoryKind.StationHangar
                && i.StationId != null)
            .Select(i => i.StationId!.Value)
            .Distinct()
            .ToListAsync(cancellation);

        foreach (int stationId in stationIds)
        {
            if (!await docking.IsDockedAtAsync(characterId, stationId, cancellation))
            {
                // Conflict rather than forbidden: nothing about the caller is wrong, they are
                // simply somewhere else, and flying there fixes it.
                return Results.Conflict(new
                {
                    error = "You must be docked at this station to move goods in or out of it.",
                    reason = "not_docked",
                });
            }
        }

        return null;
    }

    private static CharacterResponse ToResponse(Character character) => new(
        character.Id,
        character.Name,
        character.Race,
        Races.FactionFor(character.Race),
        character.HomeBodyId,
        character.Balance.MinorUnits);
}
