using SpaceMMO.Domain.Characters;
using SpaceMMO.Domain.Items;
using SpaceMMO.Domain.Progression;
using SpaceMMO.Domain.Quests;
using SpaceMMO.Domain.Universe;

namespace SpaceMMO.Domain.Content;

/// <summary>One problem found in authored content.</summary>
/// <param name="Scope">Which kind of definition — <c>recipe</c>, <c>quest</c>, and so on.</param>
/// <param name="Key">The offending definition's key, or <c>*</c> for pack-wide problems.</param>
/// <param name="Message">What is wrong, phrased for whoever is editing the file.</param>
public sealed record ContentError(string Scope, string Key, string Message)
{
    public override string ToString() => $"[{Scope}:{Key}] {Message}";
}

/// <summary>
/// Checks authored content before it reaches the database.
/// </summary>
/// <remarks>
/// <para>
/// Returns <em>every</em> problem rather than throwing on the first. Content is edited in bulk,
/// and fixing twelve errors one server restart at a time is miserable in a way that fixing twelve
/// listed errors is not.
/// </para>
/// <para>
/// Pure, so content can be validated in a unit test and in CI rather than only at startup. A
/// broken balance edit should fail before anyone deploys it.
/// </para>
/// </remarks>
public static class ContentValidator
{
    /// <summary>Validates a pack, returning all problems found.</summary>
    public static IReadOnlyList<ContentError> Validate(ContentPack pack)
    {
        ArgumentNullException.ThrowIfNull(pack);

        var errors = new List<ContentError>();

        HashSet<string> skillKeys = CollectKeys(pack.Skills.Select(s => s.Key), "skill", errors);
        HashSet<string> itemKeys = CollectKeys(pack.Items.Select(i => i.Key), "item", errors);
        CollectKeys(pack.Recipes.Select(r => r.Key), "recipe", errors);
        HashSet<string> questKeys = CollectKeys(pack.Quests.Select(q => q.Key), "quest", errors);

        var toolKeys = pack.Items
            .Where(i => i.Category == ItemCategory.Tool)
            .Select(i => i.Key)
            .ToHashSet(StringComparer.Ordinal);

        HashSet<string> systemKeys = CollectKeys(pack.Systems.Select(s => s.Key), "system", errors);
        HashSet<string> bodyKeys = CollectKeys(pack.Bodies.Select(b => b.Key), "body", errors);
        CollectKeys(pack.Stations.Select(s => s.Key), "station", errors);

        ValidateItems(pack, errors);
        ValidateRecipes(pack, skillKeys, itemKeys, toolKeys, errors);
        ValidateQuests(pack, skillKeys, questKeys, errors);
        ValidateUniverse(pack, systemKeys, bodyKeys, errors);
        ValidateResourceNodes(pack, bodyKeys, itemKeys, skillKeys, errors);
        ValidateRecipeGraphIsAcyclic(pack, errors);
        ValidatePrerequisitesAreAcyclic(pack, errors);

        return errors;
    }

    /// <summary>Validates and throws if anything is wrong. For startup paths.</summary>
    /// <exception cref="InvalidOperationException">If the pack has any errors.</exception>
    public static void ValidateOrThrow(ContentPack pack)
    {
        IReadOnlyList<ContentError> errors = Validate(pack);

        if (errors.Count == 0)
        {
            return;
        }

        throw new InvalidOperationException(
            $"Content has {errors.Count} problem(s):{Environment.NewLine}"
            + string.Join(Environment.NewLine, errors.Select(e => $"  {e}")));
    }

    private static HashSet<string> CollectKeys(
        IEnumerable<string> keys, string scope, List<ContentError> errors)
    {
        var seen = new HashSet<string>(StringComparer.Ordinal);

        foreach (string key in keys)
        {
            if (string.IsNullOrWhiteSpace(key))
            {
                errors.Add(new ContentError(scope, "?", "Key is missing or blank."));
                continue;
            }

            if (!seen.Add(key))
            {
                // Duplicates would make an upsert silently pick a winner, so the content author
                // would never learn which of their two definitions is live.
                errors.Add(new ContentError(scope, key, "Duplicate key."));
            }
        }

        return seen;
    }

