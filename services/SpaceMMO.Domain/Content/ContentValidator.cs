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

            ValidateStationPosition(station, errors);
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

        ValidatePlanetLockedMaterials(pack, errors);
    }

    /// <summary>
    /// A planet-locked material must occur on exactly one body (ADR-0008).
    /// </summary>
    /// <remarks>
    /// Both halves matter and they fail differently. A material on two planets is no longer
    /// locked, so hauling it stops being a trade and the faction line stops having anything to
    /// be a line between — and nothing about the world looks wrong while that is true. A
    /// material on no planet is worse in a quieter way: it is an input to a recipe that nobody
    /// can ever satisfy, which reads to a player as a broken game rather than as missing
    /// content.
    /// </remarks>
    /// <summary>
    /// A station must be placed the way its location allows, or not placed at all.
    /// </summary>
    /// <remarks>
    /// Being unplaced is legitimate — a station may be authored before anyone decides where it
    /// stands, and nothing can dock at one without a position. Being placed <em>both</em> ways is
    /// not: two answers to "where is it" mean whichever the code happens to read wins, and the
    /// other is a lie nobody notices until docking works from the wrong place.
    ///
    /// A body-relative direction on a station that orbits nothing has no centre to be relative to,
    /// and a system position on a station attached to a body would drift the moment the body did.
    /// </remarks>
    private static void ValidateStationPosition(StationContent station, List<ContentError> errors)
    {
        bool hasDirection = station.Direction is not null;
        bool hasSystemPosition = station.SystemPosition is not null;

        if (hasDirection && hasSystemPosition)
        {
            errors.Add(new ContentError(
                "station",
                station.Key,
                "Placed by both a direction and a system position; it can only be in one place."));
        }

        if (hasDirection)
        {
            if (station.Body is null)
            {
                errors.Add(new ContentError(
                    "station",
                    station.Key,
                    "Placed by direction, but orbits no body for the direction to be from."));
            }

            // A zero direction names no point on the sphere, and normalising it later produces a
            // NaN — a station at no position rather than a visible mistake.
            if (station.Direction is not { Length: 3 }
                || (station.Direction[0] == 0.0
                    && station.Direction[1] == 0.0
                    && station.Direction[2] == 0.0))
            {
                errors.Add(new ContentError(
                    "station", station.Key, "Direction must be three non-zero components."));
            }
        }

        if (hasSystemPosition)
        {
            if (station.Body is not null)
            {
                errors.Add(new ContentError(
                    "station",
                    station.Key,
                    "Placed by system position, but attached to a body it would drift away from."));
            }

            if (station.SystemPosition is not { Length: 3 })
            {
                errors.Add(new ContentError(
                    "station", station.Key, "System position must be three components."));
            }
        }

        if (station.DockingRangeKm <= 0.0)
        {
            // Zero range makes a station impossible to dock at, which reads to a player as a
            // broken station rather than as unfinished content.
            errors.Add(new ContentError(
                "station", station.Key, "Docking range must be positive."));
        }
    }

    private static void ValidatePlanetLockedMaterials(ContentPack pack, List<ContentError> errors)
    {
        foreach (ItemContent item in pack.Items.Where(i => i.PlanetLocked))
        {
            string[] bodies = pack.ResourceNodes
                .Where(n => n.Item == item.Key)
                .Select(n => n.Body)
                .Distinct()
                .Order(StringComparer.Ordinal)
                .ToArray();

            if (bodies.Length == 0)
            {
                errors.Add(new ContentError(
                    "item",
                    item.Key,
                    "Planet-locked, but no deposit anywhere produces it."));
            }
            else if (bodies.Length > 1)
            {
                errors.Add(new ContentError(
                    "item",
                    item.Key,
                    $"Planet-locked, but occurs on {bodies.Length} bodies: "
                        + $"{string.Join(", ", bodies)}."));
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

            if (item.FactionBuyPrice is not { } price)
            {
                continue;
            }

            if (price <= 0)
            {
                // A zero or negative standing bid is not a cheap floor, it is a broken one: the
                // player hands over material and receives nothing, which reads as theft.
                errors.Add(new ContentError(
                    "item", item.Key, $"Faction buy price must be positive, got {price}."));
            }

            if (item.Category != ItemCategory.Raw)
            {
                // Raw material only, enforced here rather than trusted to whoever authors the file.
                // A standing bid on a manufactured good puts a price floor under exactly the things
                // players are meant to compete on, and nobody would notice until the market for that
                // item quietly stopped forming.
                errors.Add(new ContentError(
                    "item",
                    item.Key,
                    $"Only Raw items may have a faction buy price; this is {item.Category}."));
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
