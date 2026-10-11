using Microsoft.EntityFrameworkCore;
using SpaceMMO.Data.Content;
using SpaceMMO.Data.Entities;
using SpaceMMO.Data.Industry;
using SpaceMMO.Data.Inventories;
using SpaceMMO.Data.Market;
using SpaceMMO.Domain.Characters;
using SpaceMMO.Domain.Economy;
using SpaceMMO.Domain.Items;
using SpaceMMO.Domain.Market;
using SpaceMMO.Domain.Progression;
using Xunit;

namespace SpaceMMO.Data.Tests.Industry;

/// <summary>
/// A crafted good takes its style from its main input, and keeps it wherever it goes (task 187).
/// </summary>
/// <remarks>
/// <para>
/// <strong>Against the shipped content, through the real services.</strong> The rule is spread
/// across content (which ore belongs to which world, which input is a recipe's main one), the
/// industry service (which decides), and the inventory and market services (which must not lose
/// it). A handmade fixture would prove the services agree with the fixture; this proves a Martian
/// refining oxide salt at home gets Martian plate.
/// </para>
/// <para>
/// Every test starts from a freshly loaded pack, so the item and recipe ids are whatever the loader
/// gave them, and every lookup is by key.
/// </para>
/// </remarks>
[Collection(SharedDatabase.Name)]
public sealed class ItemStyleTests(DatabaseFixture fixture) : IAsyncLifetime
{
    private readonly DatabaseFixture _fixture = fixture;

    public async Task InitializeAsync()
    {
        await _fixture.ResetAsync();

        await using SpaceMmoDbContext context = _fixture.CreateContext();
        await new ContentLoader(context).LoadAsync(ContentRoot());
    }

    public Task DisposeAsync() => Task.CompletedTask;

    public static IEnumerable<object[]> EveryRace() =>
        Enum.GetValues<Race>().Select(race => new object[] { race });

    // ── The opening, at home ─────────────────────────────────────────────────

    [Theory]
    [MemberData(nameof(EveryRace))]
    public async Task Every_race_builds_a_shuttle_in_its_own_worlds_style_at_home(Race race)
    {
        // The whole chain, from the world's own ore: plate, thruster, hull section, shuttle. Every
        // step's main input carries the style forward, and the hull at the end is the race's.
        Crafter crafter = await CrafterAsync(race, "Builder");

        string ore = await HomeOreAsync(race);

        await GiveAsync(crafter, ore, 60);
        await GiveAsync(crafter, "scrap_alloy", 40);

        ItemStyle expected = ItemStyles.ForRace(race);

        await MakeAsync(crafter, RefineRecipeFor(ore), runs: 3);
        Assert.Equal(12, await HeldAsync(crafter, "ferrite_plate", expected));

        await MakeAsync(crafter, "craft_crude_thruster", runs: 1);
        Assert.Equal(1, await HeldAsync(crafter, "crude_thruster", expected));

        await MakeAsync(crafter, "build_shuttle_hull_section", runs: 1);
        Assert.Equal(1, await HeldAsync(crafter, "shuttle_hull_section", expected));

        await MakeAsync(crafter, "assemble_hull_shuttle", runs: 1);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        ItemInstance hull = await verify.ItemInstances
            .SingleAsync(i => i.ItemDef!.Key == "hull_shuttle");

        Assert.Equal(expected, hull.Style);
    }

    [Theory]
    [MemberData(nameof(EveryRace))]
    public async Task A_crude_laser_made_from_scrap_alone_is_Common_whoever_makes_it(Race race)
    {
        // Joe, 10 October: "Goods made from common materials should all have the same style".
        Crafter crafter = await CrafterAsync(race, "Tinker");

        await GiveAsync(crafter, "scrap_alloy", 8);

        await MakeAsync(crafter, "craft_crude_mining_laser", runs: 1);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        ItemInstance laser = await verify.ItemInstances
            .SingleAsync(i => i.ItemDef!.Key == "crude_mining_laser");

        Assert.Equal(ItemStyle.Common, laser.Style);
    }

    // ── The main input ───────────────────────────────────────────────────────

