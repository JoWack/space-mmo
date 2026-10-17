using Microsoft.EntityFrameworkCore;
using SpaceMMO.Data.Content;
using SpaceMMO.Data.Entities;
using SpaceMMO.Domain.Characters;
using SpaceMMO.Domain.Content;
using SpaceMMO.Domain.Economy;
using SpaceMMO.Domain.Items;
using SpaceMMO.Domain.Quests;
using SpaceMMO.Domain.Universe;
using Xunit;

namespace SpaceMMO.Data.Tests.Content;

/// <summary>
/// Integration tests for loading authored content.
/// </summary>
/// <remarks>
/// These load the <em>real</em> files from <c>data/</c>, not a fixture. That makes the shipped
/// content itself part of the test suite: a typo in a recipe fails the build rather than the
/// server.
/// </remarks>
[Collection(SharedDatabase.Name)]
public sealed class ContentLoaderTests(DatabaseFixture fixture) : IAsyncLifetime
{
    private readonly DatabaseFixture _fixture = fixture;

    public async Task InitializeAsync() => await _fixture.ResetAsync();

    public Task DisposeAsync() => Task.CompletedTask;

    /// <summary>
    /// Walks up from the test binary to the repository's <c>data/</c> directory.
    /// </summary>
    /// <remarks>
    /// Located by walking rather than by a relative path, so the tests keep working if the build
    /// output moves.
    /// </remarks>
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

    // ── The shipped content ──────────────────────────────────────────────────

    [Fact]
    public async Task TheShippedContent_IsValid()
    {
        // The most valuable test here: whatever is in data/ must always pass validation.
        ContentPack pack = await ContentLoader.ReadAsync(ContentRoot());

        IReadOnlyList<ContentError> errors = ContentValidator.Validate(pack);

        Assert.True(
            errors.Count == 0,
            $"Shipped content has problems:{Environment.NewLine}"
            + string.Join(Environment.NewLine, errors.Select(e => $"  {e}")));
    }

    [Fact]
    public async Task TheShippedContent_GatesMiningBehindAToolAndLeavesGatheringBareHanded()
    {
        ContentPack pack = await ContentLoader.ReadAsync(ContentRoot());

        // Design bible §skills: gathering is "hand-collecting surface materials", mining is
        // "tool-gated ore extraction". The gate has always been enforced in GatheringService and
        // the column has always existed, but ResourceNodeContent had no field for it — so no
        // authored deposit could set it and every mining node was minable bare-handed, silently.
        //
        // A property rather than a count, so authoring more deposits cannot break it. It fails only
        // if a mining node ships without a tool, which is the design being violated rather than the
        // content merely changing.
        var mining = pack.ResourceNodes
            .Where(n => string.Equals(n.Skill, "mining", StringComparison.Ordinal))
            .ToList();

        Assert.NotEmpty(mining);

        foreach (ResourceNodeContent node in mining)
        {
            Assert.False(
                string.IsNullOrWhiteSpace(node.RequiredTool),
                $"Mining deposit '{node.Key}' can be worked bare-handed.");

            // Resolved by key, so a typo would otherwise surface as a KeyNotFoundException during
            // seeding — a long way from the line that caused it.
            Assert.True(
                pack.Items.Any(i => string.Equals(i.Key, node.RequiredTool, StringComparison.Ordinal)),
                $"Mining deposit '{node.Key}' requires unknown tool '{node.RequiredTool}'.");
        }

        // The other half of the rule, and the one that keeps a new character able to start at all:
        // the onboarding questline gathers scrap before it crafts anything, so if gathering ever
        // needed a tool the chain could not begin.
        foreach (ResourceNodeContent node in pack.ResourceNodes
            .Where(n => string.Equals(n.Skill, "gathering", StringComparison.Ordinal)))
        {
            Assert.True(
                string.IsNullOrWhiteSpace(node.RequiredTool),
                $"Gathering deposit '{node.Key}' needs a tool, which would strand a new character.");
        }
    }

    [Fact]
    public async Task TheShippedContent_ContainsTheOnboardingChain()
    {
        ContentPack pack = await ContentLoader.ReadAsync(ContentRoot());

        // Design-bible §4: seven quests totalling 13,000 credits, which is the entire starting
        // money supply per character and the anchor for every early-game price.
        var story = pack.Quests.Where(q => q.Kind == QuestKind.MainStory).ToList();

        Assert.Equal(7, story.Count);
        Assert.Equal(13_000, story.Sum(q => q.RewardCredits));
    }

