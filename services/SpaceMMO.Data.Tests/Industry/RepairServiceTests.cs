using Microsoft.EntityFrameworkCore;
using SpaceMMO.Data.Entities;
using SpaceMMO.Data.Industry;
using SpaceMMO.Data.Inventories;
using SpaceMMO.Data.Market;
using SpaceMMO.Domain.Characters;
using SpaceMMO.Domain.Economy;
using SpaceMMO.Domain.Industry;
using SpaceMMO.Domain.Items;
using SpaceMMO.Domain.Progression;
using SpaceMMO.Domain.Universe;
using Xunit;

namespace SpaceMMO.Data.Tests.Industry;

/// <summary>Repair at a station's industry bay (ADR-0016 as amended, task 196).</summary>
[Collection(SharedDatabase.Name)]
public sealed class RepairServiceTests(DatabaseFixture fixture) : IAsyncLifetime
{
    private readonly DatabaseFixture _fixture = fixture;

    private const long StartingCredits = 1_000;

    private int _characterId;
    private int _hereId;
    private int _elsewhereId;

    private int _plateId;
    private int _scrapId;
    private int _rifleId;
    private int _laserId;
    private int _hullId;
    private int _relicId;

    private long _equippedId;
    private long _carriedId;
    private long _hangarId;
    private long _elsewhereHangarId;

    public async Task InitializeAsync()
    {
        await _fixture.ResetAsync();
        await SeedAsync();
    }

    public Task DisposeAsync() => Task.CompletedTask;

    // ── The list ─────────────────────────────────────────────────────────────

    [Fact]
    public async Task The_list_is_what_is_worn_carried_and_put_away_here_and_damaged()
    {
        long worn = await ItemAsync(_rifleId, _equippedId, 40, slot: EquipmentSlot.Weapon1);
        long carried = await ItemAsync(_laserId, _carriedId, 0);
        long inHangar = await ItemAsync(_laserId, _hangarId, 90);

        await ItemAsync(_laserId, _elsewhereHangarId, 10);
        await ItemAsync(_hullId, _hangarId, 50, deployed: true);
        await ItemAsync(_laserId, _carriedId, 100);
        await ItemAsync(_laserId, _carriedId, 20, destroyed: true);

        IReadOnlyList<RepairQuote> quotes = await QuoteAsync();

        // Worn, then pockets, then the hangar: the order the REPAIRS section reads.
        Assert.Equal(new[] { worn, carried, inHangar }, quotes.Select(q => q.ItemInstanceId));
        Assert.Equal(
            new[] { InventoryKind.Equipped, InventoryKind.CharacterCarried, InventoryKind.StationHangar },
            quotes.Select(q => q.Kind));
    }

    [Fact]
    public async Task A_quote_says_what_the_repair_will_do_and_take()
    {
        await ItemAsync(_rifleId, _equippedId, 40, slot: EquipmentSlot.Weapon1);
        await StackAsync(_hangarId, _scrapId, 3);
        await StackAsync(_carriedId, _scrapId, 2);

        RepairQuote quote = Assert.Single(await QuoteAsync());

        // 60 restored takes 10.80 off the maximum, and condition comes back to what is left of it.
        Assert.Equal((60, 89, 8920, false, true), (quote.Restored, quote.ConditionAfter, quote.MaxAfterHundredths, quote.WornOut, quote.Priced));

        // A quarter of 6 plate and 10 scrap per 100 points: 0.9 plate, all on the tab, and 1.5 scrap, one now.
        Assert.Equal(
            new[] { ("ferrite_plate", 0, 0, 9000), ("scrap_alloy", 1, 5, 5000) },
            quote.Materials.Select(m => (m.ItemKey, m.Units, m.Held, m.OwedAfterTenThousandths)));

        // 60% of one run's crafting fee, 15 cr.
        Assert.Equal(Credits.FromWholeCredits(9), quote.Credits);
    }

    [Fact]
    public async Task Not_docked_here_is_no_list()
    {
        await DockAsync(_elsewhereId);

        RepairRefusedException refused = await Assert.ThrowsAsync<RepairRefusedException>(QuoteAsync);
        Assert.Equal("not_docked", refused.Reason);
    }

    // ── A repair ─────────────────────────────────────────────────────────────

