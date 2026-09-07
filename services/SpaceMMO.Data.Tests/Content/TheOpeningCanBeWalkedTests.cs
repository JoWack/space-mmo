using SpaceMMO.Data.Content;
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

    [Fact]
    public async Task Every_craft_in_the_opening_is_reachable_with_the_skill_it_has_earned()
    {
        ContentPack pack = await ContentLoader.ReadAsync(ContentRoot());

        // Skill key to XP. A new character has none of any of them, which is the whole premise.
        var experience = new Dictionary<string, long>();

        foreach (QuestContent quest in MainStoryInOrder(pack))
        {
            foreach (QuestStepContent step in quest.Steps.OrderBy(s => s.Ordinal))
            {
                if (step.Objective != ObjectiveType.Craft)
                {
                    continue;
                }

                RecipeContent recipe = RecipeFor(pack, step.Target, quest.Key);

                long xp = experience.GetValueOrDefault(recipe.Skill);
                int level = SkillCurve.LevelForXp(xp);

                Assert.True(
                    level >= recipe.RequiredLevel,
                    $"'{quest.Name}' asks for {step.Target}, which needs {recipe.Skill} "
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

    [Fact]
    public async Task Every_ingredient_in_the_opening_can_be_obtained()
    {
        ContentPack pack = await ContentLoader.ReadAsync(ContentRoot());

        foreach (QuestContent quest in MainStoryInOrder(pack))
        {
            foreach (QuestStepContent step in quest.Steps.OrderBy(s => s.Ordinal))
            {
                if (step.Objective != ObjectiveType.Craft)
                {
                    continue;
                }

                RecipeContent recipe = RecipeFor(pack, step.Target, quest.Key);

                foreach (RecipeInputContent input in recipe.Inputs)
                {
                    Assert.True(
                        Obtainable(pack, input.Item, []),
                        $"'{quest.Name}' asks for {step.Target}, which takes {input.Quantity} "
                        + $"{input.Item} — and nothing in the pack gathers, refines, crafts or "
                        + $"sells one. The chain stops here for every player, and no test of any "
                        + "single recipe would notice.");
                }
            }
        }
    }

    /// <summary>
    /// Whether a player can end up holding this item at all.
    /// </summary>
    /// <remarks>
    /// Gathered from a resource node, or crafted from things that are themselves obtainable. The
    /// recursion carries a trail so a recipe that consumes its own output cannot spin: content like
    /// that is a fault of its own, and hanging the suite is a poor way to report one.
    /// </remarks>
    private static bool Obtainable(ContentPack pack, string item, HashSet<string> visiting)
    {
        if (KnownUnobtainable.Contains(item))
        {
            return true;
        }

        if (pack.ResourceNodes.Any(n => n.Item == item))
        {
            return true;
        }

        if (!visiting.Add(item))
        {
            return false;
        }

        bool madeSomehow = pack.Recipes
            .Where(r => r.Output == item)
            .Any(r => r.Inputs.All(i => Obtainable(pack, i.Item, visiting)));

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

    private static RecipeContent RecipeFor(ContentPack pack, string output, string questKey)
    {
        RecipeContent? recipe = pack.Recipes.SingleOrDefault(r => r.Output == output);

        Assert.True(
            recipe is not null,
            $"Quest '{questKey}' asks the player to craft '{output}', and no recipe makes one.");

        return recipe!;
    }
}
