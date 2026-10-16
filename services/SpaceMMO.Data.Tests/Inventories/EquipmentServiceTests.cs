using Microsoft.EntityFrameworkCore;
using SpaceMMO.Data.Entities;
using SpaceMMO.Data.Inventories;
using SpaceMMO.Domain.Characters;
using SpaceMMO.Domain.Economy;
using SpaceMMO.Domain.Items;
using SpaceMMO.Domain.Universe;
using Xunit;

namespace SpaceMMO.Data.Tests.Inventories;

/// <summary>
/// Putting things on and taking them off (task 193).
/// </summary>
/// <remarks>
/// The tests worth having are the ones where something would quietly go missing or end up somewhere it
/// should not: a swap that strands the old item, an unequip with nowhere to go, a transfer that slips an
/// item into a slot without the slot rules, or a backfill that takes off what somebody chose to wear.
/// </remarks>
[Collection(SharedDatabase.Name)]
public sealed class EquipmentServiceTests(DatabaseFixture fixture) : IAsyncLifetime
{
    private readonly DatabaseFixture _fixture = fixture;

    private int _ownerId;
    private int _strangerId;
    private int _hereId;
    private int _elsewhereId;
    private int _oreId;
    private int _laserId;
    private int _rifleId;
    private int _cannonId;
    private int _hullId;

    public async Task InitializeAsync()
    {
        await _fixture.ResetAsync();
        await SeedAsync();
    }

    public Task DisposeAsync() => Task.CompletedTask;

    private static EquipmentService Service(SpaceMmoDbContext context) => new(context);

    // ── Equipping ────────────────────────────────────────────────────────────

    [Fact]
    public async Task A_tool_in_the_pockets_goes_into_its_slot()
    {
        long laser = await GiveAsync(_ownerId, _laserId, InventoryKind.CharacterCarried);

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        EquipResult result = await Service(context).EquipAsync(_ownerId, laser);

        Assert.Equal(EquipmentSlot.MiningTool, result.Slot);
        Assert.Null(result.SwappedOutInstanceId);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        ItemInstance worn = await verify.ItemInstances.Include(i => i.Inventory).SingleAsync(i => i.Id == laser);

        Assert.Equal(InventoryKind.Equipped, worn.Inventory!.Kind);
        Assert.Equal(_ownerId, worn.Inventory.CharacterId);
        Assert.Equal(EquipmentSlot.MiningTool, worn.EquippedSlot);
    }

    [Fact]
    public async Task A_second_weapon_fills_Weapon_2_and_a_third_replaces_Weapon_1()
    {
        long first = await GiveAsync(_ownerId, _rifleId, InventoryKind.CharacterCarried);
        long second = await GiveAsync(_ownerId, _rifleId, InventoryKind.CharacterCarried);
        long third = await GiveAsync(_ownerId, _rifleId, InventoryKind.ShipHold);

        await using (SpaceMmoDbContext context = _fixture.CreateContext())
        {
            Assert.Equal(EquipmentSlot.Weapon1, (await Service(context).EquipAsync(_ownerId, first)).Slot);
        }

        await using (SpaceMmoDbContext context = _fixture.CreateContext())
        {
            Assert.Equal(EquipmentSlot.Weapon2, (await Service(context).EquipAsync(_ownerId, second)).Slot);
        }

        EquipResult swapped;

        await using (SpaceMmoDbContext context = _fixture.CreateContext())
        {
            swapped = await Service(context).EquipAsync(_ownerId, third);
        }

        Assert.Equal(EquipmentSlot.Weapon1, swapped.Slot);
        Assert.Equal(first, swapped.SwappedOutInstanceId);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        // What came out went back where the new one came from, the hold, and wears no slot there.
        ItemInstance outgoing = await verify.ItemInstances.Include(i => i.Inventory).SingleAsync(i => i.Id == first);

        Assert.Equal(InventoryKind.ShipHold, outgoing.Inventory!.Kind);
        Assert.Null(outgoing.EquippedSlot);
        Assert.Equal(EquipmentSlot.Weapon2, (await verify.ItemInstances.SingleAsync(i => i.Id == second)).EquippedSlot);
        Assert.Equal(EquipmentSlot.Weapon1, (await verify.ItemInstances.SingleAsync(i => i.Id == third)).EquippedSlot);
    }