    [Fact]
    public async Task A_repair_restores_lowers_the_maximum_and_charges()
    {
        long rifle = await ItemAsync(_rifleId, _equippedId, 0, slot: EquipmentSlot.Weapon1);
        await StackAsync(_hangarId, _plateId, 5);
        await StackAsync(_hangarId, _scrapId, 5);

        RepairResult result = await RepairAsync(rifle);

        Assert.False(result.Destroyed);
        Assert.Equal((82, 8200), (result.Condition, result.MaxConditionHundredths));
        Assert.Equal(Credits.FromWholeCredits(15), result.Credits);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        ItemInstance after = await verify.ItemInstances.SingleAsync(i => i.Id == rifle);
        Assert.Equal((82, 8200), (after.Condition, after.MaxConditionHundredths));

        // A quarter of the recipe: 1.5 plate and 2.5 scrap, the halves on the tab.
        Assert.Equal(4, await HeldAsync(verify, _hangarId, _plateId));
        Assert.Equal(3, await HeldAsync(verify, _hangarId, _scrapId));
        Dictionary<int, int> tabs = await verify.RepairTabs.Where(t => t.ItemInstanceId == rifle)
            .ToDictionaryAsync(t => t.ItemDefId, t => t.OwedTenThousandths);
        Assert.Equal(new Dictionary<int, int> { [_plateId] = 5000, [_scrapId] = 5000 }, tabs);

        Assert.Equal(Credits.FromWholeCredits(StartingCredits - 15), await BalanceAsync(verify));

        LedgerEntry ledger = await verify.LedgerEntries.SingleAsync();
        Assert.Equal((LedgerReason.RepairCost, rifle, Credits.FromWholeCredits(-15)), (ledger.Reason, ledger.ReferenceId, ledger.DeltaCredits));
    }

    [Fact]
    public async Task Materials_come_from_the_hangar_first_then_the_pockets()
    {
        long laser = await ItemAsync(_laserId, _carriedId, 0);
        await StackAsync(_hangarId, _scrapId, 1);
        await StackAsync(_carriedId, _scrapId, 5);

        await RepairAsync(laser);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.Equal(0, await HeldAsync(verify, _hangarId, _scrapId));
        Assert.Equal(4, await HeldAsync(verify, _carriedId, _scrapId));
    }

    [Fact]
    public async Task The_tab_carries_from_one_repair_to_the_next()
    {
        // The mining laser owes 0.02 scrap a point: five top-ups of ten are one scrap, charged on the fifth.
        long laser = await ItemAsync(_laserId, _carriedId, 90);
        await StackAsync(_hangarId, _scrapId, 3);

        for (int repair = 1; repair <= 5; repair++)
        {
            RepairResult result = await RepairAsync(laser);

            Assert.Equal(repair == 5 ? new[] { (_scrapId, 1) } : [], result.Charged);
            Assert.Equal(Credits.FromWholeCredits(2), result.Credits);

            await DieAsync(laser);
        }

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.Equal(2, await HeldAsync(verify, _hangarId, _scrapId));
        Assert.Equal(0, (await verify.RepairTabs.SingleAsync()).OwedTenThousandths);
        Assert.Equal(10_000 - (5 * 180), (await verify.ItemInstances.SingleAsync(i => i.Id == laser)).MaxConditionHundredths);
    }

    [Fact]
    public async Task Short_of_materials_changes_nothing()
    {
        long rifle = await ItemAsync(_rifleId, _equippedId, 0, slot: EquipmentSlot.Weapon1);
        await StackAsync(_hangarId, _plateId, 5);
        await StackAsync(_carriedId, _scrapId, 1);

        InsufficientItemsException shortfall = await Assert.ThrowsAsync<InsufficientItemsException>(() => RepairAsync(rifle));

        // The hangar and the pockets together, not one of them.
        Assert.Equal((_scrapId, 2, 1), (shortfall.ItemDefId, shortfall.Requested, shortfall.Available));
        await AssertUntouchedAsync(rifle, 0, plates: 5);
    }

    [Fact]
    public async Task Short_of_credits_changes_nothing()
    {
        long rifle = await ItemAsync(_rifleId, _equippedId, 0, slot: EquipmentSlot.Weapon1);
        await StackAsync(_hangarId, _plateId, 5);
        await StackAsync(_hangarId, _scrapId, 5);
        await SetBalanceAsync(Credits.FromWholeCredits(14));

        await Assert.ThrowsAsync<InsufficientFundsException>(() => RepairAsync(rifle));

        await AssertUntouchedAsync(rifle, 0, plates: 5);
    }

