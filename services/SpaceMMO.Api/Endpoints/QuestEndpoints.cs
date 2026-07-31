using Microsoft.EntityFrameworkCore;
using SpaceMMO.Api.Auth;
using SpaceMMO.Data;
using SpaceMMO.Data.Quests;
using SpaceMMO.Domain.Quests;

namespace SpaceMMO.Api.Endpoints;

public sealed record AcceptQuestRequest(int CharacterId, string QuestKey);

public sealed record JournalEntryResponse(
    string QuestKey,
    string Name,
    QuestKind Kind,
    QuestState State,
    int StepOrdinal,
    DateTimeOffset? CompletedAt);

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

        List<JournalEntryResponse> entries = await database.CharacterQuests
            .Where(cq => cq.CharacterId == characterId)
            .Include(cq => cq.QuestDef)
            .OrderBy(cq => cq.Id)
            .Select(cq => new JournalEntryResponse(
                cq.QuestDef!.Key,
                cq.QuestDef.Name,
                cq.QuestDef.Kind,
                cq.State,
                cq.StepOrdinal,
                cq.CompletedAt))
            .ToListAsync(cancellation);

        return Results.Ok(entries);
    }
}
