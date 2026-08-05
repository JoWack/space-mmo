using Microsoft.EntityFrameworkCore;
using SpaceMMO.Api.Auth;
using SpaceMMO.Data;
using SpaceMMO.Data.Quests;
using SpaceMMO.Domain.Quests;

namespace SpaceMMO.Api.Endpoints;

public sealed record AcceptQuestRequest(int CharacterId, string QuestKey);

/// <summary>
/// One quest in a character's journal, including what it currently wants.
/// </summary>
/// <param name="StepDescription">The authored line telling the player what to do.</param>
/// <param name="StepProgress">How far into the current step they are.</param>
/// <param name="StepRequired">How much the current step needs.</param>
/// <remarks>
/// The step fields are null on a quest with no active step — one that is finished, or waiting to be
/// handed in. A journal that returned only an ordinal, as this did, told a player which numbered
/// step they were on and nothing about what it asked for, which is the one thing they needed.
/// </remarks>
public sealed record JournalEntryResponse(
    string QuestKey,
    string Name,
    QuestKind Kind,
    QuestState State,
    int StepOrdinal,
    DateTimeOffset? CompletedAt,
    string? StepDescription,
    ObjectiveType? StepObjective,
    string? StepTargetKey,
    int StepProgress,
    int? StepRequired);

/// <summary>A quest the character could accept now.</summary>
/// <remarks>
/// Advisory, not authoritative. <c>QuestService.AcceptAsync</c> re-checks prerequisites, cooldowns
/// and repeat rules and is the only thing that decides — listing a quest here and then refusing it
/// is preferable to duplicating those rules in a query and having the two drift apart.
/// </remarks>
public sealed record AvailableQuestResponse(string QuestKey, string Name, QuestKind Kind);

/// <summary>
/// The quest journal.
/// </summary>
/// <remarks>
/// <para>
/// There is no "complete this quest" endpoint, and there should never be one. Progress is a
/// consequence of what a character actually did — gathering, crafting, travelling — recorded by
/// whichever service did it. The client renders the journal and never asserts a step is done.
/// </para>
/// <para>
/// Accepting is the one thing a player genuinely chooses, so it is the one thing they can POST.
/// </para>
/// </remarks>
public static class QuestEndpoints
{
    public static void MapQuestEndpoints(this IEndpointRouteBuilder routes)
    {
        RouteGroupBuilder group = routes.MapGroup("/quests").WithTags("Quests");

        group.MapPost("/accept", AcceptAsync);
        group.MapGet("/journal/{characterId:int}", JournalAsync);
        group.MapGet("/available/{characterId:int}", AvailableAsync);
    }

    private static async Task<IResult> AcceptAsync(
        AcceptQuestRequest request,
        HttpContext context,
        Caller caller,
        QuestService quests,
        CancellationToken cancellation)
    {
        OwnershipResult owned =
            await caller.OwnedCharacterAsync(context, request.CharacterId, cancellation);

        if (owned.Status != OwnershipStatus.Owned)
        {
            return owned.ToProblem();
        }

        try
        {
            long id = await quests.AcceptAsync(request.CharacterId, request.QuestKey, cancellation);

            return Results.Ok(new { characterQuestId = id });
        }
        catch (QuestLockedException ex)
        {
            return Results.Conflict(new { error = ex.Message, reason = "prerequisite_incomplete" });
        }
        catch (QuestOnCooldownException ex)
        {
            return Results.Conflict(new { error = ex.Message, reason = "on_cooldown" });
        }
        catch (InvalidOperationException)
        {
            // SingleAsync throws this when the quest key matches nothing. A bad key is the
            // caller's mistake, not a server fault.
            return Results.NotFound(new { error = $"No quest with key '{request.QuestKey}'." });
        }
    }

    private static async Task<IResult> JournalAsync(
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

        // The active step is joined in rather than fetched per entry. A journal is read constantly
        // and a query per quest would turn a cheap screen into a slow one as chains get longer.
        var rows = await database.CharacterQuests
            .Where(cq => cq.CharacterId == characterId)
            .Include(cq => cq.QuestDef)
            .OrderBy(cq => cq.Id)
            .Select(cq => new
            {
                Quest = cq,
                Step = database.QuestSteps.FirstOrDefault(
                    s => s.QuestDefId == cq.QuestDefId && s.Ordinal == cq.StepOrdinal),
            })
            .ToListAsync(cancellation);

        return Results.Ok(rows
            .Select(r => new JournalEntryResponse(
                r.Quest.QuestDef!.Key,
                r.Quest.QuestDef.Name,
                r.Quest.QuestDef.Kind,
                r.Quest.State,
                r.Quest.StepOrdinal,
                r.Quest.CompletedAt,
                r.Step?.Description,
                r.Step?.ObjectiveType,
                r.Step?.TargetKey,
                r.Quest.StepProgress,
                r.Step?.Quantity))
            .ToList());
    }

    /// <summary>
    /// Quests this character could take now.
    /// </summary>
    /// <remarks>
    /// Without this a new character has an empty journal and no way to discover that a questline
    /// exists at all — accepting requires a key, and nothing was willing to say what the keys were.
    /// </remarks>
    private static async Task<IResult> AvailableAsync(
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

        List<int> completed = await database.CharacterQuests
            .Where(cq => cq.CharacterId == characterId && cq.State == QuestState.Completed)
            .Select(cq => cq.QuestDefId)
            .ToListAsync(cancellation);

        // Anything already under way is not available, whether it is being worked on or waiting to
        // be handed in.
        List<int> underway = await database.CharacterQuests
            .Where(cq => cq.CharacterId == characterId
                && (cq.State == QuestState.InProgress || cq.State == QuestState.ReadyToTurnIn))
            .Select(cq => cq.QuestDefId)
            .ToListAsync(cancellation);

        List<AvailableQuestResponse> available = await database.QuestDefs
            .Where(q => !underway.Contains(q.Id))
            .Where(q => !completed.Contains(q.Id) || q.CooldownSeconds != null)
            .Where(q => q.PrerequisiteQuestDefId == null
                || completed.Contains(q.PrerequisiteQuestDefId.Value))
            .OrderBy(q => q.Id)
            .Select(q => new AvailableQuestResponse(q.Key, q.Name, q.Kind))
            .ToListAsync(cancellation);

        return Results.Ok(available);
    }
}
