using Microsoft.EntityFrameworkCore;
using SpaceMMO.Data.Entities;

namespace SpaceMMO.Data.Progression;

/// <summary>
/// Adding XP to a character's skill, from wherever the XP came from.
/// </summary>
/// <remarks>
/// <para>
/// <strong>One implementation, because three was one bug written three times.</strong> Gathering,
/// industry and quests each had their own copy, and each queried the database for an existing row
/// before adding one. That is correct in isolation and wrong the moment two of them run in the same
/// unit of work: the first adds a row to the change tracker, the second's query goes to the
/// database, cannot see an insert that has not been saved, and adds a second row with the same
/// primary key. The save then fails on a duplicate key.
/// </para>
/// <para>
/// It surfaced the day quest progress was wired into gathering. Mining ore awarded <c>mining</c>
/// while the active quest rewarded <c>gathering</c> — two different rows, no collision, and it
/// worked. Gathering scrap awarded <c>gathering</c> and completed a quest that also rewarded
/// <c>gathering</c>, and every attempt returned a 500. The difference between the two was invisible
/// from either service on its own.
/// </para>
/// </remarks>
public static class SkillAwards
{
    /// <summary>
    /// Adds XP to a skill, creating the row on first use.
    /// </summary>
    /// <remarks>
    /// <para>
    /// <c>FindAsync</c> rather than <c>FirstOrDefaultAsync</c>, and that is the whole fix. Find
    /// checks the change tracker before it touches the database, so a row added earlier in the same
    /// transaction is found rather than duplicated. A query cannot see uncommitted inserts; the
    /// tracker can.
    /// </para>
    /// <para>
    /// Non-positive XP is ignored rather than rejected. Callers pass whatever an action produced,
    /// and an action that happened to be worth nothing is an ordinary outcome, not an error worth
    /// unwinding a transaction over.
    /// </para>
    /// </remarks>
    public static async Task AwardAsync(
        SpaceMmoDbContext database,
        int characterId,
        int skillId,
        long xp,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(database);

        if (xp <= 0)
        {
            return;
        }

        CharacterSkill? skill = await database.CharacterSkills.FindAsync(
            [characterId, skillId], cancellationToken);

        if (skill is null)
        {
            database.CharacterSkills.Add(new CharacterSkill
            {
                CharacterId = characterId,
                SkillId = skillId,
                Xp = xp,
            });

            return;
        }

        skill.Xp += xp;
    }
}