    [Fact]
    public async Task Worn_out_is_destroyed_for_nothing_and_its_slot_freed()
    {
        long rifle = await ItemAsync(_rifleId, _equippedId, 0, max: 1100, slot: EquipmentSlot.Weapon1);
        await StackAsync(_hangarId, _plateId, 5);

        await using (SpaceMmoDbContext context = _fixture.CreateContext())
        {
            context.RepairTabs.Add(new RepairTab { ItemInstanceId = rifle, ItemDefId = _plateId, OwedTenThousandths = 4000 });
            await context.SaveChangesAsync();
        }

        // The quote warns first.
        RepairQuote quote = Assert.Single(await QuoteAsync());
        Assert.True(quote.WornOut);
        Assert.Equal((0, Credits.Zero), (quote.Materials.Count, quote.Credits));

        RepairResult result = await RepairAsync(rifle);
        Assert.True(result.Destroyed);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        ItemInstance gone = await verify.ItemInstances.SingleAsync(i => i.Id == rifle);
        Assert.NotNull(gone.DestroyedAt);
        Assert.Null(gone.InventoryId);
        Assert.Null(gone.EquippedSlot);
        Assert.False(await verify.RepairTabs.AnyAsync());
        Assert.Equal(5, await HeldAsync(verify, _hangarId, _plateId));
        Assert.Equal(Credits.FromWholeCredits(StartingCredits), await BalanceAsync(verify));

        // A new rifle goes into the slot the old one held: the unique index would refuse it otherwise.
        await ItemAsync(_rifleId, _equippedId, 100, slot: EquipmentSlot.Weapon1);
    }

    [Fact]
    public async Task Two_repairs_of_one_item_at_once_charge_once()
    {
        long laser = await ItemAsync(_laserId, _carriedId, 0);
        await StackAsync(_hangarId, _scrapId, 10);

        Task<RepairResult>[] both = [RepairAsync(laser), RepairAsync(laser)];

        try
        {
            await Task.WhenAll(both);
        }
        catch (RepairRefusedException)
        {
        }

        // One repaired it; the other, waiting on its lock, found nothing left to repair.
        Assert.Equal(1, both.Count(t => t.IsCompletedSuccessfully));

        Task<RepairResult> refused = Assert.Single(both, t => t.IsFaulted);
        Assert.Equal("not_damaged", Assert.IsType<RepairRefusedException>(refused.Exception!.InnerException).Reason);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.Equal(8, await HeldAsync(verify, _hangarId, _scrapId));
        Assert.Equal(Credits.FromWholeCredits(StartingCredits - 15), await BalanceAsync(verify));
    }

    // ── Refusals ─────────────────────────────────────────────────────────────

    [Fact]
    public async Task Not_docked_here_is_refused()
    {
        long laser = await ItemAsync(_laserId, _carriedId, 0);
        await DockAsync(null);

        Assert.Equal("not_docked", (await Assert.ThrowsAsync<RepairRefusedException>(() => RepairAsync(laser))).Reason);
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public async Task Elsewhere_or_standing_outside_is_refused(bool deployedHere)
    {
        long item = deployedHere
            ? await ItemAsync(_hullId, _hangarId, 50, deployed: true)
            : await ItemAsync(_laserId, _elsewhereHangarId, 0);

        Assert.Equal("not_here", (await Assert.ThrowsAsync<RepairRefusedException>(() => RepairAsync(item))).Reason);
    }

    [Fact]
    public async Task Nothing_to_repair_is_refused()
    {
        long laser = await ItemAsync(_laserId, _carriedId, 98, max: 9820);

        Assert.Equal("not_damaged", (await Assert.ThrowsAsync<RepairRefusedException>(() => RepairAsync(laser))).Reason);
    }

    [Fact]
    public async Task An_item_nothing_makes_is_listed_unpriced_and_refused()
    {
        long relic = await ItemAsync(_relicId, _carriedId, 50);

        RepairQuote quote = Assert.Single(await QuoteAsync());
        Assert.False(quote.Priced);
        Assert.Equal(Credits.Zero, quote.Credits);

        Assert.Equal("no_recipe", (await Assert.ThrowsAsync<RepairRefusedException>(() => RepairAsync(relic))).Reason);
    }

    // ── Helpers ──────────────────────────────────────────────────────────────

    private async Task<IReadOnlyList<RepairQuote>> QuoteAsync()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();
        return await new RepairService(context).QuoteAsync(_characterId, _hereId);
    }