    [Fact]
    public async Task The_picked_style_of_the_main_input_is_consumed_and_decides_the_output()
    {
        Crafter crafter = await CrafterAsync(Race.Humanoid, "Picker");

        await GiveAsync(crafter, "ferrite_plate", 2, ItemStyle.Terran);
        await GiveAsync(crafter, "ferrite_plate", 2, ItemStyle.Martian);
        await GiveAsync(crafter, "scrap_alloy", 4);

        await MakeAsync(crafter, "craft_crude_thruster", runs: 1, ItemStyle.Martian);

        // A Terran made a Martian thruster, because the plate was Martian: style follows the
        // materials, not the maker.
        Assert.Equal(1, await HeldAsync(crafter, "crude_thruster", ItemStyle.Martian));
        Assert.Equal(0, await HeldAsync(crafter, "ferrite_plate", ItemStyle.Martian));
        Assert.Equal(2, await HeldAsync(crafter, "ferrite_plate", ItemStyle.Terran));
    }

    [Fact]
    public async Task Without_a_pick_the_style_held_most_of_is_used()
    {
        Crafter crafter = await CrafterAsync(Race.Humanoid, "Unpicked");

        await GiveAsync(crafter, "ferrite_plate", 2, ItemStyle.Terran);
        await GiveAsync(crafter, "ferrite_plate", 6, ItemStyle.Verdant);
        await GiveAsync(crafter, "scrap_alloy", 4);

        await MakeAsync(crafter, "craft_crude_thruster", runs: 1);

        Assert.Equal(1, await HeldAsync(crafter, "crude_thruster", ItemStyle.Verdant));
        Assert.Equal(4, await HeldAsync(crafter, "ferrite_plate", ItemStyle.Verdant));
        Assert.Equal(2, await HeldAsync(crafter, "ferrite_plate", ItemStyle.Terran));
    }

    [Fact]
    public async Task A_main_input_is_never_made_up_from_two_styles()
    {
        // One Terran and one Martian plate are two plates, and a thruster needs two -- but a thruster
        // made of both has no one style to be, so the job is refused rather than mislabelled.
        Crafter crafter = await CrafterAsync(Race.Humanoid, "Mixer");

        await GiveAsync(crafter, "ferrite_plate", 1, ItemStyle.Terran);
        await GiveAsync(crafter, "ferrite_plate", 1, ItemStyle.Martian);
        await GiveAsync(crafter, "scrap_alloy", 4);

        int thruster = await RecipeIdAsync("craft_crude_thruster");

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        await Assert.ThrowsAsync<InsufficientItemsException>(() =>
            new IndustryService(context).StartJobAsync(
                crafter.CharacterId, thruster, crafter.StationId,
                runs: 1, ItemStyle.Terran));

        Assert.Equal(1, await HeldAsync(crafter, "ferrite_plate", ItemStyle.Terran));
        Assert.Equal(1, await HeldAsync(crafter, "ferrite_plate", ItemStyle.Martian));
    }

    [Fact]
    public async Task A_pick_held_none_of_is_refused_rather_than_corrected()
    {
        Crafter crafter = await CrafterAsync(Race.Humanoid, "Wishful");

        await GiveAsync(crafter, "ferrite_plate", 4, ItemStyle.Terran);
        await GiveAsync(crafter, "scrap_alloy", 4);

        int thruster = await RecipeIdAsync("craft_crude_thruster");

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        await Assert.ThrowsAsync<InsufficientItemsException>(() =>
            new IndustryService(context).StartJobAsync(
                crafter.CharacterId, thruster, crafter.StationId,
                runs: 1, ItemStyle.Grimhold));

        Assert.Equal(4, await HeldAsync(crafter, "ferrite_plate", ItemStyle.Terran));
        Assert.Equal(4, await HeldAsync(crafter, "scrap_alloy", ItemStyle.Common));
    }

    [Fact]
    public async Task A_raw_main_input_decides_by_its_world_whatever_is_asked()
    {
        // Ferrite ore is Terran wherever it is. Asking for Martian plate from it gets Terran plate:
        // the ore has one stack and one world.
        Crafter crafter = await CrafterAsync(Race.Martian, "Importer");

        await GiveAsync(crafter, "ferrite_ore", 20);

        await MakeAsync(crafter, "refine_ferrite_plate", runs: 1, ItemStyle.Martian);

        Assert.Equal(4, await HeldAsync(crafter, "ferrite_plate", ItemStyle.Terran));
    }

