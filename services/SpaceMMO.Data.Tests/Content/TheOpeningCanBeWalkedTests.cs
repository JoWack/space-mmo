using SpaceMMO.Data.Content;
using SpaceMMO.Domain.Characters;
using SpaceMMO.Domain.Content;
using SpaceMMO.Domain.Progression;
using SpaceMMO.Domain.Quests;
using Xunit;

namespace SpaceMMO.Data.Tests.Content;

/// <summary>
/// A new character can actually walk the main-story questline.
/// </summary>
/// <remarks>
/// <para>
/// <strong>The test task 148 exists to leave behind.</strong> The onboarding chain was broken in two
/// places for months while every part of it was individually correct: the recipes loaded, the skill
/// curve was right, the level gate was enforced exactly as designed, the quests chained in the right
/// order, and EconSim ran five simulated years over the same pack. Nothing anywhere asked whether a
/// character starting from nothing could get to the end.
/// </para>
/// <para>
/// So this walks it. Start with no skills and nothing owned, take each quest in prerequisite order,
/// and for every craft assert two things: the skill level is reachable with the XP the previous
/// steps actually paid out, and every input can be obtained. Then bank what the step gives and go on.
/// </para>
/// <para>
/// <strong>It reads the shipped pack rather than a fixture</strong>, like the other content tests,
/// because a fixture would only prove the test agrees with itself. The whole value here is that it
/// is looking at what players get.
/// </para>
/// </remarks>
public sealed class TheOpeningCanBeWalkedTests
{
    /// <summary>Walks up from the test binary to the repository's <c>data/</c> directory.</summary>
    private static string ContentRoot()
    {
        var directory = new DirectoryInfo(AppContext.BaseDirectory);

        while (directory is not null)
        {
            string candidate = Path.Combine(directory.FullName, "data");

            if (Directory.Exists(candidate) && Directory.Exists(Path.Combine(candidate, "items")))
            {
                return candidate;
            }

            directory = directory.Parent;
        }

        throw new DirectoryNotFoundException(
            $"Could not find the repository 'data' directory from {AppContext.BaseDirectory}.");
    }

    /// <summary>
    /// Items no player can make, which is meant to be none of them.
    /// </summary>
    /// <remarks>
    /// <para>
    /// <strong>Empty, and it is supposed to stay that way.</strong> Everything is player-made
    /// (design bible §5). It held <c>crude_thruster</c> for one commit, because the content had
    /// authored it as the single exception to that rule and the faction supply order meant to sell
    /// it in the meantime was never built — so the questline could not be finished at all.
    /// </para>
    /// <para>
    /// <strong>Kept rather than deleted</strong>, because the next thing authored as an exception
    /// should have to be written down here, in a diff, next to a comment saying the rule is that
    /// there are none. An allowlist that grows is the failure this shape invites; the cost of
    /// adding to it is meant to be an argument.
    /// </para>
    /// </remarks>
    private static readonly HashSet<string> KnownUnobtainable = [];

    /// <summary>Every race, because the opening is walked from each homeworld separately.</summary>
    public static IEnumerable<object[]> EveryRace() =>
        Enum.GetValues<Race>().Select(race => new object[] { race });

    /// <summary>
    /// Whether a step is made at an industry station, and so has a recipe to walk.
    /// </summary>
    /// <remarks>
    /// <strong>Refining as well as crafting, since task 185.</strong> This walked only
    /// <c>Craft</c> steps, so quest 4, which refines plate, was never walked at all. It did not matter
    /// while one recipe made plate from an ore every world could reach; it does once each world refines
    /// its own.
    /// </remarks>
    private static bool IsMadeAtAStation(QuestStepContent step) =>
        step.Objective is ObjectiveType.Craft or ObjectiveType.Refine;

