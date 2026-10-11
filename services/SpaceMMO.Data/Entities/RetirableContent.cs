namespace SpaceMMO.Data.Entities;

/// <summary>
/// Content that seeding retires, rather than deletes, when the pack stops naming it (task 188).
/// </summary>
/// <remarks>
/// Stations, recipes, quests and items: each has players' things hanging off it -- hangars, jobs,
/// journals, stacks -- so taking its row away would orphan them, and leaving it untouched kept serving
/// content the pack no longer authors. Retiring stops anything new starting from it and leaves
/// everything that already refers to it valid (Joe, 11 October).
/// </remarks>
public interface IRetirableContent
{
    string Key { get; }

    DateTimeOffset? RetiredAt { get; set; }
}