    [Fact]
    public async Task Ordinary_inputs_are_drawn_in_the_outputs_style_first()
    {
        // The shuttle's main input is the hull section. Its thruster is an ordinary input, drawn from
        // any style -- but the section's first, so a Martian shuttle takes the Martian thruster and
        // leaves the Terran one.
        Crafter crafter = await CrafterAsync(Race.Humanoid, "Assembler");

        await GiveAsync(crafter, "shuttle_hull_section", 1, ItemStyle.Martian);
        await GiveAsync(crafter, "crude_thruster", 3, ItemStyle.Terran);
        await GiveAsync(crafter, "crude_thruster", 1, ItemStyle.Martian);

        await MakeAsync(crafter, "assemble_hull_shuttle", runs: 1);

        Assert.Equal(0, await HeldAsync(crafter, "crude_thruster", ItemStyle.Martian));
        Assert.Equal(3, await HeldAsync(crafter, "crude_thruster", ItemStyle.Terran));
    }

    [Fact]
    public async Task Pockets_top_up_the_picked_style_only()
    {
        // Standing in the station holding Martian plate, with Terran plate in the hangar: picking
        // Martian takes it from their hands and leaves the hangar's alone.
        Crafter crafter = await CrafterAsync(Race.Humanoid, "Carrier");

        await GiveAsync(crafter, "ferrite_plate", 4, ItemStyle.Terran);
        await GiveAsync(crafter, "scrap_alloy", 4);
        await CarryAsync(crafter, "ferrite_plate", 2, ItemStyle.Martian);

        await MakeAsync(crafter, "craft_crude_thruster", runs: 1, ItemStyle.Martian);

        Assert.Equal(1, await HeldAsync(crafter, "crude_thruster", ItemStyle.Martian));
        Assert.Equal(4, await HeldAsync(crafter, "ferrite_plate", ItemStyle.Terran));
        Assert.Equal(0, await HeldAsync(crafter, "ferrite_plate", ItemStyle.Martian));
    }

    [Fact]
    public async Task Cancelling_returns_each_input_to_the_style_it_came_from()
    {
        Crafter crafter = await CrafterAsync(Race.Humanoid, "Quitter");

        await GiveAsync(crafter, "ferrite_plate", 2, ItemStyle.Terran);
        await GiveAsync(crafter, "ferrite_plate", 2, ItemStyle.Grimhold);
        await GiveAsync(crafter, "scrap_alloy", 4);

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        StartJobResult started = await new IndustryService(context).StartJobAsync(
            crafter.CharacterId, await RecipeIdAsync("craft_crude_thruster"), crafter.StationId,
            runs: 1, ItemStyle.Grimhold);

        await using SpaceMmoDbContext cancel = _fixture.CreateContext();

        Assert.True(await new IndustryService(cancel).CancelJobAsync(started.JobId, crafter.CharacterId));

        // Cancelled at once, so everything comes back, and back where it was.
        Assert.Equal(2, await HeldAsync(crafter, "ferrite_plate", ItemStyle.Grimhold));
        Assert.Equal(2, await HeldAsync(crafter, "ferrite_plate", ItemStyle.Terran));
    }

    // ── Keeping it ───────────────────────────────────────────────────────────

    [Fact]
    public async Task A_raw_material_named_by_its_world_is_still_one_stack()
    {
        // The client sends the style it shows, and for ferrite ore it shows Terran. That must find
        // the one stack there is rather than starting a second beside it.
        Crafter crafter = await CrafterAsync(Race.Humanoid, "Miner");

        await GiveAsync(crafter, "ferrite_ore", 10);
        await GiveAsync(crafter, "ferrite_ore", 5, ItemStyle.Terran);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        InventoryItem stack = await verify.InventoryItems
            .SingleAsync(i => i.ItemDef!.Key == "ferrite_ore");

        Assert.Equal(15, stack.Quantity);
        Assert.Equal(ItemStyle.Common, stack.Style);
    }

    [Fact]
    public async Task Moving_goods_moves_their_style()
    {
        Crafter crafter = await CrafterAsync(Race.Humanoid, "Mover");

        await GiveAsync(crafter, "ferrite_plate", 5, ItemStyle.Verdant);
        await GiveAsync(crafter, "ferrite_plate", 5, ItemStyle.Terran);

        await using (SpaceMmoDbContext context = _fixture.CreateContext())
        {
            var inventories = new InventoryService(context);

            Inventory pockets = await inventories.GetOrCreateCarriedAsync(crafter.CharacterId);

            await inventories.TransferAsync(
                crafter.HangarId, pockets.Id, await ItemIdAsync("ferrite_plate"), 3, ItemStyle.Verdant);

            await context.SaveChangesAsync();
        }

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        InventoryItem carried = await verify.InventoryItems
            .SingleAsync(i => i.Inventory!.Kind == InventoryKind.CharacterCarried);

        Assert.Equal(ItemStyle.Verdant, carried.Style);
        Assert.Equal(3, carried.Quantity);
        Assert.Equal(5, await HeldAsync(crafter, "ferrite_plate", ItemStyle.Terran));
    }