    private async Task<RepairResult> RepairAsync(long itemInstanceId)
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();
        return await new RepairService(context).RepairAsync(_characterId, _hereId, itemInstanceId);
    }

    private async Task<long> ItemAsync(
        int itemDefId,
        long inventoryId,
        int condition,
        int max = Repairs.NewMaxHundredths,
        EquipmentSlot? slot = null,
        bool deployed = false,
        bool destroyed = false)
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        var item = new ItemInstance
        {
            ItemDefId = itemDefId,
            InventoryId = inventoryId,
            Condition = condition,
            MaxConditionHundredths = max,
            EquippedSlot = slot,
            AcquisitionValue = Credits.FromWholeCredits(100),
            CreatedAt = DateTimeOffset.UtcNow,
            DeployedSystemX = deployed ? 1.0 : null,
            DeployedSystemY = deployed ? 2.0 : null,
            DeployedSystemZ = deployed ? 3.0 : null,
            DestroyedAt = destroyed ? DateTimeOffset.UtcNow : null,
        };

        context.ItemInstances.Add(item);
        await context.SaveChangesAsync();

        return item.Id;
    }

    private async Task StackAsync(long inventoryId, int itemDefId, int quantity)
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();
        await new InventoryService(context).AddAsync(inventoryId, itemDefId, quantity, Credits.Zero);
        await context.SaveChangesAsync();
    }

    /// <summary>A death's wear, as <c>DeathCostService</c> takes it.</summary>
    private async Task DieAsync(long itemInstanceId)
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();
        ItemInstance item = await context.ItemInstances.SingleAsync(i => i.Id == itemInstanceId);
        item.Condition = Domain.Loss.DeathCosts.Worn(item.Condition);
        await context.SaveChangesAsync();
    }

    private async Task DockAsync(int? stationId)
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();
        Character character = await context.Characters.SingleAsync(c => c.Id == _characterId);
        character.DockedStationId = stationId;
        await context.SaveChangesAsync();
    }

    private async Task SetBalanceAsync(Credits balance)
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();
        Character character = await context.Characters.SingleAsync(c => c.Id == _characterId);
        character.Balance = balance;
        await context.SaveChangesAsync();
    }

    private async Task AssertUntouchedAsync(long itemInstanceId, int condition, int plates)
    {
        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        ItemInstance item = await verify.ItemInstances.SingleAsync(i => i.Id == itemInstanceId);
        Assert.Equal((condition, Repairs.NewMaxHundredths), (item.Condition, item.MaxConditionHundredths));
        Assert.Equal(plates, await HeldAsync(verify, _hangarId, _plateId));
        Assert.False(await verify.RepairTabs.AnyAsync());
        Assert.False(await verify.LedgerEntries.AnyAsync());
    }

    private static async Task<int> HeldAsync(SpaceMmoDbContext context, long inventoryId, int itemDefId) =>
        await new InventoryService(context).QuantityOfAsync(inventoryId, itemDefId);

    private async Task<Credits> BalanceAsync(SpaceMmoDbContext context) =>
        (await context.Characters.SingleAsync(c => c.Id == _characterId)).Balance;

    private async Task SeedAsync()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        var system = new StarSystem
        {
            Key = "system_origin", Name = "Origin", Seed = 42, GeneratorVersion = 1,
            SecurityLevel = SecurityLevel.Secure,
        };
        context.StarSystems.Add(system);
        await context.SaveChangesAsync();

        var body = new Body
        {
            Key = "body_terra", Name = "Terra", StarSystemId = system.Id, Kind = BodyKind.Planet,
            SecurityLevel = SecurityLevel.Secure, RadiusKm = 637.1,
        };
        context.Bodies.Add(body);

        var crafting = new Skill { Key = "toolcrafting", Name = "Toolcrafting", Category = SkillCategory.Life };
        context.Skills.Add(crafting);

        var plate = new ItemDef { Key = "ferrite_plate", Name = "Ferrite Plate", Category = ItemCategory.Refined, VolumeM3 = 0.2 };
        var scrap = new ItemDef { Key = "scrap_alloy", Name = "Scrap Alloy", Category = ItemCategory.Raw, VolumeM3 = 0.1 };
        var rifle = new ItemDef
        {
            Key = "laser_rifle", Name = "Laser Rifle", Category = ItemCategory.Weapon, VolumeM3 = 2,
            EquipsAs = EquipKind.Weapon,
        };
        var laser = new ItemDef
        {
            Key = "crude_mining_laser", Name = "Crude Mining Laser", Category = ItemCategory.Tool, VolumeM3 = 2,
            EquipsAs = EquipKind.MiningTool,
        };
        var hull = new ItemDef { Key = "hull_shuttle", Name = "Shuttle", Category = ItemCategory.Hull, VolumeM3 = 100 };
        var relic = new ItemDef { Key = "relic_pistol", Name = "Relic Pistol", Category = ItemCategory.Weapon, VolumeM3 = 1 };

        context.ItemDefs.AddRange(plate, scrap, rifle, laser, hull, relic);
        await context.SaveChangesAsync();

        var here = new Station
        {
            Key = "station_here", Name = "Here", StarSystemId = system.Id, BodyId = body.Id, Kind = StationKind.TradingHub,
        };
        var elsewhere = new Station
        {
            Key = "station_elsewhere", Name = "Elsewhere", StarSystemId = system.Id, BodyId = body.Id,
            Kind = StationKind.TradingHub,
        };
        context.Stations.AddRange(here, elsewhere);

        var account = new Account { Email = "repair@example.com", PasswordHash = "not-a-real-hash", CreatedAt = DateTimeOffset.UtcNow };
        context.Accounts.Add(account);
        await context.SaveChangesAsync();

        var character = new Character
        {
            AccountId = account.Id, Name = "Mender", Race = Race.Humanoid, HomeBodyId = body.Id,
            Balance = Credits.FromWholeCredits(StartingCredits), CreatedAt = DateTimeOffset.UtcNow,
            DockedStationId = here.Id,
        };
        context.Characters.Add(character);

        // The shipped recipes' inputs: the rifle 6 plate and 10 scrap, the mining laser 8 scrap. The hull's has
        // something to be made of; the relic has none.
        Recipe Making(ItemDef output) => new()
        {
            Key = "craft_" + output.Key, OutputItemDefId = output.Id, OutputQuantity = 1, SkillId = crafting.Id,
            RequiredLevel = 1, JobSeconds = 30, XpPerRun = 10,
        };

        Recipe rifleRecipe = Making(rifle);
        Recipe laserRecipe = Making(laser);
        Recipe hullRecipe = Making(hull);
        context.Recipes.AddRange(rifleRecipe, laserRecipe, hullRecipe);
        await context.SaveChangesAsync();

        context.RecipeInputs.AddRange(
            new RecipeInput { RecipeId = rifleRecipe.Id, ItemDefId = plate.Id, Quantity = 6 },
            new RecipeInput { RecipeId = rifleRecipe.Id, ItemDefId = scrap.Id, Quantity = 10 },
            new RecipeInput { RecipeId = laserRecipe.Id, ItemDefId = scrap.Id, Quantity = 8 },
            new RecipeInput { RecipeId = hullRecipe.Id, ItemDefId = plate.Id, Quantity = 40 });

        var equipped = new Inventory { CharacterId = character.Id, Kind = InventoryKind.Equipped, CapacityM3 = 0 };
        var carried = new Inventory { CharacterId = character.Id, Kind = InventoryKind.CharacterCarried, CapacityM3 = 0 };
        var hangar = new Inventory { CharacterId = character.Id, Kind = InventoryKind.StationHangar, StationId = here.Id, CapacityM3 = 0 };
        var elsewhereHangar = new Inventory
        {
            CharacterId = character.Id, Kind = InventoryKind.StationHangar, StationId = elsewhere.Id, CapacityM3 = 0,
        };
        context.Inventories.AddRange(equipped, carried, hangar, elsewhereHangar);
        await context.SaveChangesAsync();

        (_characterId, _hereId, _elsewhereId) = (character.Id, here.Id, elsewhere.Id);
        (_plateId, _scrapId, _rifleId, _laserId, _hullId, _relicId) = (plate.Id, scrap.Id, rifle.Id, laser.Id, hull.Id, relic.Id);
        (_equippedId, _carriedId, _hangarId, _elsewhereHangarId) = (equipped.Id, carried.Id, hangar.Id, elsewhereHangar.Id);
    }
}