    [Fact]
    public async Task TheShippedContent_ChainsTheOnboardingQuestsInOrder()
    {
        ContentPack pack = await ContentLoader.ReadAsync(ContentRoot());

        Dictionary<string, QuestContent> byKey = pack.Quests.ToDictionary(q => q.Key);

        // Exactly one entry point, and every other story quest reachable from it.
        var story = pack.Quests.Where(q => q.Kind == QuestKind.MainStory).ToList();

        Assert.Single(story, q => q.Prerequisite is null);

        foreach (QuestContent quest in story.Where(q => q.Prerequisite is not null))
        {
            Assert.True(
                byKey.ContainsKey(quest.Prerequisite!),
                $"'{quest.Key}' requires '{quest.Prerequisite}', which does not exist.");
        }
    }

    // ── Applying ─────────────────────────────────────────────────────────────

    [Fact]
    public async Task AuthoredPalettes_ReachTheDatabase()
    {
        // A planet's look is content, the same as its radius, and it travels the same path:
        // data/ -> seed -> Postgres -> API -> client. This is the seed half.
        await using SpaceMmoDbContext context = _fixture.CreateContext();
        await new ContentLoader(context).LoadAsync(ContentRoot());

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        ContentPack pack = await ContentLoader.ReadAsync(ContentRoot());

        // Against the pack rather than a literal count, for the reason the test below says: a
        // number that is bumped whenever content is authored catches nothing.
        foreach (BodyContent authored in pack.Bodies.Where(b => b.Appearance is not null))
        {
            Body stored = await verify.Bodies.SingleAsync(b => b.Key == authored.Key);

            Assert.Equal(authored.Appearance!.LowColour, stored.LowColour);
            Assert.Equal(authored.Appearance.HighColour, stored.HighColour);
            Assert.Equal(authored.Appearance.RockColour, stored.RockColour);
            Assert.Equal(authored.Appearance.SlopeFrom, stored.SlopeFrom);
        }

        // And that any were authored at all, since the loop above passes against none.
        Assert.Contains(pack.Bodies, b => b.Appearance is not null);
    }

    [Fact]
    public async Task AuthoredTerrainShapes_ReachTheDatabase_AndDiffer()
    {
        // Shape is what makes two worlds different places rather than one place painted twice, so
        // this asserts both that it survives the seed and that the bodies are not all the same.
        await using SpaceMmoDbContext context = _fixture.CreateContext();
        await new ContentLoader(context).LoadAsync(ContentRoot());

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        ContentPack pack = await ContentLoader.ReadAsync(ContentRoot());

        foreach (BodyContent authored in pack.Bodies.Where(b => b.Terrain is not null))
        {
            Body stored = await verify.Bodies.SingleAsync(b => b.Key == authored.Key);

            Assert.Equal(authored.Terrain!.Seed, stored.TerrainSeed);
            Assert.Equal(authored.Terrain.MaxElevationKm, stored.MaxElevationKm);
            Assert.Equal(authored.Terrain.BaseFrequency, stored.BaseFrequency);
        }

        // Distinct seeds, or every world is the same landscape wearing different colours -- which
        // would look like a content decision rather than the copy-paste it actually was.
        List<long?> seeds = await verify.Bodies
            .Where(b => b.TerrainSeed != null)
            .Select(b => b.TerrainSeed)
            .ToListAsync();

        Assert.Equal(seeds.Count, seeds.Distinct().Count());
        Assert.Contains(pack.Bodies, b => b.Terrain is not null);
    }

    [Fact]
    public async Task AuthoredGroundPads_ReachTheDatabase()
    {
        // The ground a city stands on is part of the terrain, so it travels the path the terrain does
        // (task 168). Three different numbers per pad, so a loader that wrote one field into another
        // fails rather than passing on a coincidence.
        await using SpaceMmoDbContext context = _fixture.CreateContext();
        await new ContentLoader(context).LoadAsync(ContentRoot());

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        ContentPack pack = await ContentLoader.ReadAsync(ContentRoot());

        foreach (StationContent authored in pack.Stations.Where(s => s.Pad is not null))
        {
            Station stored = await verify.Stations.SingleAsync(s => s.Key == authored.Key);

            Assert.Equal(authored.Pad!.FlatRadiusKm, stored.PadFlatRadiusKm);
            Assert.Equal(authored.Pad.BlendKm, stored.PadBlendKm);
            Assert.Equal(authored.Pad.ElevationKm, stored.PadElevationKm);
        }

        // A station that levels nothing stores nothing, rather than a pad of zeros.
        Assert.All(
            await verify.Stations.Where(s => s.PadFlatRadiusKm == null).ToListAsync(),
            s => Assert.Null(s.PadElevationKm));

        // And that any is authored, since the loop above passes against none: Borlash needs one.
        Assert.Contains(pack.Stations, s => s.Key == "station_capital_hub" && s.Pad is not null);
    }

