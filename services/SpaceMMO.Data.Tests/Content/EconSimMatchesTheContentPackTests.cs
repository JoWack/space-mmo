using SpaceMMO.Data.Content;
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
    public async Task Refining_matches_the_shipped_recipe()
    {
        ContentPack pack = await ContentLoader.ReadAsync(ContentRoot());

        RecipeContent refining = Recipe(pack, Sim.RefiningRecipeKey);

        Assert.Equal(Sim.OrePerRefiningRun, InputQuantity(refining, Sim.Ore));
        Assert.Equal(Sim.PlatesPerRefiningRun, refining.OutputQuantity);
        Assert.Equal(Sim.Plate, refining.Output);
        Assert.Equal(Sim.RefiningJobSeconds, refining.JobSeconds);
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