    /// <summary>
    /// Checks the universe: references resolve, radii are real, and every race can start.
    /// </summary>
    /// <remarks>
    /// The last of those is the one that matters. A missing homeworld is not a content typo that
    /// degrades something — it makes character creation fail outright for everyone who picks that
    /// race, and it fails at the API with a 500 rather than at load with an explanation. Checking
    /// it here turns a runtime outage into a startup error naming the missing key.
    /// </remarks>
    private static void ValidateUniverse(
        ContentPack pack,
        HashSet<string> systemKeys,
        HashSet<string> bodyKeys,
        List<ContentError> errors)
    {
        foreach (BodyContent body in pack.Bodies)
        {
            if (!systemKeys.Contains(body.System))
            {
                errors.Add(new ContentError("body", body.Key, $"Unknown system '{body.System}'."));
            }

            if (body.RadiusKm <= 0.0)
            {
                // A zero radius makes every gravity and altitude calculation degenerate, and a
                // negative one inverts "up".
                errors.Add(new ContentError("body", body.Key, "Radius must be positive."));
            }
        }

        foreach (StationContent station in pack.Stations)
        {
            if (!systemKeys.Contains(station.System))
            {
                errors.Add(new ContentError(
                    "station", station.Key, $"Unknown system '{station.System}'."));
            }

            // A null body is a deep-space station and legitimate. A named one that does not exist
            // is not.
            if (station.Body is not null && !bodyKeys.Contains(station.Body))
            {
                errors.Add(new ContentError(
                    "station", station.Key, $"Unknown body '{station.Body}'."));
            }
        }

        // Only worth checking once any universe is authored at all — an empty pack is a valid
        // thing to validate, and demanding homeworlds of it would break every content unit test.
        if (pack.Bodies.Count == 0)
        {
            return;
        }

        foreach (Race race in Enum.GetValues<Race>())
        {
            string required = Races.HomeBodyKeyFor(race);

            if (!bodyKeys.Contains(required))
            {
                errors.Add(new ContentError(
                    "body",
                    required,
                    $"Missing starting body for {race}; characters of that race cannot be created."));
            }
        }
    }

    /// <summary>
    /// Checks that every deposit sits on a real body, yields a real item, and has somewhere to be.
    /// </summary>
    private static void ValidateResourceNodes(
        ContentPack pack,
        HashSet<string> bodyKeys,
        HashSet<string> itemKeys,
        HashSet<string> skillKeys,
        List<ContentError> errors)
    {
        foreach (ResourceNodeContent node in pack.ResourceNodes)
        {
            if (!bodyKeys.Contains(node.Body))
            {
                errors.Add(new ContentError("node", node.Key, $"Unknown body '{node.Body}'."));
            }

            if (!itemKeys.Contains(node.Item))
            {
                errors.Add(new ContentError("node", node.Key, $"Unknown item '{node.Item}'."));
            }

            if (!skillKeys.Contains(node.Skill))
            {
                errors.Add(new ContentError("node", node.Key, $"Unknown skill '{node.Skill}'."));
            }

            // A zero direction has no point on the sphere to correspond to, and normalising it
            // later would produce a NaN that ends up as a deposit at no position at all.
            if (node.Direction is not { Length: 3 }
                || (node.Direction[0] == 0.0 && node.Direction[1] == 0.0 && node.Direction[2] == 0.0))
            {
                errors.Add(new ContentError(
                    "node", node.Key, "Direction must be three non-zero components."));
            }

            if (node.QuantityMax <= 0)
            {
                errors.Add(new ContentError("node", node.Key, "Quantity must be positive."));
            }

            if (node.RespawnSeconds <= 0)
            {
                // Zero would make the deposit infinite, which removes the throttle the whole
                // material faucet depends on (economy-design §5b).
                errors.Add(new ContentError("node", node.Key, "Respawn seconds must be positive."));
            }

            if (node.RequiredLevel is < SkillCurve.MinLevel or > SkillCurve.MaxLevel)
            {
                errors.Add(new ContentError(
                    "node", node.Key, $"Required level {node.RequiredLevel} is outside the curve."));
            }
        }
    }

    private static void ValidateItems(ContentPack pack, List<ContentError> errors)
    {
        foreach (ItemContent item in pack.Items)
        {
            if (item.VolumeM3 <= 0)
            {
                errors.Add(new ContentError(
                    "item", item.Key, $"Volume must be positive, got {item.VolumeM3}."));
            }
        }
    }