    [Fact]
    public async Task A_buyer_receives_the_style_the_seller_put_up()
    {
        // One book (ADR-0008): the buy order names no style and fills with whatever is offered.
        Crafter seller = await CrafterAsync(Race.SpaceOrc, "Seller");
        Crafter buyer = await CrafterAsync(Race.Humanoid, "Buyer", seller.StationId);

        await GiveAsync(seller, "ferrite_plate", 4, ItemStyle.Grimhold);
        await GiveAsync(seller, "ferrite_plate", 9, ItemStyle.Terran);

        int plate = await ItemIdAsync("ferrite_plate");

        await using (SpaceMmoDbContext sell = _fixture.CreateContext())
        {
            await new MarketService(sell).PlaceOrderAsync(new PlaceOrderRequest(
                seller.CharacterId, seller.StationId, plate, OrderSide.Sell,
                Credits.FromWholeCredits(5), 4, Style: ItemStyle.Grimhold));
        }

        await using (SpaceMmoDbContext buy = _fixture.CreateContext())
        {
            PlaceOrderResult bought = await new MarketService(buy).PlaceOrderAsync(new PlaceOrderRequest(
                buyer.CharacterId, buyer.StationId, plate, OrderSide.Buy,
                Credits.FromWholeCredits(5), 4));

            Assert.True(bought.IsFullyFilled);
        }

        Assert.Equal(4, await HeldAsync(buyer, "ferrite_plate", ItemStyle.Grimhold));
        Assert.Equal(9, await HeldAsync(seller, "ferrite_plate", ItemStyle.Terran));

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.Equal(ItemStyle.Grimhold, (await verify.Trades.SingleAsync()).Style);
        Assert.Null((await verify.MarketOrders.SingleAsync(o => o.Side == OrderSide.Buy)).Style);
    }

    [Fact]
    public async Task Cancelling_a_sell_order_returns_its_goods_in_their_style()
    {
        Crafter seller = await CrafterAsync(Race.SpaceElf, "Lister");

        await GiveAsync(seller, "crude_thruster", 2, ItemStyle.Verdant);

        int thruster = await ItemIdAsync("crude_thruster");
        long orderId;

        await using (SpaceMmoDbContext sell = _fixture.CreateContext())
        {
            orderId = (await new MarketService(sell).PlaceOrderAsync(new PlaceOrderRequest(
                seller.CharacterId, seller.StationId, thruster, OrderSide.Sell,
                Credits.FromWholeCredits(500), 2))).OrderId;
        }

        Assert.Equal(0, await HeldAsync(seller, "crude_thruster", ItemStyle.Verdant));

        await using (SpaceMmoDbContext cancel = _fixture.CreateContext())
        {
            Assert.True(await new MarketService(cancel).CancelOrderAsync(orderId, seller.CharacterId));
        }

        Assert.Equal(2, await HeldAsync(seller, "crude_thruster", ItemStyle.Verdant));
        Assert.Equal(0, await HeldAsync(seller, "crude_thruster", ItemStyle.Common));
    }

    // ── Helpers ──────────────────────────────────────────────────────────────

    private readonly record struct Crafter(int CharacterId, int StationId, long HangarId);

    /// <summary>
    /// A character docked at a station on their race's homeworld, with every crafting skill high
    /// enough that levels are never the thing under test.
    /// </summary>
    private async Task<Crafter> CrafterAsync(Race race, string name, int? atStationId = null)
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        string homeKey = Races.HomeBodyKeyFor(race);
        Body home = await context.Bodies.SingleAsync(b => b.Key == homeKey);

        int stationId = atStationId
            ?? await context.Stations.Where(s => s.BodyId == home.Id).Select(s => s.Id).FirstAsync();

        var account = new Account
        {
            Email = $"{name.ToLowerInvariant()}@example.com",
            PasswordHash = "not-a-real-hash",
            CreatedAt = DateTimeOffset.UtcNow,
        };
        context.Accounts.Add(account);
        await context.SaveChangesAsync();

