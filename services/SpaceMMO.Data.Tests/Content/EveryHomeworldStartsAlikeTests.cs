using System.Globalization;
using SpaceMMO.Data.Content;
using SpaceMMO.Domain.Characters;
using SpaceMMO.Domain.Content;
using SpaceMMO.Domain.Quests;
using Xunit;

namespace SpaceMMO.Data.Tests.Content;

/// <summary>
/// Every race's homeworld carries what the opening gathers, and none carries more than another.
/// </summary>
/// <remarks>
/// <para>
/// <strong>Task 184.</strong> Until 9 October 2026 every scrap and ferrite deposit was on the
/// Capital and each homeworld had one level-15 rock, so a new character started on their own world
/// (task 145) could not have finished quest 1. Joe decided that day that scrap is common to every
/// world and that each world's ore is its own: Terra keeps ferrite, and Ares, Verdance and Grimhold
/// have oxide salt, rootstone and blackiron ore.
/// </para>
/// <para>
/// <strong>It reads the shipped pack and compares the worlds with each other</strong>, rather than
/// with numbers written here. Retuning all four worlds together is a content change; retuning one
/// is the failure, because "mechanically equivalent, materially distinct" (design-bible §4) is the
/// promise that no race's first hour is easier than another's.
/// </para>
/// </remarks>
public sealed class EveryHomeworldStartsAlikeTests
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

    [Fact]
    public async Task Every_homeworld_has_the_common_materials_and_an_ore_of_its_own()
    {
        ContentPack pack = await ContentLoader.ReadAsync(ContentRoot());

        string[] common = CommonMaterialsTheOpeningGathers(pack);

        // Not vacuous: if the questline stopped gathering anything common, the loop below would
        // check nothing and pass.
        Assert.NotEmpty(common);

        foreach (Race race in Enum.GetValues<Race>())
        {
            string home = Races.HomeBodyKeyFor(race);

            ResourceNodeContent[] deposits = [.. pack.ResourceNodes.Where(n => n.Body == home)];

            foreach (string item in common)
            {
                Assert.True(
                    deposits.Any(d => d.Item == item),
                    $"The opening gathers {item}, and {home}, the {race} homeworld, has none. A "
                    + $"{race} character starting at home stops at that step.");
            }

            string[] ownOres = OwnOres(pack, deposits);

            Assert.True(
                ownOres.Length == 1,
                $"{home}, the {race} homeworld, has {ownOres.Length} starter ores locked to it "
                + $"({string.Join(", ", ownOres)}); it should have exactly one. A starter ore is "
                + "planet-locked and mined at level 1.");
        }
    }

    [Fact]
    public async Task No_homeworld_starts_better_provisioned_than_another()
    {
        ContentPack pack = await ContentLoader.ReadAsync(ContentRoot());

        string[] common = CommonMaterialsTheOpeningGathers(pack);

        Race[] races = Enum.GetValues<Race>();
        Race baseline = races[0];
        string baselineProvision = Provision(pack, Races.HomeBodyKeyFor(baseline), common);

        foreach (Race race in races.Skip(1))
        {
            string provision = Provision(pack, Races.HomeBodyKeyFor(race), common);

            Assert.True(
                provision == baselineProvision,
                $"The {race} homeworld starts differently from the {baseline} one.{Environment.NewLine}"
                + $"{baseline}:{Environment.NewLine}{baselineProvision}{Environment.NewLine}"
                + $"{race}:{Environment.NewLine}{provision}");
        }
    }

    /// <summary>
    /// What the main story asks a character to gather that is not locked to any one world.
    /// </summary>
    /// <remarks>
    /// Read from the questline, so a second common material joins this test by being asked for
    /// rather than by being written down here. Scrap, today.
    /// </remarks>
    private static string[] CommonMaterialsTheOpeningGathers(ContentPack pack) =>
    [
        .. pack.Quests
            .Where(q => q.Kind == QuestKind.MainStory)
            .SelectMany(q => q.Steps)
            .Where(s => s.Objective == ObjectiveType.Gather)
            .Select(s => s.Target)
            .Where(target => pack.Items.Any(i => i.Key == target && !i.PlanetLocked))
            .Distinct()
            .Order(),
    ];

    /// <summary>The planet-locked materials on a world that a new character can work.</summary>
    private static string[] OwnOres(ContentPack pack, IEnumerable<ResourceNodeContent> deposits) =>
    [
        .. deposits
            .Where(d => d.RequiredLevel == 1)
            .Where(d => pack.Items.Single(i => i.Key == d.Item).PlanetLocked)
            .Select(d => d.Item)
            .Distinct()
            .Order(),
    ];

    /// <summary>
    /// Everything about a world's start that could make it easier or harder, with the name of its
    /// own ore left out — that is the one thing meant to differ.
    /// </summary>
    private static string Provision(ContentPack pack, string body, string[] common)
    {
        ResourceNodeContent[] deposits = [.. pack.ResourceNodes.Where(n => n.Body == body)];

        string[] ownOres = OwnOres(pack, deposits);

        IEnumerable<string> lines = deposits
            .Where(d => common.Contains(d.Item) || ownOres.Contains(d.Item))
            .Select(d =>
            {
                ItemContent item = pack.Items.Single(i => i.Key == d.Item);

                string what = common.Contains(d.Item) ? d.Item : "its own ore";

                return string.Create(
                    CultureInfo.InvariantCulture,
                    $"  {what}: {d.Skill} {d.RequiredLevel}, tool {d.RequiredTool ?? "none"}, "
                    + $"{d.QuantityMax} every {d.RespawnSeconds} s, {item.VolumeM3} m3, "
                    + $"faction price {item.FactionBuyPrice}");
            })
            .Order();

        return string.Join(Environment.NewLine, lines);
    }
}
