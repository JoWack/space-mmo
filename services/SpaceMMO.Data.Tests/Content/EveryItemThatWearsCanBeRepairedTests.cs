using Microsoft.EntityFrameworkCore;
using SpaceMMO.Data.Content;
using SpaceMMO.Data.Entities;
using SpaceMMO.Data.Industry;
using SpaceMMO.Domain.Characters;
using SpaceMMO.Domain.Content;
using SpaceMMO.Domain.Economy;
using SpaceMMO.Domain.Industry;
using SpaceMMO.Domain.Items;
using Xunit;

namespace SpaceMMO.Data.Tests.Content;

/// <summary>
/// Every shipped item that wears can be repaired, at a quarter of what makes it (task 196).
/// </summary>
/// <remarks>
/// <para>
/// <strong>Through the repair service against the loaded pack</strong>, not by reading the recipes back: the service
/// finds the recipe by the item it makes, in the database, and that join is what an item nothing makes would fall
/// through. Such an item breaks on a death and stays broken for good, which nobody would see until it happened.
/// </para>
/// <para>
/// Counted against the pack, never a literal, so authoring a new weapon is checked rather than a number bumped.
/// </para>
/// </remarks>
[Collection(SharedDatabase.Name)]
public sealed class EveryItemThatWearsCanBeRepairedTests(DatabaseFixture fixture) : IAsyncLifetime
{
    private readonly DatabaseFixture _fixture = fixture;

    public async Task InitializeAsync() => await _fixture.ResetAsync();

    public Task DisposeAsync() => Task.CompletedTask;

    [Fact]
    public async Task A_broken_one_of_each_is_priced_at_a_quarter_of_its_recipe()
    {
        ContentPack pack = await ContentLoader.ReadAsync(ContentRoot());

        await using (SpaceMmoDbContext context = _fixture.CreateContext())
        {
            await new ContentLoader(context).LoadAsync(ContentRoot());
        }

        List<ItemContent> wearing = [.. pack.Items.Where(i => i.Category.HasCondition())];
        Assert.NotEmpty(wearing);

        (int characterId, int stationId) = await BrokenOneOfEachAsync(wearing);

        IReadOnlyList<RepairQuote> quotes;

        await using (SpaceMmoDbContext context = _fixture.CreateContext())
        {
            quotes = await new RepairService(context).QuoteAsync(characterId, stationId);
        }

        Assert.Equal(
            wearing.Select(i => i.Key).Order(StringComparer.Ordinal),
            quotes.Select(q => q.ItemKey).Order(StringComparer.Ordinal));

        foreach (RepairQuote quote in quotes)
        {
            Assert.True(quote.Priced, $"Nothing makes {quote.ItemKey}, so it cannot be repaired.");
            Assert.Equal(Repairs.FullCondition, quote.Restored);
            Assert.Equal(IndustryFees.ForJob(1), quote.Credits);

            RecipeContent recipe = pack.Recipes.Single(r => r.Output == quote.ItemKey);

            Assert.Equal(
                recipe.Inputs.Select(i => i.Item).Order(StringComparer.Ordinal),
                quote.Materials.Select(m => m.ItemKey).Order(StringComparer.Ordinal));

            foreach (RepairMaterial material in quote.Materials)
            {
                int input = recipe.Inputs.Single(i => i.Item == material.ItemKey).Quantity;

                // Charged and left on the tab together are a quarter of the input, exactly.
                long owed = ((long)material.Units * Repairs.TabPerUnit) + material.OwedAfterTenThousandths;

                Assert.True(
                    owed * Repairs.RecipeShareDivisor * recipe.OutputQuantity == (long)input * Repairs.TabPerUnit,
                    $"{quote.ItemKey} owes {owed / (double)Repairs.TabPerUnit} {material.ItemKey} of {input}.");
            }
        }
    }

    private async Task<(int CharacterId, int StationId)> BrokenOneOfEachAsync(IEnumerable<ItemContent> wearing)
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        Station station = await context.Stations.OrderBy(s => s.Key).FirstAsync();

        var account = new Account
        {
            Email = "mender@example.com", PasswordHash = "not-a-real-hash", CreatedAt = DateTimeOffset.UtcNow,
        };
        context.Accounts.Add(account);
        await context.SaveChangesAsync();

        var character = new Character
        {
            AccountId = account.Id,
            Name = "Mender",
            Race = Race.Humanoid,
            HomeBodyId = station.BodyId!.Value,
            Balance = Credits.FromWholeCredits(1_000),
            CreatedAt = DateTimeOffset.UtcNow,
            DockedStationId = station.Id,
        };
        context.Characters.Add(character);
        await context.SaveChangesAsync();

        var hangar = new Inventory
        {
            CharacterId = character.Id, Kind = InventoryKind.StationHangar, StationId = station.Id, CapacityM3 = 0,
        };
        context.Inventories.Add(hangar);
        await context.SaveChangesAsync();

        foreach (ItemContent item in wearing)
        {
            context.ItemInstances.Add(new ItemInstance
            {
                ItemDefId = await context.ItemDefs.Where(d => d.Key == item.Key).Select(d => d.Id).SingleAsync(),
                InventoryId = hangar.Id,
                Condition = 0,
                AcquisitionValue = Credits.Zero,
                CreatedAt = DateTimeOffset.UtcNow,
            });
        }

        await context.SaveChangesAsync();

        return (character.Id, station.Id);
    }

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
}