    [Fact]
    public async Task AuthoredBodyPositions_ReachTheDatabase()
    {
        // Where a body is travels the same path its palette and shape do: data/ -> seed ->
        // Postgres -> API -> client. This is the seed half, and before 8 September there was no
        // position to carry -- which is why the client skipped four of the six seeded stations as
        // standing on bodies it had never heard of (task 157).
        await using SpaceMmoDbContext context = _fixture.CreateContext();
        await new ContentLoader(context).LoadAsync(ContentRoot());

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        ContentPack pack = await ContentLoader.ReadAsync(ContentRoot());

        foreach (BodyContent authored in pack.Bodies.Where(b => b.SystemPosition is not null))
        {
            Body stored = await verify.Bodies.SingleAsync(b => b.Key == authored.Key);

            Assert.Equal(authored.SystemPosition![0], stored.SystemX);
            Assert.Equal(authored.SystemPosition[1], stored.SystemY);
            Assert.Equal(authored.SystemPosition[2], stored.SystemZ);
        }

        // Every shipped body is placed, not merely some. A body without a position is drawn by
        // nobody, and one unplaced homeworld is a race whose starting world does not exist.
        Assert.All(pack.Bodies, b => Assert.NotNull(b.SystemPosition));

        // And the axes are not interchangeable. Asserting X, Y and Z against a pack whose bodies
        // all sat on one axis would pass with the loader writing the same component three times;
        // the shipped content is authored off-axis so that it cannot.
        Assert.Contains(
            await verify.Bodies.ToListAsync(),
            b => b.SystemX != b.SystemY && b.SystemY != b.SystemZ);
    }

    [Fact]
    public async Task TheShippedFactionSpace_ReachesTheDatabaseAsAuthored()
    {
        // Task 194: the API judges attacks from the database, never from data/. If what it builds differs from
        // what the content says by one field, every rule test stays green over geometry nobody plays in.
        await using SpaceMmoDbContext context = _fixture.CreateContext();
        await new ContentLoader(context).LoadAsync(ContentRoot());

        ContentPack pack = await ContentLoader.ReadAsync(ContentRoot());
        FactionSpaceContent authored = pack.FactionSpace!;
        double[] capital = pack.Bodies.Single(b => b.Key == authored.Capital).SystemPosition!;

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Domain.Combat.FactionSpace? served = await new SpaceMMO.Data.Combat.CombatService(verify).FactionSpaceAsync();

        Assert.NotNull(served);
        Assert.Equal(
            Domain.Combat.FactionSpace.FromContent(authored, new SystemPosition(capital[0], capital[1], capital[2])),
            served);
    }

    [Fact]
    public async Task TheFirstWeapon_ReachesTheDatabaseReadyToFire()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();
        await new ContentLoader(context).LoadAsync(ContentRoot());

        ContentPack pack = await ContentLoader.ReadAsync(ContentRoot());
        ItemContent authored = pack.Items.Single(i => i.Key == "laser_rifle");

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        ItemDef rifle = await verify.ItemDefs.Include(i => i.WeaponSkill).SingleAsync(i => i.Key == "laser_rifle");

        Assert.Equal(EquipKind.Weapon, rifle.EquipsAs);
        Assert.Equal(authored.Weapon!.Damage, rifle.WeaponDamage);
        Assert.Equal(authored.Weapon.CooldownMs, rifle.WeaponCooldownMs);
        Assert.Equal(authored.Weapon.RangeMetres, rifle.WeaponRangeMetres);
        Assert.Equal(authored.Weapon.Skill, rifle.WeaponSkill!.Key);

        // Its skill is a weapon's, with what ADR-0015 needs to share its XP.
        Assert.NotNull(rifle.WeaponSkill.AsWeapon());
        Assert.Equal(pack.Skills.Single(s => s.Key == "laser_rifle").Weapon!.XpPerHundredDamage, rifle.WeaponSkill.XpPerHundredDamage);

