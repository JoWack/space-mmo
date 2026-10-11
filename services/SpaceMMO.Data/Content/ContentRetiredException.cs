namespace SpaceMMO.Data.Content;

/// <summary>
/// Thrown when something new is started from content the pack no longer names (task 188): a job from a
/// retired recipe, a retired quest accepted, a ship summoned to a retired station.
/// </summary>
/// <remarks>
/// What already exists is left alone -- a running job still claims, a quest under way still finishes --
/// so this is only ever about starting. The client listed the thing from a catalogue fetched before the
/// seed, which is the one way to ask for it.
/// </remarks>
public sealed class ContentRetiredException(string kind, string key)
    : InvalidOperationException($"The {kind} '{key}' is no longer part of the game.")
{
    public string Kind { get; } = kind;

    public string Key { get; } = key;
}