    [Fact]
    public async Task Moving_Weapon_1_into_Weapon_2_swaps_the_two()
    {
        // The case the unique index makes awkward: written in one save, the two rows would each briefly hold
        // the other's slot and Postgres would refuse it.
        long first = await GiveAsync(_ownerId, _rifleId, InventoryKind.CharacterCarried);
        long second = await GiveAsync(_ownerId, _rifleId, InventoryKind.CharacterCarried);

        await EquipAsync(first);
        await EquipAsync(second);

        await using (SpaceMmoDbContext context = _fixture.CreateContext())
        {
            EquipResult result = await Service(context).EquipAsync(_ownerId, first, EquipmentSlot.Weapon2);

            Assert.Equal(second, result.SwappedOutInstanceId);
        }

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.Equal(EquipmentSlot.Weapon2, (await verify.ItemInstances.SingleAsync(i => i.Id == first)).EquippedSlot);
        Assert.Equal(EquipmentSlot.Weapon1, (await verify.ItemInstances.SingleAsync(i => i.Id == second)).EquippedSlot);
        Assert.Equal(2, await CountWornAsync(verify, _ownerId));
    }

    [Fact]
    public async Task A_swap_whose_outgoing_item_will_not_fit_back_is_refused_and_changes_nothing()
    {
        // A slim rifle from full pockets, for a cannon that will not go in them.
        long cannon = await GiveAsync(_ownerId, _cannonId, InventoryKind.CharacterCarried);
        await EquipAsync(cannon);

        long rifle = await GiveAsync(_ownerId, _rifleId, InventoryKind.CharacterCarried);
        await EquipAsync(rifle, EquipmentSlot.Weapon2);

        long spare = await GiveAsync(_ownerId, _rifleId, InventoryKind.CharacterCarried);
        await FillPocketsAsync(_ownerId);

        await using (SpaceMmoDbContext context = _fixture.CreateContext())
        {
            EquipmentException refused = await Assert.ThrowsAsync<EquipmentException>(() =>
                Service(context).EquipAsync(_ownerId, spare, EquipmentSlot.Weapon1));

            Assert.Equal("no_room", refused.Reason);
        }

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.Equal(EquipmentSlot.Weapon1, (await verify.ItemInstances.SingleAsync(i => i.Id == cannon)).EquippedSlot);
        Assert.Null((await verify.ItemInstances.SingleAsync(i => i.Id == spare)).EquippedSlot);
    }

    [Fact]
    public async Task An_item_in_the_wrong_slot_is_refused()
    {
        long laser = await GiveAsync(_ownerId, _laserId, InventoryKind.CharacterCarried);

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        EquipmentException refused = await Assert.ThrowsAsync<EquipmentException>(() =>
            Service(context).EquipAsync(_ownerId, laser, EquipmentSlot.Weapon1));

        Assert.Equal("wrong_slot", refused.Reason);
    }

    [Fact]
    public async Task Something_that_is_not_worn_is_refused()
    {
        long hull = await GiveAsync(_ownerId, _hullId, InventoryKind.CharacterCarried);

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        EquipmentException refused = await Assert.ThrowsAsync<EquipmentException>(() =>
            Service(context).EquipAsync(_ownerId, hull));

        Assert.Equal("not_equippable", refused.Reason);
    }

    [Fact]
    public async Task Somebody_elses_item_is_refused()
    {
        long theirs = await GiveAsync(_strangerId, _laserId, InventoryKind.CharacterCarried);

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        EquipmentException refused = await Assert.ThrowsAsync<EquipmentException>(() =>
            Service(context).EquipAsync(_ownerId, theirs));

        Assert.Equal("not_yours", refused.Reason);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.Equal(0, await CountWornAsync(verify, _ownerId));
    }

    [Fact]
    public async Task A_hangar_reaches_only_while_docked_there()
    {
        long laser = await GiveAsync(_ownerId, _laserId, InventoryKind.StationHangar, _elsewhereId);

        await DockAsync(_hereId);

        await using (SpaceMmoDbContext context = _fixture.CreateContext())
        {
            EquipmentException refused = await Assert.ThrowsAsync<EquipmentException>(() =>
                Service(context).EquipAsync(_ownerId, laser));

            Assert.Equal("not_here", refused.Reason);
        }

        await DockAsync(_elsewhereId);

        await using (SpaceMmoDbContext context = _fixture.CreateContext())
        {
            Assert.Equal(EquipmentSlot.MiningTool, (await Service(context).EquipAsync(_ownerId, laser)).Slot);
        }
    }