    [Theory]
    [MemberData(nameof(EveryRace))]
    public async Task Every_craft_in_the_opening_is_reachable_with_the_skill_it_has_earned(Race race)
    {
        ContentPack pack = await ContentLoader.ReadAsync(ContentRoot());

        string home = Races.HomeBodyKeyFor(race);

        // Skill key to XP. A new character has none of any of them, which is the whole premise.
        var experience = new Dictionary<string, long>();

        foreach (QuestContent quest in MainStoryInOrder(pack))
        {
            foreach (QuestStepContent step in quest.Steps.OrderBy(s => s.Ordinal))
            {
                if (!IsMadeAtAStation(step))
                {
                    continue;
                }

                RecipeContent recipe = RecipeFor(pack, MadeItem(step, quest), quest.Key, home, race);

                long xp = experience.GetValueOrDefault(recipe.Skill);
                int level = SkillCurve.LevelForXp(xp);

                Assert.True(
                    level >= recipe.RequiredLevel,
                    $"'{quest.Name}' asks a {race} for {recipe.Output}, which needs {recipe.Skill} "
                    + $"{recipe.RequiredLevel}. By this point the questline has paid out {xp} "
                    + $"{recipe.Skill} XP, which is level {level}. Nothing earlier in the chain can "
                    + $"close the gap: check what else grants {recipe.Skill} XP and what it costs "
                    + "to reach.");

                // What running it pays, banked before the quest reward, because that is the order
                // it happens in: the craft completes, then the quest that asked for it does.
                experience[recipe.Skill] = xp + recipe.XpPerRun;
            }

            if (quest.RewardSkill is string rewarded && quest.RewardXp > 0)
            {
                experience[rewarded] = experience.GetValueOrDefault(rewarded) + quest.RewardXp;
            }
        }
    }

    /// <summary>
    /// Every ingredient of every craft and refine in the opening can be had on the race's own world.
    /// </summary>
    /// <remarks>
    /// <para>
    /// <strong>At home, not anywhere, since task 185.</strong> ADR-0008: the onboarding chain stays
    /// "self-sufficient within a race, so a new player never stalls waiting for a market that may have
    /// no sellers". This asked whether anything anywhere produced each ingredient, which every race
    /// passed because ferrite was on the Capital. Since 184 each world has its own ore, and a world
    /// with no recipe for it would leave its new players holding ore they cannot use.
    /// </para>
    /// <para>
    /// Gather steps are walked by <see cref="Every_gather_in_the_opening_can_be_done_at_home"/>.
    /// </para>
    /// </remarks>
    [Theory]
    [MemberData(nameof(EveryRace))]
    public async Task Every_ingredient_in_the_opening_can_be_obtained_at_home(Race race)
    {
        ContentPack pack = await ContentLoader.ReadAsync(ContentRoot());

        string home = Races.HomeBodyKeyFor(race);

        foreach (QuestContent quest in MainStoryInOrder(pack))
        {
            foreach (QuestStepContent step in quest.Steps.OrderBy(s => s.Ordinal))
            {
                if (!IsMadeAtAStation(step))
                {
                    continue;
                }

                // RecipeFor fails, naming the step, when no recipe for the target can be fed from home.
                RecipeContent recipe = RecipeFor(pack, MadeItem(step, quest), quest.Key, home, race);

                foreach (RecipeInputContent input in recipe.Inputs)
                {
                    Assert.True(
                        ObtainableAt(pack, home, input.Item, []),
                        $"'{quest.Name}' asks a {race} for {recipe.Output}, which takes "
                        + $"{input.Quantity} {input.Item} — and nothing on {home} gathers, refines or "
                        + "crafts one.");
                }
            }
        }
    }

    /// <summary>
    /// Every gather step in the opening can be done from a deposit on the race's own world.
    /// </summary>
    /// <remarks>
    /// <strong>Task 186.</strong> Until a step could name a family, quest 3 asked every race for
    /// <c>ferrite_ore</c>, which since 184 is Terra's alone, so a Martian at home could mine their own
    /// ore all day and never advance it. A step that names a family is satisfied by any member, and
    /// this asks whether one of those members can be gathered at home.
    /// </remarks>
    [Theory]
    [MemberData(nameof(EveryRace))]
    public async Task Every_gather_in_the_opening_can_be_done_at_home(Race race)
    {
        ContentPack pack = await ContentLoader.ReadAsync(ContentRoot());

        string home = Races.HomeBodyKeyFor(race);

        foreach (QuestContent quest in MainStoryInOrder(pack))
        {
            foreach (QuestStepContent step in quest.Steps.OrderBy(s => s.Ordinal))
            {
                if (step.Objective != ObjectiveType.Gather)
                {
                    continue;
                }

                string[] accepted = step.TargetFamily is string family
                    ? [.. pack.Items.Where(i => i.Family == family).Select(i => i.Key)]
                    : [step.Target!];

                Assert.True(
                    pack.ResourceNodes.Any(n => n.Body == home && accepted.Contains(n.Item)),
                    $"'{quest.Name}' asks a {race} to gather {step.Target ?? step.TargetFamily}, "
                    + $"and nothing on {home} yields {string.Join(" or ", accepted)}. A {race} "
                    + "starting at home stops at this step.");
            }
        }
    }

