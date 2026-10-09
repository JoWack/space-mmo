using SpaceMMO.Data.Content;
using SpaceMMO.Domain.Characters;
using SpaceMMO.Domain.Content;
using SpaceMMO.EconSim;
using Xunit;

namespace SpaceMMO.Data.Tests.Content;

/// <summary>
/// The economy simulator has to be simulating the recipes the game actually ships.
/// </summary>
/// <remarks>
/// <para>
/// <strong>EconSim restates the recipes rather than reading them, and it has to.</strong> It
/// references SpaceMMO.Domain and nothing else — that is what lets it run tens of thousands of days
/// in seconds — and ADR-0003 forbids file I/O in Domain outright. So the numbers cannot come from
/// the pack without either a second JSON reader inside the tool or the SpaceMMO.Data reference its
/// project file explicitly rules out.
/// </para>
/// <para>
/// Restating them is safe only if something notices when they drift, and nothing did. The comments
/// knew — <c>Sim.OrePerFrame</c> cites <c>build_alloy_frame</c> by name — and a comment does not
/// fail a build.
/// </para>
/// <para>
/// <strong>Why this matters more than an ordinary duplication.</strong> EconSim's output justifies
/// decisions: ADR-0008 rests on alloy frames staying worth building, and the freighter exists
/// because a five-year run found frames piling up unsold. A sim quietly modelling a recipe nobody
/// ships answers the question anyway, confidently, about a game that no longer exists — and unlike
/// a crash it produces perfectly plausible numbers while doing it.
/// </para>
/// <para>
/// Reads the real <c>data/</c> directory, like <see cref="ContentLoaderTests"/>, because the point
/// is the shipped content and a fixture would only prove the test agrees with itself.
/// </para>
/// </remarks>
public sealed class EconSimMatchesTheContentPackTests
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

    private static RecipeContent Recipe(ContentPack pack, string key)
    {
        RecipeContent? recipe = pack.Recipes.SingleOrDefault(r => r.Key == key);

        Assert.True(
            recipe is not null,
            $"EconSim simulates '{key}', and no recipe with that key is shipped. Either the recipe "
            + "was renamed and the simulator not told, or it was removed and the simulator is now "
            + "modelling a step of the economy that no longer exists.");

        return recipe!;
    }

    private static int InputQuantity(RecipeContent recipe, string itemKey)
    {
        RecipeInputContent? input = recipe.Inputs.SingleOrDefault(i => i.Item == itemKey);

        Assert.True(
            input is not null,
            $"EconSim feeds '{itemKey}' into '{recipe.Key}', and the shipped recipe does not take "
            + "it.");

        return input!.Quantity;
    }

    [Fact]
    public async Task Refining_matches_the_shipped_recipes()
    {
        // One per starter ore since task 185, all with the numbers the simulation shares between
        // them. A world whose recipe was retuned alone would fail here rather than quietly running
        // the old numbers.
        ContentPack pack = await ContentLoader.ReadAsync(ContentRoot());

        foreach (string ore in Sim.StarterOres)
        {
            RecipeContent refining = Recipe(pack, Sim.RefiningRecipeKeyFor(ore));

            Assert.Equal(Sim.OrePerRefiningRun, InputQuantity(refining, ore));
            Assert.Equal(Sim.PlatesPerRefiningRun, refining.OutputQuantity);
            Assert.Equal(Sim.Plate, refining.Output);
            Assert.Equal(Sim.RefiningJobSeconds, refining.JobSeconds);
        }

        // And the simulation refines every ore the pack refines into plate, so a fifth world's
        // recipe cannot be shipped without the simulation hearing of it.
        string[] shipped = [.. pack.Recipes
            .Where(r => r.Output == Sim.Plate)
            .Select(r => r.Key)
            .Order(StringComparer.Ordinal)];

        string[] simulated = [.. Sim.StarterOres
            .Select(Sim.RefiningRecipeKeyFor)
            .Order(StringComparer.Ordinal)];

        Assert.Equal(shipped, simulated);
    }

    [Fact]
    public async Task Each_homeworld_has_the_ores_the_simulation_mines_there()
    {
        // The simulation names each world's two ores in code (Sim.StarterOreOf and Sim.OreOf). If
        // either disagrees with where content puts them, its miners gather on a world that has none.
        ContentPack pack = await ContentLoader.ReadAsync(ContentRoot());

        foreach (Race race in Enum.GetValues<Race>())
        {
            string home = Races.HomeBodyKeyFor(race);

            foreach (string ore in new[] { Sim.StarterOreOf(home), Sim.OreOf(home) })
            {
                Assert.True(
                    pack.ResourceNodes.Any(n => n.Body == home && n.Item == ore),
                    $"EconSim mines {ore} on {home}, and the pack puts none there.");
            }
        }
    }

    [Fact]
    public async Task Deposits_match_the_shipped_universe()
    {
        // SimulationConfig.Deposits is transcribed from origin.json, and drifted twice with nothing
        // to say so: Ares has had two regolith deposits since task 158 while it said one, and the
        // capital had three ferrite deposits while it said two. Supply is the ceiling on everything
        // the simulation reports, so a wrong count is a wrong answer about the whole economy.
        ContentPack pack = await ContentLoader.ReadAsync(ContentRoot());

        var config = new SimulationConfig();

        foreach (string item in Sim.StarterOres.Concat(Sim.PlanetLockedOres))
        {
            Assert.True(
                config.Deposits.TryGetValue(item, out SimulationConfig.Deposit deposit),
                $"EconSim mines {item} and has no deposits configured for it.");

            ResourceNodeContent[] nodes = [.. pack.ResourceNodes.Where(n => n.Item == item)];

            Assert.True(
                deposit.Count == nodes.Length,
                $"EconSim models {deposit.Count} {item} deposit(s); the pack has {nodes.Length}.");

            foreach (ResourceNodeContent node in nodes)
            {
                Assert.True(
                    deposit.Capacity == node.QuantityMax && deposit.RespawnSeconds == node.RespawnSeconds,
                    $"{node.Key} holds {node.QuantityMax} every {node.RespawnSeconds} s; EconSim "
                    + $"models {deposit.Capacity} every {deposit.RespawnSeconds} s.");
            }
        }
    }

    [Fact]
    public async Task Shipcrafting_matches_the_shipped_recipe()
    {
        ContentPack pack = await ContentLoader.ReadAsync(ContentRoot());

        RecipeContent section = Recipe(pack, Sim.ShipcraftingRecipeKey);

        Assert.Equal(Sim.PlatesPerSectionRun, InputQuantity(section, Sim.Plate));
        Assert.Equal(Sim.SectionsPerSectionRun, section.OutputQuantity);
        Assert.Equal(Sim.Section, section.Output);
        Assert.Equal(Sim.ShipcraftingJobSeconds, section.JobSeconds);
    }

    [Fact]
    public async Task The_frame_still_costs_ten_of_each_planet_locked_ore()
    {
        // The one ADR-0008 rests on. A frame is the point at which material has to cross the
        // faction line, so if this ever stops being four ores it stops being the reason anybody
        // from one faction buys from the other, and the contested zone becomes scenery.
        ContentPack pack = await ContentLoader.ReadAsync(ContentRoot());

        RecipeContent frame = Recipe(pack, Sim.FrameRecipeKey);

        Assert.Equal(Sim.Frame, frame.Output);

        foreach (string ore in Sim.PlanetLockedOres)
        {
            Assert.Equal(Sim.OrePerFrame, InputQuantity(frame, ore));
        }

        // And nothing else goes into it, or the sim is buying the wrong shopping list.
        Assert.Equal(Sim.PlanetLockedOres.Length, frame.Inputs.Count);
    }

    [Fact]
    public async Task Every_item_the_simulation_trades_is_shipped()
    {
        // Keys, not just quantities. A renamed item leaves the sim trading something nobody can
        // buy, and the market clears at whatever price nothing sells for.
        ContentPack pack = await ContentLoader.ReadAsync(ContentRoot());

        foreach (string key in Sim.TradedItems)
        {
            Assert.True(
                pack.Items.Any(i => i.Key == key),
                $"EconSim trades '{key}' and no item with that key is shipped.");
        }
    }
}