    // ── Taking off ───────────────────────────────────────────────────────────

    [Fact]
    public async Task Taking_off_puts_it_in_the_pockets()
    {
        long laser = await GiveAsync(_ownerId, _laserId, InventoryKind.CharacterCarried);
        await EquipAsync(laser);

        await using (SpaceMmoDbContext context = _fixture.CreateContext())
        {
            await Service(context).UnequipAsync(_ownerId, EquipmentSlot.MiningTool);
        }

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        ItemInstance off = await verify.ItemInstances.Include(i => i.Inventory).SingleAsync(i => i.Id == laser);

        Assert.Equal(InventoryKind.CharacterCarried, off.Inventory!.Kind);
        Assert.Null(off.EquippedSlot);
    }

    [Fact]
    public async Task With_full_pockets_it_goes_to_the_docked_hangar()
    {
        long laser = await GiveAsync(_ownerId, _laserId, InventoryKind.CharacterCarried);
        await EquipAsync(laser);
        await FillPocketsAsync(_ownerId);
        await DockAsync(_hereId);

        await using (SpaceMmoDbContext context = _fixture.CreateContext())
        {
            await Service(context).UnequipAsync(_ownerId, EquipmentSlot.MiningTool);
        }

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        ItemInstance off = await verify.ItemInstances.Include(i => i.Inventory).SingleAsync(i => i.Id == laser);

        Assert.Equal(InventoryKind.StationHangar, off.Inventory!.Kind);
        Assert.Equal(_hereId, off.Inventory.StationId);
    }

    [Fact]
    public async Task With_full_pockets_and_no_hangar_it_stays_on()
    {
        long laser = await GiveAsync(_ownerId, _laserId, InventoryKind.CharacterCarried);
        await EquipAsync(laser);
        await FillPocketsAsync(_ownerId);

        await using (SpaceMmoDbContext context = _fixture.CreateContext())
        {
            EquipmentException refused = await Assert.ThrowsAsync<EquipmentException>(() =>
                Service(context).UnequipAsync(_ownerId, EquipmentSlot.MiningTool));

            Assert.Equal("no_room", refused.Reason);
        }

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.Equal(EquipmentSlot.MiningTool, (await verify.ItemInstances.SingleAsync(i => i.Id == laser)).EquippedSlot);
    }

    [Fact]
    public async Task Dragged_to_the_hold_it_lands_in_the_hold()
    {
        long laser = await GiveAsync(_ownerId, _laserId, InventoryKind.CharacterCarried);
        await EquipAsync(laser);

        long hold;

        await using (SpaceMmoDbContext context = _fixture.CreateContext())
        {
            hold = (await HoldAsync(context, _ownerId)).Id;

            Assert.Equal(hold, await Service(context).UnequipAsync(_ownerId, EquipmentSlot.MiningTool, hold));
        }

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        ItemInstance off = await verify.ItemInstances.SingleAsync(i => i.Id == laser);

        Assert.Equal(hold, off.InventoryId);
        Assert.Null(off.EquippedSlot);
    }

    [Fact]
    public async Task Dragged_to_a_hangar_somewhere_else_or_to_somebody_elses_container_it_stays_on()
    {
        // The same reach equipping has: dropping a laser into a hangar four worlds away would be hauling by
        // menu, and into a stranger's hold, a trade with no market.
        long laser = await GiveAsync(_ownerId, _laserId, InventoryKind.CharacterCarried);
        await EquipAsync(laser);

        long farHangar;
        long theirs;

        await using (SpaceMmoDbContext context = _fixture.CreateContext())
        {
            var inventories = new InventoryService(context);

            farHangar = (await inventories.GetOrCreateStationHangarAsync(_ownerId, _elsewhereId)).Id;
            theirs = (await inventories.GetOrCreateCarriedAsync(_strangerId)).Id;
        }

        await DockAsync(_hereId);

        await using (SpaceMmoDbContext context = _fixture.CreateContext())
        {
            EquipmentException far = await Assert.ThrowsAsync<EquipmentException>(() =>
                Service(context).UnequipAsync(_ownerId, EquipmentSlot.MiningTool, farHangar));

            Assert.Equal("not_here", far.Reason);
        }

        await using (SpaceMmoDbContext context = _fixture.CreateContext())
        {
            EquipmentException stranger = await Assert.ThrowsAsync<EquipmentException>(() =>
                Service(context).UnequipAsync(_ownerId, EquipmentSlot.MiningTool, theirs));

            Assert.Equal("not_yours", stranger.Reason);
        }

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.Equal(EquipmentSlot.MiningTool, (await verify.ItemInstances.SingleAsync(i => i.Id == laser)).EquippedSlot);
    }