    private static void ValidateRecipes(
        ContentPack pack,
        HashSet<string> skillKeys,
        HashSet<string> itemKeys,
        HashSet<string> toolKeys,
        List<ContentError> errors)
    {
        foreach (RecipeContent recipe in pack.Recipes)
        {
            if (!itemKeys.Contains(recipe.Output))
            {
                errors.Add(new ContentError(
                    "recipe", recipe.Key, $"Unknown output item '{recipe.Output}'."));
            }

            if (!skillKeys.Contains(recipe.Skill))
            {
                errors.Add(new ContentError(
                    "recipe", recipe.Key, $"Unknown skill '{recipe.Skill}'."));
            }

            if (recipe.OutputQuantity <= 0)
            {
                errors.Add(new ContentError(
                    "recipe", recipe.Key, $"Output quantity must be positive, got {recipe.OutputQuantity}."));
            }

            if (recipe.RequiredLevel is < SkillCurve.MinLevel or > SkillCurve.MaxLevel)
            {
                errors.Add(new ContentError(
                    "recipe",
                    recipe.Key,
                    $"Required level must be {SkillCurve.MinLevel}-{SkillCurve.MaxLevel}, got {recipe.RequiredLevel}."));
            }

            if (recipe.JobSeconds <= 0)
            {
                errors.Add(new ContentError(
                    "recipe", recipe.Key, $"Job duration must be positive, got {recipe.JobSeconds}."));
            }

            if (recipe.XpPerRun < 0)
            {
                errors.Add(new ContentError(
                    "recipe", recipe.Key, $"XP per run cannot be negative, got {recipe.XpPerRun}."));
            }

            if (recipe.RequiredTool is string tool)
            {
                if (!itemKeys.Contains(tool))
                {
                    errors.Add(new ContentError(
                        "recipe", recipe.Key, $"Unknown required tool '{tool}'."));
                }
                else if (!toolKeys.Contains(tool))
                {
                    // A gate on a non-tool would never be satisfiable, because the tool check
                    // looks for a tracked instance and stackable items have none.
                    errors.Add(new ContentError(
                        "recipe", recipe.Key, $"Required tool '{tool}' is not a tool-category item."));
                }
            }

            if (recipe.Inputs.Count == 0)
            {
                // A recipe with no inputs turns time into material from nothing, which is a
                // faucet outside the one place material is supposed to enter the economy.
                errors.Add(new ContentError(
                    "recipe", recipe.Key, "Recipe has no inputs, which would create material from nothing."));
            }

            var seenInputs = new HashSet<string>(StringComparer.Ordinal);

            foreach (RecipeInputContent input in recipe.Inputs)
            {
                if (!itemKeys.Contains(input.Item))
                {
                    errors.Add(new ContentError(
                        "recipe", recipe.Key, $"Unknown input item '{input.Item}'."));
                }

                if (input.Quantity <= 0)
                {
                    errors.Add(new ContentError(
                        "recipe", recipe.Key, $"Input '{input.Item}' quantity must be positive."));
                }

                if (!seenInputs.Add(input.Item))
                {
                    // The database keys inputs by (recipe, item), so a duplicate would silently
                    // drop one row rather than summing them.
                    errors.Add(new ContentError(
                        "recipe", recipe.Key, $"Input '{input.Item}' is listed more than once."));
                }

                if (string.Equals(input.Item, recipe.Output, StringComparison.Ordinal))
                {
                    errors.Add(new ContentError(
                        "recipe",
                        recipe.Key,
                        $"'{input.Item}' is both an input and the output, which is a conversion loop."));
                }
            }
        }
    }

    private static void ValidateQuests(
        ContentPack pack,
        HashSet<string> skillKeys,
        HashSet<string> questKeys,
        List<ContentError> errors)
    {
        foreach (QuestContent quest in pack.Quests)
        {
            if (quest.Prerequisite is string prerequisite && !questKeys.Contains(prerequisite))
            {
                errors.Add(new ContentError(
                    "quest", quest.Key, $"Unknown prerequisite '{prerequisite}'."));
            }

            if (quest.RewardSkill is string skill && !skillKeys.Contains(skill))
            {
                errors.Add(new ContentError("quest", quest.Key, $"Unknown reward skill '{skill}'."));
            }

            if (quest.RewardXp > 0 && quest.RewardSkill is null)
            {
                errors.Add(new ContentError(
                    "quest", quest.Key, "Awards XP but names no skill to award it to."));
            }

            if (quest.RewardCredits < 0)
            {
                errors.Add(new ContentError("quest", quest.Key, "Reward credits cannot be negative."));
            }

            // Only repeatable quests have a cooldown, and every repeatable quest needs one — a
            // repeatable with no cooldown is an unbounded faucet no matter what the daily cap says.
            bool repeatable = quest.Kind == QuestKind.Sidequest;

            if (repeatable && quest.CooldownSeconds is null)
            {
                errors.Add(new ContentError(
                    "quest", quest.Key, "Sidequests must define a cooldown."));
            }

            if (!repeatable && quest.CooldownSeconds is not null)
            {
                errors.Add(new ContentError(
                    "quest", quest.Key, $"{quest.Kind} quests are one-shot and cannot have a cooldown."));
            }

            if (quest.CooldownSeconds is int cooldown && cooldown <= 0)
            {
                errors.Add(new ContentError("quest", quest.Key, "Cooldown must be positive."));
            }

            ValidateQuestSteps(quest, errors);
        }
    }