    /// <summary>The item a craft or refine step makes. Those always name one item.</summary>
    private static string MadeItem(QuestStepContent step, QuestContent quest)
    {
        Assert.True(
            step.Target is not null,
            $"'{quest.Name}' step {step.Ordinal} makes something and names no item to make.");

        return step.Target!;
    }

    /// <summary>
    /// Whether a player on one world can end up holding this item without leaving it.
    /// </summary>
    /// <remarks>
    /// Gathered from a deposit on that world, or made from things that are themselves obtainable
    /// there. The recursion carries a trail so a recipe that consumes its own output cannot spin:
    /// content like that is a fault of its own, and hanging the suite is a poor way to report one.
    /// </remarks>
    private static bool ObtainableAt(
        ContentPack pack, string body, string item, HashSet<string> visiting)
    {
        if (KnownUnobtainable.Contains(item))
        {
            return true;
        }

        if (pack.ResourceNodes.Any(n => n.Item == item && n.Body == body))
        {
            return true;
        }

        if (!visiting.Add(item))
        {
            return false;
        }

        bool madeSomehow = pack.Recipes
            .Where(r => r.Output == item)
            .Any(r => r.Inputs.All(i => ObtainableAt(pack, body, i.Item, visiting)));

        visiting.Remove(item);

        return madeSomehow;
    }

    /// <summary>
    /// The main story, in the order a player meets it.
    /// </summary>
    /// <remarks>
    /// Followed by prerequisite rather than trusting the order in the file, because the file's order
    /// is a convenience and the prerequisite chain is the rule. A quest that lost its place in the
    /// list would otherwise be walked at the wrong point and quietly pass.
    /// </remarks>
    private static List<QuestContent> MainStoryInOrder(ContentPack pack)
    {
        List<QuestContent> story = [.. pack.Quests.Where(q => q.Kind == QuestKind.MainStory)];

        Assert.NotEmpty(story);

        var ordered = new List<QuestContent>();
        var placed = new HashSet<string>();

        while (ordered.Count < story.Count)
        {
            QuestContent? next = story.FirstOrDefault(q =>
                !placed.Contains(q.Key)
                && (q.Prerequisite is null || placed.Contains(q.Prerequisite)));

            Assert.True(
                next is not null,
                "The main story does not form a chain: some quest's prerequisite is missing, or two "
                + "of them wait on each other. Whichever it is, no player reaches the end.");

            ordered.Add(next!);
            placed.Add(next!.Key);
        }

        return ordered;
    }

    /// <summary>
    /// The recipe a character on this world would use to make an item: one whose inputs can all be
    /// had there, and the easiest of those to reach.
    /// </summary>
    /// <remarks>
    /// <strong>Not "the" recipe, since task 185.</strong> This took the single recipe for an output,
    /// and four now make plate — one per homeworld's ore — so it would have thrown on the second. The
    /// right one depends on where the player is standing, which is the whole point of having four.
    /// </remarks>
    private static RecipeContent RecipeFor(
        ContentPack pack, string output, string questKey, string home, Race race)
    {
        RecipeContent[] makers = [.. pack.Recipes.Where(r => r.Output == output)];

        Assert.True(
            makers.Length > 0,
            $"Quest '{questKey}' asks the player to make '{output}', and no recipe makes one.");

        RecipeContent? recipe = makers
            .Where(r => r.Inputs.All(i => ObtainableAt(pack, home, i.Item, [])))
            .OrderBy(r => r.RequiredLevel)
            .ThenBy(r => r.Key, StringComparer.Ordinal)
            .FirstOrDefault();

        Assert.True(
            recipe is not null,
            $"Quest '{questKey}' asks a {race} for '{output}', and none of the "
            + $"{makers.Length} recipe(s) that make one can be fed from {home}: "
            + string.Join("; ", makers.Select(r =>
                $"{r.Key} takes {string.Join(", ", r.Inputs.Select(i => i.Item))}"))
            + ".");

        return recipe!;
    }
}