    [Fact]
    public async Task Dragged_to_full_pockets_it_stays_on_rather_than_going_elsewhere()
    {
        // Named means there: a drop onto the pockets that quietly landed in a hangar would be the drag lying.
        long laser = await GiveAsync(_ownerId, _laserId, InventoryKind.CharacterCarried);
        await EquipAsync(laser);
        await FillPocketsAsync(_ownerId);
        await DockAsync(_hereId);

        await using (SpaceMmoDbContext context = _fixture.CreateContext())
        {
            long pockets = (await new InventoryService(context).GetOrCreateCarriedAsync(_ownerId)).Id;

            EquipmentException refused = await Assert.ThrowsAsync<EquipmentException>(() =>
                Service(context).UnequipAsync(_ownerId, EquipmentSlot.MiningTool, pockets));

            Assert.Equal("no_room", refused.Reason);
        }

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.Equal(EquipmentSlot.MiningTool, (await verify.ItemInstances.SingleAsync(i => i.Id == laser)).EquippedSlot);
    }

    [Fact]
    public async Task Taking_off_an_empty_slot_is_refused()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        EquipmentException refused = await Assert.ThrowsAsync<EquipmentException>(() =>
            Service(context).UnequipAsync(_ownerId, EquipmentSlot.Head));

        Assert.Equal("empty_slot", refused.Reason);
    }

    // ── The other routes ─────────────────────────────────────────────────────

    [Fact]
    public async Task A_transfer_can_neither_put_an_item_on_nor_take_one_off()
    {
        // Otherwise a drag would leave a laser in the Equipped inventory wearing no slot, or a slot set on an
        // item in somebody's pockets. Both routes, stacks and instances, because either would do it.
        long worn = await GiveAsync(_ownerId, _laserId, InventoryKind.CharacterCarried);
        await EquipAsync(worn);

        long loose = await GiveAsync(_ownerId, _laserId, InventoryKind.CharacterCarried);

        await using SpaceMmoDbContext context = _fixture.CreateContext();
        var inventories = new InventoryService(context);

        Inventory pockets = await inventories.GetOrCreateCarriedAsync(_ownerId);
        Inventory equipped = await inventories.GetOrCreateEquippedAsync(_ownerId);

        await Assert.ThrowsAsync<InventoryTransferException>(() =>
            inventories.TransferInstanceAsync(worn, pockets.Id));
        await Assert.ThrowsAsync<InventoryTransferException>(() =>
            inventories.TransferInstanceAsync(loose, equipped.Id));

        await inventories.AddAsync(pockets.Id, _oreId, 1, Credits.Zero);
        await context.SaveChangesAsync();

        await Assert.ThrowsAsync<InventoryTransferException>(() =>
            inventories.TransferAsync(pockets.Id, equipped.Id, _oreId, 1));
    }

    [Fact]
    public async Task Two_items_cannot_share_a_slot_even_written_directly()
    {
        // The index is the last word: whatever route a future change takes, the database refuses it.
        long first = await GiveAsync(_ownerId, _laserId, InventoryKind.CharacterCarried);
        long second = await GiveAsync(_ownerId, _laserId, InventoryKind.CharacterCarried);
        await EquipAsync(first);

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        Inventory equipped = await new InventoryService(context).GetOrCreateEquippedAsync(_ownerId);
        ItemInstance intruder = await context.ItemInstances.SingleAsync(i => i.Id == second);

        intruder.InventoryId = equipped.Id;
        intruder.EquippedSlot = EquipmentSlot.MiningTool;

        await Assert.ThrowsAsync<DbUpdateException>(() => context.SaveChangesAsync());
    }

    // ── The backfill ─────────────────────────────────────────────────────────

    [Fact]
    public async Task The_backfill_equips_the_best_tool_each_character_owns_and_runs_once()
    {
        long broken = await GiveAsync(_ownerId, _laserId, InventoryKind.CharacterCarried, condition: 12);
        long good = await GiveAsync(_ownerId, _laserId, InventoryKind.StationHangar, _elsewhereId, condition: 90);
        long theirs = await GiveAsync(_strangerId, _laserId, InventoryKind.ShipHold);

        // A weapon is not a tool: the backfill exists because tools started being required, and nobody's
        // rifle should be put on for them.
        long rifle = await GiveAsync(_ownerId, _rifleId, InventoryKind.CharacterCarried);

        await using (SpaceMmoDbContext context = _fixture.CreateContext())
        {
            IReadOnlyList<string> given = await Service(context).EquipOwnedToolsAsync();

            Assert.Equal(2, given.Count);
        }

        await using (SpaceMmoDbContext verify = _fixture.CreateContext())
        {
            Assert.Equal(EquipmentSlot.MiningTool, (await verify.ItemInstances.SingleAsync(i => i.Id == good)).EquippedSlot);
            Assert.Null((await verify.ItemInstances.SingleAsync(i => i.Id == broken)).EquippedSlot);
            Assert.Equal(EquipmentSlot.MiningTool, (await verify.ItemInstances.SingleAsync(i => i.Id == theirs)).EquippedSlot);
            Assert.Null((await verify.ItemInstances.SingleAsync(i => i.Id == rifle)).EquippedSlot);
        }

        await using (SpaceMmoDbContext again = _fixture.CreateContext())
        {
            Assert.Empty(await Service(again).EquipOwnedToolsAsync());
        }
    }

    [Fact]
    public async Task The_backfill_leaves_a_chosen_tool_alone()
    {
        long chosen = await GiveAsync(_ownerId, _laserId, InventoryKind.CharacterCarried, condition: 40);
        await EquipAsync(chosen);

        long better = await GiveAsync(_ownerId, _laserId, InventoryKind.CharacterCarried, condition: 100);

        await using (SpaceMmoDbContext context = _fixture.CreateContext())
        {
            Assert.Empty(await Service(context).EquipOwnedToolsAsync());
        }

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.Equal(EquipmentSlot.MiningTool, (await verify.ItemInstances.SingleAsync(i => i.Id == chosen)).EquippedSlot);
        Assert.Null((await verify.ItemInstances.SingleAsync(i => i.Id == better)).EquippedSlot);
    }

    // ── Helpers ──────────────────────────────────────────────────────────────

    private async Task EquipAsync(long instanceId, EquipmentSlot? slot = null)
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        await Service(context).EquipAsync(_ownerId, instanceId, slot);
    }

    private async Task<long> GiveAsync(
        int characterId, int itemDefId, InventoryKind kind, int? stationId = null, int condition = 100)
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();
        var inventories = new InventoryService(context);

        Inventory into = kind switch
        {
            InventoryKind.CharacterCarried => await inventories.GetOrCreateCarriedAsync(characterId),
            InventoryKind.StationHangar => await inventories.GetOrCreateStationHangarAsync(
                characterId, stationId ?? _hereId),
            InventoryKind.ShipHold => await HoldAsync(context, characterId),
            _ => throw new ArgumentOutOfRangeException(nameof(kind)),
        };

        var instance = new ItemInstance
        {
            ItemDefId = itemDefId,
            InventoryId = into.Id,
            Condition = condition,
            AcquisitionValue = Credits.FromWholeCredits(100),
            CreatedAt = DateTimeOffset.UtcNow,
        };

        context.ItemInstances.Add(instance);
        await context.SaveChangesAsync();

        return instance.Id;
    }

    private static async Task<Inventory> HoldAsync(SpaceMmoDbContext context, int characterId)
    {
        Inventory? hold = await context.Inventories.FirstOrDefaultAsync(
            i => i.CharacterId == characterId && i.Kind == InventoryKind.ShipHold);

        if (hold is null)
        {
            hold = new Inventory { CharacterId = characterId, Kind = InventoryKind.ShipHold, CapacityM3 = 0 };
            context.Inventories.Add(hold);
            await context.SaveChangesAsync();
        }

        return hold;
    }

    /// <summary>Fills the pockets with ore to the brim, so nothing more goes in.</summary>
    private async Task FillPocketsAsync(int characterId)
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();
        var inventories = new InventoryService(context);

        Inventory pockets = await inventories.GetOrCreateCarriedAsync(characterId);
        double room = pockets.CapacityM3 - await inventories.UsedVolumeAsync(pockets.Id);

        // The ore is 0.5 m³, so this leaves less than one unit's worth: no tool or weapon fits.
        int units = (int)Math.Floor(room / 0.5);

        if (units > 0)
        {
            await inventories.AddAsync(pockets.Id, _oreId, units, Credits.Zero);
            await context.SaveChangesAsync();
        }
    }

    private async Task DockAsync(int? stationId)
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        Character character = await context.Characters.SingleAsync(c => c.Id == _ownerId);
        character.DockedStationId = stationId;

        await context.SaveChangesAsync();
    }

    private static Task<int> CountWornAsync(SpaceMmoDbContext context, int characterId) =>
        context.ItemInstances.CountAsync(i => i.Inventory!.CharacterId == characterId
            && i.Inventory.Kind == InventoryKind.Equipped
            && i.EquippedSlot != null);

    private async Task SeedAsync()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        var system = new StarSystem
        {
            Key = "system_origin",
            Name = "Origin",
            Seed = 42,
            GeneratorVersion = 1,
            SecurityLevel = SecurityLevel.Secure,
        };

        context.StarSystems.Add(system);
        await context.SaveChangesAsync();

        var body = new Body
        {
            Key = "body_terra",
            Name = "Terra",
            StarSystemId = system.Id,
            Kind = BodyKind.Planet,
            SecurityLevel = SecurityLevel.Secure,
            RadiusKm = 637.1,
        };

        context.Bodies.Add(body);
        await context.SaveChangesAsync();

        var here = new Station
        {
            Key = "station_here",
            Name = "Here",
            StarSystemId = system.Id,
            BodyId = body.Id,
            Kind = StationKind.TradingHub,
            DirectionX = -1.0,
            DockingRangeKilometres = 5.0,
        };

        var elsewhere = new Station
        {
            Key = "station_elsewhere",
            Name = "Elsewhere",
            StarSystemId = system.Id,
            BodyId = body.Id,
            Kind = StationKind.TradingHub,
            DirectionX = 1.0,
            DockingRangeKilometres = 5.0,
        };

        context.Stations.AddRange(here, elsewhere);

        var ore = new ItemDef
        {
            Key = "ferrite_ore", Name = "Ferrite Ore", Category = ItemCategory.Raw, VolumeM3 = 0.5,
        };

        var laser = new ItemDef
        {
            Key = "crude_mining_laser", Name = "Crude Mining Laser",
            Category = ItemCategory.Tool, VolumeM3 = 2, EquipsAs = EquipKind.MiningTool,
        };

        var rifle = new ItemDef
        {
            Key = "laser_rifle", Name = "Laser Rifle",
            Category = ItemCategory.Weapon, VolumeM3 = 1, EquipsAs = EquipKind.Weapon,
        };

        // Bigger than the pockets hold, so a swap that would send it back to them has nowhere to put it.
        var cannon = new ItemDef
        {
            Key = "hand_cannon", Name = "Hand Cannon",
            Category = ItemCategory.Weapon, VolumeM3 = InventoryService.CarriedCapacityM3 + 1,
            EquipsAs = EquipKind.Weapon,
        };

        var hull = new ItemDef
        {
            Key = "shuttle_hull", Name = "Shuttle", Category = ItemCategory.Hull, VolumeM3 = 0.1,
        };

        context.ItemDefs.AddRange(ore, laser, rifle, cannon, hull);
        await context.SaveChangesAsync();

        var account = new Account
        {
            Email = "wearer@local.test",
            PasswordHash = "x",
            CreatedAt = DateTimeOffset.UtcNow,
        };

        context.Accounts.Add(account);
        await context.SaveChangesAsync();

        var owner = new Character
        {
            AccountId = account.Id,
            Name = "Wearer",
            Race = Race.Humanoid,
            HomeBodyId = body.Id,
            Balance = Credits.FromWholeCredits(1_000),
            CreatedAt = DateTimeOffset.UtcNow,
        };

        var stranger = new Character
        {
            AccountId = account.Id,
            Name = "Stranger",
            Race = Race.Humanoid,
            HomeBodyId = body.Id,
            Balance = Credits.FromWholeCredits(1_000),
            CreatedAt = DateTimeOffset.UtcNow,
        };

        context.Characters.AddRange(owner, stranger);
        await context.SaveChangesAsync();

        _ownerId = owner.Id;
        _strangerId = stranger.Id;
        _hereId = here.Id;
        _elsewhereId = elsewhere.Id;
        _oreId = ore.Id;
        _laserId = laser.Id;
        _rifleId = rifle.Id;
        _cannonId = cannon.Id;
        _hullId = hull.Id;
    }
}