        var character = new Character
        {
            AccountId = account.Id,
            Name = name,
            Race = race,
            HomeBodyId = home.Id,
            Balance = Credits.FromWholeCredits(50_000),
            CreatedAt = DateTimeOffset.UtcNow,
            DockedStationId = stationId,
        };
        context.Characters.Add(character);
        await context.SaveChangesAsync();

        foreach (Skill skill in await context.Skills.ToListAsync())
        {
            context.CharacterSkills.Add(new CharacterSkill
            {
                CharacterId = character.Id,
                SkillId = skill.Id,
                Xp = SkillCurve.XpForLevel(50),
            });
        }

        Inventory hangar = await new InventoryService(context)
            .GetOrCreateStationHangarAsync(character.Id, stationId);

        await context.SaveChangesAsync();

        return new Crafter(character.Id, stationId, hangar.Id);
    }

    /// <summary>The starter ore deposited on a race's homeworld, read from the loaded content.</summary>
    private async Task<string> HomeOreAsync(Race race)
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        string homeKey = Races.HomeBodyKeyFor(race);

        return await context.ResourceNodes
            .Where(n => context.Bodies.Any(b => b.Id == n.BodyId && b.Key == homeKey))
            .Join(context.ItemDefs, n => n.ItemDefId, d => d.Id, (n, d) => d)
            .Where(d => d.Family == "starter_ore")
            .Select(d => d.Key)
            .Distinct()
            .SingleAsync();
    }

    /// <summary>The refining recipe whose main input is this ore: one per world (task 185).</summary>
    private string RefineRecipeFor(string ore)
    {
        using SpaceMmoDbContext context = _fixture.CreateContext();

        return context.Recipes
            .Where(r => r.MainInputItemDef!.Key == ore)
            .Select(r => r.Key)
            .Single();
    }

    private async Task GiveAsync(
        Crafter crafter, string itemKey, int quantity, ItemStyle style = ItemStyle.Common)
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        await new InventoryService(context).AddAsync(
            crafter.HangarId, await ItemIdAsync(itemKey), quantity, Credits.Zero, style);

        await context.SaveChangesAsync();
    }

    private async Task CarryAsync(Crafter crafter, string itemKey, int quantity, ItemStyle style)
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();
        var inventories = new InventoryService(context);

        Inventory carried = await inventories.GetOrCreateCarriedAsync(crafter.CharacterId);

        await inventories.AddAsync(carried.Id, await ItemIdAsync(itemKey), quantity, Credits.Zero, style);

        await context.SaveChangesAsync();
    }

    /// <summary>Starts a job, moves its clock past completion, and claims it.</summary>
    private async Task MakeAsync(
        Crafter crafter, string recipeKey, int runs, ItemStyle? mainInputStyle = null)
    {
        await using SpaceMmoDbContext start = _fixture.CreateContext();

        StartJobResult started = await new IndustryService(start).StartJobAsync(
            crafter.CharacterId, await RecipeIdAsync(recipeKey), crafter.StationId, runs, mainInputStyle);

        await using (SpaceMmoDbContext shift = _fixture.CreateContext())
        {
            IndustryJob job = await shift.IndustryJobs.SingleAsync(j => j.Id == started.JobId);
            job.CompletesAt = DateTimeOffset.UtcNow.AddSeconds(-1);
            await shift.SaveChangesAsync();
        }

        await using SpaceMmoDbContext claim = _fixture.CreateContext();

        await new IndustryService(claim).ClaimJobAsync(started.JobId, crafter.CharacterId);
    }

    /// <summary>How many of an item in one style a character's station hangar holds.</summary>
    private async Task<int> HeldAsync(Crafter crafter, string itemKey, ItemStyle style)
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        return await context.InventoryItems
            .Where(i => i.InventoryId == crafter.HangarId
                && i.ItemDef!.Key == itemKey
                && i.Style == style)
            .SumAsync(i => i.Quantity);
    }

    private async Task<int> ItemIdAsync(string key)
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        return await context.ItemDefs.Where(d => d.Key == key).Select(d => d.Id).SingleAsync();
    }

    private async Task<int> RecipeIdAsync(string key)
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        return await context.Recipes.Where(r => r.Key == key).Select(r => r.Id).SingleAsync();
    }

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
