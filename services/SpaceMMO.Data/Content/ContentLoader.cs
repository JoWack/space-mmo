using System.Text.Json;
using System.Text.Json.Serialization;
using Microsoft.EntityFrameworkCore;
using SpaceMMO.Data.Entities;
using SpaceMMO.Domain.Content;
using SpaceMMO.Domain.Economy;

namespace SpaceMMO.Data.Content;

/// <summary>
/// Reads authored content from <c>data/</c> and applies it to the database.
/// </summary>
/// <remarks>
/// <para>
/// <strong>Validated before anything is written.</strong> Content is applied as a unit, so a typo
/// in one recipe cannot leave the database half-updated with the rest.
/// </para>
/// <para>
/// <strong>Idempotent.</strong> Definitions are upserted by key, so running this at every startup
/// converges on the files rather than accumulating duplicates. That is what makes a balance change
/// a data edit and a restart rather than a deploy.
/// </para>
/// <para>
/// Content is never deleted, only added or updated. An item definition that vanished from the
/// files would still be referenced by player inventories, and removing it would orphan real
/// possessions — retiring content needs a deliberate migration, not a silent side effect of
/// editing a file.
/// </para>
/// </remarks>
public sealed class ContentLoader(SpaceMmoDbContext database)
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNameCaseInsensitive = true,
        ReadCommentHandling = JsonCommentHandling.Skip,
        AllowTrailingCommas = true,
        Converters = { new JsonStringEnumConverter() },
    };

    private readonly SpaceMmoDbContext _database =
        database ?? throw new ArgumentNullException(nameof(database));

    /// <summary>
    /// Reads every <c>*.json</c> under a content directory into one pack.
    /// </summary>
    /// <remarks>
    /// Files are merged rather than replacing one another, so content can be split by theme —
    /// <c>data/quests/main-story.json</c> alongside <c>data/quests/careers.json</c> — without the
    /// loader needing to know the layout. Files are read in sorted order so a duplicate key always
    /// produces the same validation error regardless of filesystem enumeration order.
    /// </remarks>
    /// <exception cref="DirectoryNotFoundException">If the content root does not exist.</exception>
    public static async Task<ContentPack> ReadAsync(
        string contentRoot, CancellationToken cancellationToken = default)
    {
        if (!Directory.Exists(contentRoot))
        {
            throw new DirectoryNotFoundException($"Content root '{contentRoot}' does not exist.");
        }

        ContentPack pack = ContentPack.Empty;

        foreach (string path in Directory
            .EnumerateFiles(contentRoot, "*.json", SearchOption.AllDirectories)
            .OrderBy(p => p, StringComparer.Ordinal))
        {
            await using FileStream stream = File.OpenRead(path);

            ContentFile? file;

            try
            {
                file = await JsonSerializer.DeserializeAsync<ContentFile>(
                    stream, JsonOptions, cancellationToken);
            }
            catch (JsonException error)
            {
                // Without the path, a malformed file in a directory of twenty is a scavenger hunt.
                throw new InvalidOperationException($"Could not parse '{path}': {error.Message}", error);
            }

            if (file is null)
            {
                continue;
            }

            pack = pack.Concat(new ContentPack(
                file.Skills ?? [],
                file.Items ?? [],
                file.Recipes ?? [],
                file.Quests ?? []));
        }

        return pack;
    }

    /// <summary>
    /// Validates a pack and applies it, in one transaction.
    /// </summary>
    /// <exception cref="InvalidOperationException">If the content has any validation errors.</exception>
    public async Task ApplyAsync(ContentPack pack, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(pack);

        // Before the transaction, not inside it: there is no point opening one to immediately
        // roll it back, and the error message is what the caller actually needs.
        ContentValidator.ValidateOrThrow(pack);

        await using var transaction =
            await _database.Database.BeginTransactionAsync(cancellationToken);

        Dictionary<string, int> skillIds = await UpsertSkillsAsync(pack, cancellationToken);
        Dictionary<string, int> itemIds = await UpsertItemsAsync(pack, cancellationToken);

        await UpsertRecipesAsync(pack, skillIds, itemIds, cancellationToken);
        await UpsertQuestsAsync(pack, skillIds, cancellationToken);

        await transaction.CommitAsync(cancellationToken);
    }

    /// <summary>Reads a content directory and applies it.</summary>
    public async Task LoadAsync(string contentRoot, CancellationToken cancellationToken = default)
    {
        ContentPack pack = await ReadAsync(contentRoot, cancellationToken);
        await ApplyAsync(pack, cancellationToken);
    }

    private async Task<Dictionary<string, int>> UpsertSkillsAsync(
        ContentPack pack, CancellationToken cancellationToken)
    {
        Dictionary<string, Skill> existing = await _database.Skills
            .ToDictionaryAsync(s => s.Key, StringComparer.Ordinal, cancellationToken);

        foreach (SkillContent content in pack.Skills)
        {
            if (existing.TryGetValue(content.Key, out Skill? skill))
            {
                skill.Name = content.Name;
                skill.Category = content.Category;
                continue;
            }

            var created = new Skill
            {
                Key = content.Key,
                Name = content.Name,
                Category = content.Category,
            };

            _database.Skills.Add(created);
            existing[content.Key] = created;
        }

        await _database.SaveChangesAsync(cancellationToken);

        return existing.ToDictionary(e => e.Key, e => e.Value.Id, StringComparer.Ordinal);
    }

    private async Task<Dictionary<string, int>> UpsertItemsAsync(
        ContentPack pack, CancellationToken cancellationToken)
    {
        Dictionary<string, ItemDef> existing = await _database.ItemDefs
            .ToDictionaryAsync(i => i.Key, StringComparer.Ordinal, cancellationToken);

        foreach (ItemContent content in pack.Items)
        {
            if (existing.TryGetValue(content.Key, out ItemDef? item))
            {
                item.Name = content.Name;
                item.Category = content.Category;
                item.VolumeM3 = content.VolumeM3;
                continue;
            }

            var created = new ItemDef
            {
                Key = content.Key,
                Name = content.Name,
                Category = content.Category,
                VolumeM3 = content.VolumeM3,
            };

            _database.ItemDefs.Add(created);
            existing[content.Key] = created;
        }

        await _database.SaveChangesAsync(cancellationToken);

        return existing.ToDictionary(e => e.Key, e => e.Value.Id, StringComparer.Ordinal);
    }

    private async Task UpsertRecipesAsync(
        ContentPack pack,
        Dictionary<string, int> skillIds,
        Dictionary<string, int> itemIds,
        CancellationToken cancellationToken)
    {
        Dictionary<string, Recipe> existing = await _database.Recipes
            .ToDictionaryAsync(r => r.Key, StringComparer.Ordinal, cancellationToken);

        foreach (RecipeContent content in pack.Recipes)
        {
            if (!existing.TryGetValue(content.Key, out Recipe? recipe))
            {
                recipe = new Recipe { Key = content.Key };
                _database.Recipes.Add(recipe);
                existing[content.Key] = recipe;
            }

            recipe.OutputItemDefId = itemIds[content.Output];
            recipe.OutputQuantity = content.OutputQuantity;
            recipe.SkillId = skillIds[content.Skill];
            recipe.RequiredLevel = content.RequiredLevel;
            recipe.JobSeconds = content.JobSeconds;
            recipe.XpPerRun = content.XpPerRun;
            recipe.RequiredToolItemDefId =
                content.RequiredTool is string tool ? itemIds[tool] : null;
        }

        await _database.SaveChangesAsync(cancellationToken);

        // Inputs are replaced wholesale rather than diffed. They are keyed by (recipe, item), so a
        // rebalance that drops an ingredient would otherwise leave the old row behind and quietly
        // keep charging players for a material the recipe no longer needs.
        foreach (RecipeContent content in pack.Recipes)
        {
            Recipe recipe = existing[content.Key];

            List<RecipeInput> current = await _database.RecipeInputs
                .Where(i => i.RecipeId == recipe.Id)
                .ToListAsync(cancellationToken);

            _database.RecipeInputs.RemoveRange(current);
            await _database.SaveChangesAsync(cancellationToken);

            foreach (RecipeInputContent input in content.Inputs)
            {
                _database.RecipeInputs.Add(new RecipeInput
                {
                    RecipeId = recipe.Id,
                    ItemDefId = itemIds[input.Item],
                    Quantity = input.Quantity,
                });
            }
        }

        await _database.SaveChangesAsync(cancellationToken);
    }

    private async Task UpsertQuestsAsync(
        ContentPack pack, Dictionary<string, int> skillIds, CancellationToken cancellationToken)
    {
        Dictionary<string, QuestDef> existing = await _database.QuestDefs
            .ToDictionaryAsync(q => q.Key, StringComparer.Ordinal, cancellationToken);

        // Two passes, because a quest's prerequisite may appear later in the file than the quest
        // that needs it, and its row has no id until it is saved.
        foreach (QuestContent content in pack.Quests)
        {
            if (!existing.TryGetValue(content.Key, out QuestDef? quest))
            {
                quest = new QuestDef { Key = content.Key, Name = content.Name };
                _database.QuestDefs.Add(quest);
                existing[content.Key] = quest;
            }

            quest.Name = content.Name;
            quest.Kind = content.Kind;
            quest.RewardCredits = Credits.FromWholeCredits(content.RewardCredits);
            quest.RewardSkillId = content.RewardSkill is string skill ? skillIds[skill] : null;
            quest.RewardXp = content.RewardXp;
            quest.CooldownSeconds = content.CooldownSeconds;
        }

        await _database.SaveChangesAsync(cancellationToken);

        foreach (QuestContent content in pack.Quests)
        {
            existing[content.Key].PrerequisiteQuestDefId =
                content.Prerequisite is string prerequisite ? existing[prerequisite].Id : null;
        }

        await _database.SaveChangesAsync(cancellationToken);

        // Steps are replaced wholesale for the same reason recipe inputs are: they are keyed by
        // (quest, ordinal), and shortening a quest would strand the removed tail.
        foreach (QuestContent content in pack.Quests)
        {
            QuestDef quest = existing[content.Key];

            List<QuestStep> current = await _database.QuestSteps
                .Where(s => s.QuestDefId == quest.Id)
                .ToListAsync(cancellationToken);

            _database.QuestSteps.RemoveRange(current);
            await _database.SaveChangesAsync(cancellationToken);

            foreach (QuestStepContent step in content.Steps)
            {
                _database.QuestSteps.Add(new QuestStep
                {
                    QuestDefId = quest.Id,
                    Ordinal = step.Ordinal,
                    Description = step.Description,
                    ObjectiveType = step.Objective,
                    TargetKey = step.Target,
                    Quantity = step.Quantity,
                });
            }
        }

        await _database.SaveChangesAsync(cancellationToken);
    }

    /// <summary>
    /// The shape of one content file. Every section is optional, so a file can define just one
    /// kind of thing.
    /// </summary>
    private sealed record ContentFile(
        IReadOnlyList<SkillContent>? Skills,
        IReadOnlyList<ItemContent>? Items,
        IReadOnlyList<RecipeContent>? Recipes,
        IReadOnlyList<QuestContent>? Quests);
}