    private static void ValidateQuestSteps(QuestContent quest, List<ContentError> errors)
    {
        if (quest.Steps.Count == 0)
        {
            errors.Add(new ContentError("quest", quest.Key, "Quest has no steps."));
            return;
        }

        // Ordinals must run 1..n with no gaps: the engine advances by incrementing, so a gap
        // would leave a quest permanently stuck on a step that does not exist.
        var ordinals = quest.Steps.Select(s => s.Ordinal).OrderBy(o => o).ToList();

        for (int i = 0; i < ordinals.Count; i++)
        {
            if (ordinals[i] != i + 1)
            {
                errors.Add(new ContentError(
                    "quest",
                    quest.Key,
                    $"Step ordinals must run 1..{ordinals.Count} with no gaps or duplicates, got "
                    + $"[{string.Join(", ", ordinals)}]."));

                break;
            }
        }

        foreach (QuestStepContent step in quest.Steps)
        {
            if (step.Quantity <= 0)
            {
                errors.Add(new ContentError(
                    "quest", quest.Key, $"Step {step.Ordinal} quantity must be positive."));
            }

            if (string.IsNullOrWhiteSpace(step.Target))
            {
                errors.Add(new ContentError(
                    "quest", quest.Key, $"Step {step.Ordinal} has no target."));
            }

            if (string.IsNullOrWhiteSpace(step.Description))
            {
                errors.Add(new ContentError(
                    "quest", quest.Key, $"Step {step.Ordinal} has no description."));
            }

            // Travel, dock, and talk are singular actions; asking for three of them would never
            // complete, since each event carries a quantity of one.
            bool singular = step.Objective
                is ObjectiveType.Travel or ObjectiveType.Dock or ObjectiveType.Talk;

            if (singular && step.Quantity != 1)
            {
                errors.Add(new ContentError(
                    "quest",
                    quest.Key,
                    $"Step {step.Ordinal} is a {step.Objective} objective, which must have quantity 1."));
            }
        }
    }

    /// <summary>
    /// Rejects cycles in the recipe dependency graph.
    /// </summary>
    /// <remarks>
    /// If A can be made from B and B from A, players can convert back and forth indefinitely. That
    /// is a free-material loop, and economy-design invariant 4 exists to keep them out. Catching it
    /// in content is far cheaper than discovering it from a price chart six months in.
    /// </remarks>
    private static void ValidateRecipeGraphIsAcyclic(ContentPack pack, List<ContentError> errors)
    {
        // output item -> the items it is made from
        var dependencies = new Dictionary<string, HashSet<string>>(StringComparer.Ordinal);

        foreach (RecipeContent recipe in pack.Recipes)
        {
            if (!dependencies.TryGetValue(recipe.Output, out HashSet<string>? inputs))
            {
                inputs = new HashSet<string>(StringComparer.Ordinal);
                dependencies[recipe.Output] = inputs;
            }

            foreach (RecipeInputContent input in recipe.Inputs)
            {
                inputs.Add(input.Item);
            }
        }

        var visiting = new HashSet<string>(StringComparer.Ordinal);
        var settled = new HashSet<string>(StringComparer.Ordinal);
        var reported = new HashSet<string>(StringComparer.Ordinal);

        foreach (string item in dependencies.Keys)
        {
            Walk(item, []);
        }

        void Walk(string item, List<string> path)
        {
            if (settled.Contains(item))
            {
                return;
            }

            if (!visiting.Add(item))
            {
                int start = path.IndexOf(item);
                IEnumerable<string> cycle = start >= 0 ? path.Skip(start) : path;
                string description = string.Join(" -> ", cycle.Append(item));

                if (reported.Add(description))
                {
                    errors.Add(new ContentError(
                        "recipe", item, $"Recipe dependency cycle: {description}."));
                }

                return;
            }

            path.Add(item);

            if (dependencies.TryGetValue(item, out HashSet<string>? inputs))
            {
                foreach (string input in inputs)
                {
                    Walk(input, path);
                }
            }

            path.RemoveAt(path.Count - 1);
            visiting.Remove(item);
            settled.Add(item);
        }
    }

    /// <summary>
    /// Rejects cycles in quest prerequisites, which would make every quest in the loop unreachable.
    /// </summary>
    private static void ValidatePrerequisitesAreAcyclic(ContentPack pack, List<ContentError> errors)
    {
        var prerequisites = pack.Quests
            .Where(q => q.Prerequisite is not null)
            .ToDictionary(q => q.Key, q => q.Prerequisite!, StringComparer.Ordinal);

        foreach (string questKey in prerequisites.Keys)
        {
            var seen = new HashSet<string>(StringComparer.Ordinal) { questKey };
            string current = questKey;

            while (prerequisites.TryGetValue(current, out string? next))
            {
                if (!seen.Add(next))
                {
                    errors.Add(new ContentError(
                        "quest", questKey, $"Prerequisite cycle involving '{next}'."));

                    break;
                }

                current = next;
            }
        }
    }
}