        // And constitution exists to take its third.
        Assert.True(await verify.Skills.AnyAsync(s => s.Key == Domain.Combat.CombatXp.ConstitutionSkillKey));
    }

    [Fact]
    public async Task EveryHomeBodysStart_ReachesTheDatabase()
    {
        // Task 145: where a new character first stands travels data/ -> seed -> Postgres -> API ->
        // game server. This is the seed half, read against the shipped content, and each component
        // separately so a loader writing one axis three times fails.
        await using (SpaceMmoDbContext context = _fixture.CreateContext())
        {
            await new ContentLoader(context).LoadAsync(ContentRoot());
        }

        ContentPack pack = await ContentLoader.ReadAsync(ContentRoot());

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        foreach (Race race in Enum.GetValues<Race>())
        {
            BodyContent authored = pack.Bodies.Single(b => b.Key == Races.HomeBodyKeyFor(race));
            Body stored = await verify.Bodies.SingleAsync(b => b.Key == authored.Key);

            Assert.NotNull(authored.StartDirection);
            Assert.Equal(authored.StartDirection![0], stored.StartX);
            Assert.Equal(authored.StartDirection[1], stored.StartY);
            Assert.Equal(authored.StartDirection[2], stored.StartZ);
        }
    }

    [Fact]
    public async Task RemovingABodyPosition_ClearsIt()
    {
        // The same reason a removed palette has to clear: seeding is how content is corrected, and
        // a body moved back to unplaced would otherwise keep the position it used to have in every
        // environment that had already seeded it -- with a planet still drawn where nothing is
        // authored any more.
        await using SpaceMmoDbContext context = _fixture.CreateContext();
        await new ContentLoader(context).LoadAsync(ContentRoot());

        await using (SpaceMmoDbContext move = _fixture.CreateContext())
        {
            Body body = await move.Bodies.FirstAsync();
            body.SystemX = 12345.0;

            await move.SaveChangesAsync();
        }

        await using (SpaceMmoDbContext reseed = _fixture.CreateContext())
        {
            await new ContentLoader(reseed).LoadAsync(ContentRoot());
        }

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.DoesNotContain(
            await verify.Bodies.Select(b => b.SystemX).ToListAsync(),
            x => x == 12345.0);
    }

    [Fact]
    public async Task RemovingAPalette_ClearsIt()
    {
        // Seeding is how content is corrected, so deleting an appearance from the JSON has to
        // remove it from the database. Left alone, a palette taken out on purpose would live on in
        // every environment that had already seeded it -- and the only symptom would be a planet
        // that still looks like something nobody can find the source of.
        await using SpaceMmoDbContext context = _fixture.CreateContext();
        await new ContentLoader(context).LoadAsync(ContentRoot());

        await using (SpaceMmoDbContext paint = _fixture.CreateContext())
        {
            Body body = await paint.Bodies.FirstAsync();
            body.LowColour = "1,0,1";

            await paint.SaveChangesAsync();
        }

        await using (SpaceMmoDbContext reseed = _fixture.CreateContext())
        {
            await new ContentLoader(reseed).LoadAsync(ContentRoot());
        }

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.DoesNotContain(
            await verify.Bodies.Select(b => b.LowColour).ToListAsync(),
            colour => colour == "1,0,1");
    }

    [Fact]
    public async Task LoadingTheShippedContent_PopulatesTheDatabase()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();
        await new ContentLoader(context).LoadAsync(ContentRoot());

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        // Counted against the pack rather than against literals. The claim worth making is that
        // the loader drops nothing, and hard-coded totals do not make it: they fail every time
        // content is authored, which trains whoever is authoring to bump the number without
        // reading why it moved — and a number that is always bumped catches nothing.
        ContentPack pack = await ContentLoader.ReadAsync(ContentRoot());

        Assert.Equal(pack.Skills.Count, await verify.Skills.CountAsync());
        Assert.Equal(pack.Items.Count, await verify.ItemDefs.CountAsync());
        Assert.Equal(pack.Recipes.Count, await verify.Recipes.CountAsync());
        Assert.Equal(pack.Quests.Count, await verify.QuestDefs.CountAsync());
        Assert.Equal(pack.ResourceNodes.Count, await verify.ResourceNodes.CountAsync());

        // And that it loaded something at all, since every count above would pass against an
        // empty directory.
        Assert.NotEmpty(pack.Items);
        Assert.NotEmpty(pack.Recipes);

        // A deposit's tool survives the trip into the database, which the counts above cannot see.
        // The authored side is checked elsewhere, and on its own it proves only that the JSON says
        // the right thing: a mistyped line in the loader would leave every mining deposit workable
        // bare-handed with the content test still green. This is the assertion that crosses the
        // boundary, so it is the one that would catch it.
        int gatedInDatabase = await verify.ResourceNodes
            .CountAsync(n => n.RequiredToolItemDefId != null);

        int gatedInPack = pack.ResourceNodes.Count(n => !string.IsNullOrWhiteSpace(n.RequiredTool));

        Assert.Equal(gatedInPack, gatedInDatabase);
        Assert.NotEqual(0, gatedInDatabase);
    }

    /// <summary>
    /// A finished quest waits to be handed in, all the way from the pack to the column.
    /// </summary>
    /// <remarks>
    /// <para>
    /// <strong>The assertion that crosses the boundary, and the one that was missing.</strong>
    /// <c>QuestDef.RequiresTurnIn</c> existed, <c>QuestService</c> branched on it, and every test of
    /// the service passed — including one proving a quest that requires turning in finishes unpaid.
    /// What nothing checked was whether any <em>shipped</em> quest could ever set it. Content had no
    /// field for it and the loader never wrote it, so the column was false for all seven, the
    /// ReadyToTurnIn state was unreachable, and every quest paid out on the last swing of a pickaxe
    /// (task 150).
    /// </para>
    /// <para>
    /// Asserted against the pack rather than a literal seven, for the reason the counts above are.
    /// </para>
    /// </remarks>
    [Fact]
    public async Task QuestsAreHandedIn_RatherThanPayingOutWhereThePlayerStands()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();
        await new ContentLoader(context).LoadAsync(ContentRoot());

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        ContentPack pack = await ContentLoader.ReadAsync(ContentRoot());

        Assert.NotEmpty(pack.Quests);

        foreach (QuestContent quest in pack.Quests)
        {
            QuestDef stored = await verify.QuestDefs.SingleAsync(q => q.Key == quest.Key);

            Assert.True(
                stored.RequiresTurnIn == quest.TurnInRequired,
                $"'{quest.Key}' is authored as turn-in {quest.TurnInRequired} and reached the "
                + $"database as {stored.RequiresTurnIn}. The loader is dropping the field, which "
                + "leaves every quest paying out the instant its last objective completes.");
        }

        // And that the shipped pack really does require it, which the loop above would satisfy
        // just as happily if everything were false on both sides.
        Assert.True(
            await verify.QuestDefs.AllAsync(q => q.RequiresTurnIn),
            "Every shipped quest should be handed in rather than paying out where the player "
            + "stands. A quest that opts out is authoring requiresTurnIn: false deliberately.");
    }

    /// <summary>
    /// Content that says nothing gets a hand-in; content that says false gets its way.
    /// </summary>
    /// <remarks>
    /// The default is the load-bearing half. Whether System.Text.Json honours an optional
    /// constructor parameter's default for an absent property has changed between versions, and a
    /// silent false is exactly the bug being fixed — so the pack models "unspecified" as null and
    /// resolves it in one place, and this is what holds that down.
    /// </remarks>
    [Fact]
    public void AQuestThatSaysNothingAboutHandingIn_IsHandedIn()
    {
        QuestContent silent = new(
            "silent", "Silent", QuestKind.MainStory, null, 0, null, 0, null, []);

        Assert.True(silent.TurnInRequired);

        QuestContent optedOut = new(
            "auto", "Auto", QuestKind.MainStory, null, 0, null, 0, null, [], RequiresTurnIn: false);

        Assert.False(optedOut.TurnInRequired);
    }

    [Fact]
    public async Task TheShippedContentReallyLocksItsPlanetMaterials()
    {
        // Read from the real files, because the rule that enforces planet-locking is worthless if
        // the flag never arrives. A misspelt JSON key leaves PlanetLocked false, the validator
        // finds nothing to check, and every other test in this file still passes — the loudest
        // possible silence.
        //
        // <strong>It used to assert the locked items were exactly the frame's four.</strong> That
        // was true until 9 October 2026, when each homeworld's starter ore was locked to it as well
        // (task 184), and "planet-locked" and "goes into the frame" stopped being one set. The
        // starter ores' flags are EveryHomeworldStartsAlikeTests' to check; this keeps the frame's.
        ContentPack pack = await ContentLoader.ReadAsync(ContentRoot());

        HashSet<string> locked = [.. pack.Items.Where(i => i.PlanetLocked).Select(i => i.Key)];

        // And that each really does come from one world, since that is the fact hauling is built
        // on rather than a property of the flag.
        foreach (string key in locked)
        {
            string[] bodies = [.. pack.ResourceNodes
                .Where(n => n.Item == key)
                .Select(n => n.Body)
                .Distinct()
                .Order()];

            Assert.Single(bodies);
        }

        // The cross-faction recipe consumes only planet-locked materials, each from a different
        // world, which is what makes two of them cross the line to reach any given builder
        // (ADR-0008). Asserted as a property rather than as a list of keys, so this says what the
        // frame is for and goes red when that stops being true, not when an item is renamed.
        RecipeContent frame = pack.Recipes.Single(r => r.Key == "build_alloy_frame");

        foreach (RecipeInputContent input in frame.Inputs)
        {
            Assert.True(
                locked.Contains(input.Item),
                $"The frame takes {input.Item}, which is not planet-locked, so it is not a reason "
                + "for anybody to cross the faction line.");
        }

        string[] frameWorlds = [.. frame.Inputs
            .SelectMany(i => pack.ResourceNodes.Where(n => n.Item == i.Item).Select(n => n.Body))
            .Distinct()
            .Order()];

        string[] homeworlds = [.. Enum.GetValues<Race>().Select(Races.HomeBodyKeyFor).Order()];

        Assert.Equal(homeworlds, frameWorlds);
    }

    [Fact]
    public async Task AFamilyStepArrivesAsOne_WithEveryHomeworldsOreInIt()
    {
        // Task 186, read from the real files and the database they seed. A misspelt "targetFamily"
        // would load as a step with no target, and a misspelt "family" would leave the step waiting
        // for an empty family; either way the validator's view of the file and the engine's view of
        // the row would disagree, and a quest would sit at zero while the player mined.
        await using (SpaceMmoDbContext context = _fixture.CreateContext())
        {
            await new ContentLoader(context).LoadAsync(ContentRoot());
        }

        ContentPack pack = await ContentLoader.ReadAsync(ContentRoot());

        QuestStepContent authored = pack.Quests
            .SelectMany(q => q.Steps)
            .Single(s => s.TargetFamily is not null);

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        QuestStep stored = await verify.QuestSteps.SingleAsync(s => s.TargetIsFamily);

        Assert.Equal(authored.TargetFamily, stored.TargetKey);

        string[] members = [.. await verify.ItemDefs
            .Where(i => i.Family == stored.TargetKey)
            .Select(i => i.Key)
            .ToListAsync()];

        // Every race can satisfy it from home: one member of the family is on each homeworld.
        foreach (Race race in Enum.GetValues<Race>())
        {
            string home = Races.HomeBodyKeyFor(race);

            Assert.True(
                await verify.ResourceNodes.AnyAsync(
                    n => n.Body!.Key == home && members.Contains(n.ItemDef!.Key)),
                $"No member of '{stored.TargetKey}' ({string.Join(", ", members)}) is mined on {home}.");
        }
    }

    [Fact]
    public async Task ADepositContentNoLongerAuthors_IsRemovedWithItsPool()
    {
        // Task 184. Nothing removed a deposit, so one deleted from the file -- or renamed in it --
        // went on being served from the database, on its old body, gatherable, and invisible to the
        // validator. The Capital's ferrite would have outlived ferrite becoming Terra's.
        await using (SpaceMmoDbContext first = _fixture.CreateContext())
        {
            await new ContentLoader(first).LoadAsync(ContentRoot());
        }

        ContentPack pack = await ContentLoader.ReadAsync(ContentRoot());

        // Scrap, because removing one of a common material leaves a pack that still validates.
        ResourceNodeContent retired = pack.ResourceNodes.First(n => n.Item == "scrap_alloy");

        // Somebody has gathered from it, so it has a pool: the row whose foreign key refuses the
        // delete if it is not removed first.
        await using (SpaceMmoDbContext gathered = _fixture.CreateContext())
        {
            ResourceNode node = await gathered.ResourceNodes.SingleAsync(n => n.Key == retired.Key);

            gathered.ResourceNodeStates.Add(
                new ResourceNodeState { ResourceNodeId = node.Id, QuantityRemaining = 3 });

            await gathered.SaveChangesAsync();
        }

        ContentPack edited = pack with
        {
            ResourceNodes = [.. pack.ResourceNodes.Where(n => n.Key != retired.Key)],
        };

        await using (SpaceMmoDbContext second = _fixture.CreateContext())
        {
            await new ContentLoader(second).ApplyAsync(edited);
        }

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.False(
            await verify.ResourceNodes.AnyAsync(n => n.Key == retired.Key),
            $"{retired.Key} is no longer authored and is still in the database.");

        Assert.Empty(await verify.ResourceNodeStates.ToListAsync());

        // And only it: every deposit still authored is still there.
        string[] remaining = [.. (await verify.ResourceNodes.Select(n => n.Key).ToListAsync()).Order()];

        Assert.Equal(edited.ResourceNodes.Select(n => n.Key).Order(), remaining);
    }

    [Fact]
    public async Task StationsArriveWithSomewhereToBe()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();
        await new ContentLoader(context).LoadAsync(ContentRoot());

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        // A station on a body carries a direction and no system position. Nothing can dock at a
        // station whose position never arrived, so a mis-parsed field presents as a station that
        // exists and cannot be reached.
        Station capital = await verify.Stations.SingleAsync(s => s.Key == "station_capital_hub");

        Assert.NotNull(capital.BodyId);
        Assert.NotNull(capital.DirectionX);
        Assert.Null(capital.SystemX);
        Assert.True(capital.DockingRangeKilometres > 0.0);

        // And the deep-space one is the mirror image, which is what makes the second branch real
        // rather than a code path nothing exercises.
        Station deepdock = await verify.Stations.SingleAsync(s => s.Key == "station_deepdock");

        Assert.Null(deepdock.BodyId);
        Assert.Null(deepdock.DirectionX);
        Assert.NotNull(deepdock.SystemX);
        Assert.Equal(StationKind.Spaceport, deepdock.Kind);

        // Every station is placed exactly one way. Both at once is what the validator refuses, and
        // it can only be reached by editing a station that already existed — the case nobody
        // reloads by hand.
        foreach (Station station in await verify.Stations.ToListAsync())
        {
            Assert.True(
                (station.DirectionX is null) != (station.SystemX is null),
                $"{station.Key} is placed {(station.DirectionX is null ? "no" : "both")} ways.");
        }
    }

    [Fact]
    public async Task MovingAStationIntoDeepSpaceLeavesNoOldPositionBehind()
    {
        await using (SpaceMmoDbContext first = _fixture.CreateContext())
        {
            await new ContentLoader(first).LoadAsync(ContentRoot());
        }

        ContentPack pack = await ContentLoader.ReadAsync(ContentRoot());

        // The same station, relocated off its body. A reload that only wrote the new position
        // would leave the old direction beside it, producing exactly the row the validator will
        // not let anyone author.
        // Its pad goes with it: a station in deep space has no ground to level, and the validator
        // refuses one that claims to (task 168).
        StationContent moved = pack.Stations.Single(s => s.Key == "station_capital_hub") with
        {
            Body = null,
            Direction = null,
            SystemPosition = [12.0, 3.0, -4.0],
            Pad = null,
        };

        ContentPack edited = pack with
        {
            Stations = [.. pack.Stations.Where(s => s.Key != moved.Key), moved],
        };

        await using (SpaceMmoDbContext second = _fixture.CreateContext())
        {
            await new ContentLoader(second).ApplyAsync(edited);
        }

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Station station = await verify.Stations.SingleAsync(s => s.Key == moved.Key);

        Assert.Null(station.DirectionX);
        Assert.Null(station.DirectionY);
        Assert.Null(station.DirectionZ);
        Assert.Equal(12.0, station.SystemX);

        // And the ground it used to level is not left levelled under a station that has gone.
        Assert.Null(station.PadFlatRadiusKm);
        Assert.Null(station.PadBlendKm);
        Assert.Null(station.PadElevationKm);
    }

    [Fact]
    public async Task LoadedRecipes_KeepTheirInputsAndGates()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();
        await new ContentLoader(context).LoadAsync(ContentRoot());

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Recipe refine = await verify.Recipes
            .Include(r => r.Inputs)
            .SingleAsync(r => r.Key == "refine_ferrite_plate");

        Assert.Equal(4, refine.OutputQuantity);
        Assert.Equal(60, refine.JobSeconds);

        RecipeInput input = Assert.Single(refine.Inputs);
        Assert.Equal(20, input.Quantity);
    }

    [Fact]
    public async Task LoadedQuests_KeepTheirPrerequisiteChain()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();
        await new ContentLoader(context).LoadAsync(ContentRoot());

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        QuestDef craftTool = await verify.QuestDefs
            .Include(q => q.PrerequisiteQuestDef)
            .SingleAsync(q => q.Key == "intro_craft_tool");

        Assert.Equal("intro_gather_scrap", craftTool.PrerequisiteQuestDef!.Key);

        // Credits are authored as whole credits and stored as minor units.
        Assert.Equal(Credits.FromWholeCredits(750), craftTool.RewardCredits);
    }

    [Fact]
    public async Task LoadedItems_KeepTheirCategories()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();
        await new ContentLoader(context).LoadAsync(ContentRoot());

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        ItemDef laser = await verify.ItemDefs.SingleAsync(i => i.Key == "crude_mining_laser");

        Assert.Equal(ItemCategory.Tool, laser.Category);
        Assert.False(laser.IsStackable);
    }

    // ── Idempotence ──────────────────────────────────────────────────────────

    [Fact]
    public async Task LoadingTwice_ChangesNothing()
    {
        // Content loads at every startup, so it must converge on the files rather than
        // accumulating duplicates.
        await using (SpaceMmoDbContext first = _fixture.CreateContext())
        {
            await new ContentLoader(first).LoadAsync(ContentRoot());
        }

        await using (SpaceMmoDbContext second = _fixture.CreateContext())
        {
            await new ContentLoader(second).LoadAsync(ContentRoot());
        }

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        ContentPack pack = await ContentLoader.ReadAsync(ContentRoot());

        Assert.Equal(pack.Items.Count, await verify.ItemDefs.CountAsync());
        Assert.Equal(pack.Recipes.Count, await verify.Recipes.CountAsync());
        Assert.Equal(pack.Quests.Count, await verify.QuestDefs.CountAsync());

        Assert.Equal(
            pack.Quests.Sum(q => q.Steps.Count), await verify.QuestSteps.CountAsync());

        // Doubling here would mean the wholesale replace is appending rather than replacing,
        // which is the failure this test exists for and the one a literal total hides once
        // somebody has bumped it twice.
        Assert.Equal(
            pack.Recipes.Sum(r => r.Inputs.Count), await verify.RecipeInputs.CountAsync());
    }

    [Fact]
    public async Task Reloading_AppliesEdits()
    {
        await using (SpaceMmoDbContext first = _fixture.CreateContext())
        {
            await new ContentLoader(first).LoadAsync(ContentRoot());
        }

        ContentPack pack = await ContentLoader.ReadAsync(ContentRoot());

        // A balance change: halve the refining duration.
        RecipeContent original = pack.Recipes.Single(r => r.Key == "refine_ferrite_plate");
        RecipeContent tweaked = original with { JobSeconds = 30 };

        var edited = pack with
        {
            Recipes = [.. pack.Recipes.Where(r => r.Key != original.Key), tweaked],
        };

        await using (SpaceMmoDbContext second = _fixture.CreateContext())
        {
            await new ContentLoader(second).ApplyAsync(edited);
        }

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.Equal(
            30, (await verify.Recipes.SingleAsync(r => r.Key == original.Key)).JobSeconds);

        // Edited, not added.
        Assert.Equal(edited.Recipes.Count, await verify.Recipes.CountAsync());
    }

    [Fact]
    public async Task Reloading_RemovesIngredientsThatWereDropped()
    {
        // Inputs are replaced wholesale rather than diffed. A dropped ingredient left behind would
        // quietly keep charging players for a material the recipe no longer needs.
        await using (SpaceMmoDbContext first = _fixture.CreateContext())
        {
            await new ContentLoader(first).LoadAsync(ContentRoot());
        }

        ContentPack pack = await ContentLoader.ReadAsync(ContentRoot());

        RecipeContent original = pack.Recipes.Single(r => r.Key == "build_shuttle_hull_section");
        Assert.Equal(2, original.Inputs.Count);

        RecipeContent simplified = original with
        {
            Inputs = [original.Inputs.First(i => i.Item == "ferrite_plate")],
        };

        var edited = pack with
        {
            Recipes = [.. pack.Recipes.Where(r => r.Key != original.Key), simplified],
        };

        await using (SpaceMmoDbContext second = _fixture.CreateContext())
        {
            await new ContentLoader(second).ApplyAsync(edited);
        }

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Recipe stored = await verify.Recipes
            .Include(r => r.Inputs)
            .SingleAsync(r => r.Key == original.Key);

        Assert.Single(stored.Inputs);
    }

    // ── Failure modes ────────────────────────────────────────────────────────

    [Fact]
    public async Task InvalidContent_IsRejectedBeforeAnythingIsWritten()
    {
        // Applied as a unit, so one bad recipe cannot leave the database half-updated.
        var broken = new ContentPack(
            [],
            [],
            [new RecipeContent("r1", "ghost", 1, "phantom", 1, 60, 0, null,
                [new RecipeInputContent("vapour", 1)])],
            [],
            [],
            [],
            [],
            []);

        await using SpaceMmoDbContext context = _fixture.CreateContext();

        await Assert.ThrowsAsync<InvalidOperationException>(
            () => new ContentLoader(context).ApplyAsync(broken));

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.Equal(0, await verify.Recipes.CountAsync());
    }

    [Fact]
    public async Task AMissingContentDirectory_FailsClearly()
    {
        await Assert.ThrowsAsync<DirectoryNotFoundException>(
            () => ContentLoader.ReadAsync(Path.Combine(Path.GetTempPath(), "spacemmo-not-here")));
    }
}
