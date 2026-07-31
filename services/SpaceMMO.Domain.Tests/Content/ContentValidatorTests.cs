using SpaceMMO.Domain.Characters;
using SpaceMMO.Domain.Content;
using SpaceMMO.Domain.Items;
using SpaceMMO.Domain.Progression;
using SpaceMMO.Domain.Quests;
using SpaceMMO.Domain.Universe;
using Xunit;

namespace SpaceMMO.Domain.Tests.Content;

/// <summary>
/// Tests for content validation.
/// </summary>
/// <remarks>
/// The validator's job is to turn content mistakes into startup failures instead of runtime
/// mysteries. A recipe pointing at a nonexistent item should be a line in an error list, not a
/// null reference three weeks later when someone finally crafts it.
/// </remarks>
public sealed class ContentValidatorTests
{
    private static SkillContent Skill(string key = "refining") =>
        new(key, "Refining", SkillCategory.Life);

    private static ItemContent Item(
        string key, ItemCategory category = ItemCategory.Raw, double volume = 1.0) =>
        new(key, key, category, volume);

    private static RecipeContent Recipe(
        string key = "r1",
        string output = "plate",
        string skill = "refining",
        int level = 1,
        string? tool = null,
        params RecipeInputContent[] inputs) =>
        new(key, output, 1, skill, level, 60, 100, tool,
            inputs.Length > 0 ? inputs : [new RecipeInputContent("ore", 1)]);

    private static QuestContent Quest(
        string key = "q1",
        QuestKind kind = QuestKind.MainStory,
        string? prerequisite = null,
        int? cooldown = null,
        string? rewardSkill = null,
        long rewardXp = 0,
        params QuestStepContent[] steps) =>
        new(key, key, kind, prerequisite, 100, rewardSkill, rewardXp, cooldown,
            steps.Length > 0
                ? steps
                : [new QuestStepContent(1, "Do the thing.", ObjectiveType.Gather, "ore", 5)]);

    private static ContentPack Pack(
        IReadOnlyList<SkillContent>? skills = null,
        IReadOnlyList<ItemContent>? items = null,
        IReadOnlyList<RecipeContent>? recipes = null,
        IReadOnlyList<QuestContent>? quests = null,
        IReadOnlyList<StarSystemContent>? systems = null,
        IReadOnlyList<BodyContent>? bodies = null,
        IReadOnlyList<StationContent>? stations = null) =>
        new(skills ?? [Skill()],
            items ?? [Item("ore"), Item("plate", ItemCategory.Refined)],
            recipes ?? [],
            quests ?? [],
            systems ?? [],
            // Empty by default. ValidateUniverse only demands the four homeworlds once a pack
            // authors any body at all, so recipe and quest tests are unaffected by it.
            bodies ?? [],
            stations ?? []);

    private static bool HasError(IReadOnlyList<ContentError> errors, string fragment) =>
        errors.Any(e => e.Message.Contains(fragment, StringComparison.OrdinalIgnoreCase));

    // ── A valid pack ─────────────────────────────────────────────────────────

    [Fact]
    public void AValidPack_HasNoErrors()
    {
        Assert.Empty(ContentValidator.Validate(Pack(recipes: [Recipe()], quests: [Quest()])));
    }

    [Fact]
    public void AnEmptyPack_IsValid()
    {
        // Nothing to be wrong with. Useful because content is merged from many files, any of
        // which may define only one section.
        Assert.Empty(ContentValidator.Validate(ContentPack.Empty));
    }

    [Fact]
    public void ValidateOrThrow_ReportsEveryProblemAtOnce()
    {
        // Fixing errors one server restart at a time is miserable; the message lists them all.
        ContentPack pack = Pack(recipes: [Recipe(output: "nope", skill: "missing")]);

        InvalidOperationException error =
            Assert.Throws<InvalidOperationException>(() => ContentValidator.ValidateOrThrow(pack));

        Assert.Contains("nope", error.Message, StringComparison.Ordinal);
        Assert.Contains("missing", error.Message, StringComparison.Ordinal);
    }

    // ── Duplicates and references ────────────────────────────────────────────

    [Fact]
    public void DuplicateKeys_AreRejected()
    {
        // An upsert would silently pick a winner, and the author would never learn which of their
        // two definitions is live.
        IReadOnlyList<ContentError> errors =
            ContentValidator.Validate(Pack(items: [Item("ore"), Item("ore")]));

        Assert.True(HasError(errors, "Duplicate key"));
    }

    [Fact]
    public void UnknownRecipeReferences_AreRejected()
    {
        IReadOnlyList<ContentError> errors = ContentValidator.Validate(
            Pack(recipes: [Recipe(output: "ghost", skill: "phantom",
                inputs: new RecipeInputContent("vapour", 1))]));

        Assert.True(HasError(errors, "Unknown output item 'ghost'"));
        Assert.True(HasError(errors, "Unknown skill 'phantom'"));
        Assert.True(HasError(errors, "Unknown input item 'vapour'"));
    }

    [Fact]
    public void AToolGateOnANonToolItem_IsRejected()
    {
        // The tool check looks for a tracked instance, and stackable items have none — so this
        // gate could never be satisfied by any player.
        IReadOnlyList<ContentError> errors =
            ContentValidator.Validate(Pack(recipes: [Recipe(tool: "ore")]));

        Assert.True(HasError(errors, "not a tool-category item"));
    }

    [Fact]
    public void AValidToolGate_IsAccepted()
    {
        ContentPack pack = Pack(
            items: [Item("ore"), Item("plate", ItemCategory.Refined), Item("laser", ItemCategory.Tool)],
            recipes: [Recipe(tool: "laser")]);

        Assert.Empty(ContentValidator.Validate(pack));
    }

    // ── Ranges ───────────────────────────────────────────────────────────────

    [Theory]
    [InlineData(0)]
    [InlineData(100)]
    public void ARecipeLevelOutsideTheCurve_IsRejected(int level)
    {
        IReadOnlyList<ContentError> errors =
            ContentValidator.Validate(Pack(recipes: [Recipe(level: level)]));

        Assert.True(HasError(errors, "Required level"));
    }

    [Fact]
    public void NonPositiveQuantitiesAndDurations_AreRejected()
    {
        var recipe = new RecipeContent(
            "r1", "plate", 0, "refining", 1, 0, -1, null, [new RecipeInputContent("ore", 0)]);

        IReadOnlyList<ContentError> errors = ContentValidator.Validate(Pack(recipes: [recipe]));

        Assert.True(HasError(errors, "Output quantity must be positive"));
        Assert.True(HasError(errors, "Job duration must be positive"));
        Assert.True(HasError(errors, "XP per run cannot be negative"));
        Assert.True(HasError(errors, "quantity must be positive"));
    }

    [Fact]
    public void ANonPositiveItemVolume_IsRejected()
    {
        IReadOnlyList<ContentError> errors =
            ContentValidator.Validate(Pack(items: [Item("ore", volume: 0)]));

        Assert.True(HasError(errors, "Volume must be positive"));
    }

    // ── Material conservation ────────────────────────────────────────────────

    [Fact]
    public void ARecipeWithNoInputs_IsRejected()
    {
        // Turning time into material from nothing is a faucet outside gathering, which is the one
        // place material is supposed to enter the economy.
        var recipe = new RecipeContent("r1", "plate", 1, "refining", 1, 60, 0, null, []);

        IReadOnlyList<ContentError> errors = ContentValidator.Validate(Pack(recipes: [recipe]));

        Assert.True(HasError(errors, "create material from nothing"));
    }

    [Fact]
    public void AnItemThatIsBothInputAndOutput_IsRejected()
    {
        IReadOnlyList<ContentError> errors = ContentValidator.Validate(
            Pack(recipes: [Recipe(output: "plate", inputs: new RecipeInputContent("plate", 1))]));

        Assert.True(HasError(errors, "conversion loop"));
    }

    [Fact]
    public void ARecipeDependencyCycle_IsRejected()
    {
        // Ore makes plate and plate makes ore: players could convert back and forth forever.
        // Catching this in content is far cheaper than reading it off a price chart months later.
        ContentPack pack = Pack(recipes:
        [
            Recipe("r1", output: "plate", inputs: new RecipeInputContent("ore", 1)),
            Recipe("r2", output: "ore", inputs: new RecipeInputContent("plate", 1)),
        ]);

        IReadOnlyList<ContentError> errors = ContentValidator.Validate(pack);

        Assert.True(HasError(errors, "dependency cycle"));
    }

    [Fact]
    public void ADeepRecipeChain_IsNotMistakenForACycle()
    {
        // A long chain revisits shared ingredients, which a naive check would flag.
        ContentPack pack = Pack(
            items: [Item("ore"), Item("plate"), Item("part"), Item("hull")],
            recipes:
            [
                Recipe("r1", output: "plate", inputs: new RecipeInputContent("ore", 1)),
                Recipe("r2", output: "part",
                    inputs: [new RecipeInputContent("plate", 1), new RecipeInputContent("ore", 1)]),
                Recipe("r3", output: "hull",
                    inputs: [new RecipeInputContent("part", 1), new RecipeInputContent("plate", 1)]),
            ]);

        Assert.Empty(ContentValidator.Validate(pack));
    }

    [Fact]
    public void DuplicateRecipeInputs_AreRejected()
    {
        // The database keys inputs by (recipe, item), so a duplicate would drop a row rather than
        // summing the quantities.
        IReadOnlyList<ContentError> errors = ContentValidator.Validate(Pack(recipes:
        [
            Recipe(inputs: [new RecipeInputContent("ore", 1), new RecipeInputContent("ore", 2)]),
        ]));

        Assert.True(HasError(errors, "listed more than once"));
    }

    // ── Quests ───────────────────────────────────────────────────────────────

    [Fact]
    public void AnUnknownPrerequisite_IsRejected()
    {
        IReadOnlyList<ContentError> errors =
            ContentValidator.Validate(Pack(quests: [Quest(prerequisite: "ghost")]));

        Assert.True(HasError(errors, "Unknown prerequisite"));
    }

    [Fact]
    public void APrerequisiteCycle_IsRejected()
    {
        // Every quest in the loop would be permanently unreachable.
        ContentPack pack = Pack(quests:
        [
            Quest("a", prerequisite: "b"),
            Quest("b", prerequisite: "a"),
        ]);

        IReadOnlyList<ContentError> errors = ContentValidator.Validate(pack);

        Assert.True(HasError(errors, "Prerequisite cycle"));
    }

    [Fact]
    public void NonContiguousStepOrdinals_AreRejected()
    {
        // The engine advances by incrementing, so a gap leaves a quest stuck on a step that does
        // not exist.
        IReadOnlyList<ContentError> errors = ContentValidator.Validate(Pack(quests:
        [
            Quest(steps:
            [
                new QuestStepContent(1, "First.", ObjectiveType.Gather, "ore", 1),
                new QuestStepContent(3, "Third.", ObjectiveType.Gather, "ore", 1),
            ]),
        ]));

        Assert.True(HasError(errors, "no gaps"));
    }

    [Fact]
    public void AQuestWithNoSteps_IsRejected()
    {
        var quest = new QuestContent("q1", "q1", QuestKind.MainStory, null, 100, null, 0, null, []);

        Assert.True(HasError(ContentValidator.Validate(Pack(quests: [quest])), "no steps"));
    }

    [Fact]
    public void ASidequestWithoutACooldown_IsRejected()
    {
        // A repeatable with no cooldown is an unbounded faucet regardless of the daily cap.
        IReadOnlyList<ContentError> errors =
            ContentValidator.Validate(Pack(quests: [Quest(kind: QuestKind.Sidequest)]));

        Assert.True(HasError(errors, "must define a cooldown"));
    }

    [Fact]
    public void AOneShotQuestWithACooldown_IsRejected()
    {
        IReadOnlyList<ContentError> errors = ContentValidator.Validate(
            Pack(quests: [Quest(kind: QuestKind.MainStory, cooldown: 3_600)]));

        Assert.True(HasError(errors, "cannot have a cooldown"));
    }

    [Fact]
    public void XpWithNoNamedSkill_IsRejected()
    {
        IReadOnlyList<ContentError> errors =
            ContentValidator.Validate(Pack(quests: [Quest(rewardXp: 500)]));

        Assert.True(HasError(errors, "names no skill"));
    }

    [Theory]
    [InlineData(ObjectiveType.Travel)]
    [InlineData(ObjectiveType.Dock)]
    [InlineData(ObjectiveType.Talk)]
    public void ASingularObjectiveAskingForMoreThanOne_IsRejected(ObjectiveType objective)
    {
        // Each such event carries a quantity of one, so a step asking for three could never
        // complete.
        IReadOnlyList<ContentError> errors = ContentValidator.Validate(Pack(quests:
        [
            Quest(steps: [new QuestStepContent(1, "Go there.", objective, "station", 3)]),
        ]));

        Assert.True(HasError(errors, "must have quantity 1"));
    }

    [Fact]
    public void ASingularObjectiveAskingForOne_IsAccepted()
    {
        ContentPack pack = Pack(quests:
        [
            Quest(steps: [new QuestStepContent(1, "Go there.", ObjectiveType.Dock, "station", 1)]),
        ]);

        Assert.Empty(ContentValidator.Validate(pack));
    }

    [Fact]
    public void Validate_WithNullPack_Throws()
    {
        Assert.Throws<ArgumentNullException>(() => ContentValidator.Validate(null!));
    }

    // ── Universe ─────────────────────────────────────────────────────────────

    [Fact]
    public void EveryRaceMustHaveAStartingBody()
    {
        // The one that matters. A missing homeworld does not degrade something — character
        // creation fails outright for everyone who picks that race, and it fails at the API with
        // a 500 rather than at load with an explanation. This is exactly the state the live
        // database was in.
        ContentPack missingOrcHome = Pack(
            systems: [System()],
            bodies:
            [
                Body("body_terra"),
                Body("body_ares"),
                Body("body_verdance"),
            ]);

        Assert.True(HasError(
            ContentValidator.Validate(missingOrcHome), "Missing starting body for SpaceOrc"));
    }

    [Fact]
    public void AFullUniverseValidates()
    {
        Assert.Empty(ContentValidator.Validate(Pack(
            systems: [System()],
            bodies: AllHomeworlds(),
            stations: [Station("station_hub", body: "body_terra")])));
    }

    [Fact]
    public void ABodyInAnUnknownSystemIsRejected()
    {
        ContentPack pack = Pack(
            systems: [System()],
            bodies: [.. AllHomeworlds(), Body("body_orphan", system: "system_nowhere")]);

        Assert.True(HasError(ContentValidator.Validate(pack), "Unknown system 'system_nowhere'"));
    }

    [Fact]
    public void AStationOnAnUnknownBodyIsRejected()
    {
        ContentPack pack = Pack(
            systems: [System()],
            bodies: AllHomeworlds(),
            stations: [Station("station_ghost", body: "body_nowhere")]);

        Assert.True(HasError(ContentValidator.Validate(pack), "Unknown body 'body_nowhere'"));
    }

    [Fact]
    public void ADeepSpaceStationNeedsNoBody()
    {
        // A null body is legitimate — a station orbiting nothing — and must not be confused with
        // a dangling reference.
        Assert.Empty(ContentValidator.Validate(Pack(
            systems: [System()],
            bodies: AllHomeworlds(),
            stations: [Station("station_deep", body: null)])));
    }

    [Theory]
    [InlineData(0.0)]
    [InlineData(-1.0)]
    public void ABodyRadiusMustBePositive(double radius)
    {
        // Zero makes every gravity and altitude calculation degenerate; negative inverts "up".
        ContentPack pack = Pack(
            systems: [System()],
            bodies: [.. AllHomeworlds(), Body("body_flat", radiusKm: radius)]);

        Assert.True(HasError(ContentValidator.Validate(pack), "Radius must be positive"));
    }

    [Fact]
    public void APackWithNoUniverseAtAllIsStillValid()
    {
        // Content is split across files, so a pack holding only recipes is an ordinary thing to
        // validate. Demanding homeworlds of it would fail every other test in this class.
        Assert.Empty(ContentValidator.Validate(Pack()));
    }

    private static StarSystemContent System(string key = "system_origin") =>
        new(key, "Origin", 0, 0, 0, 42, SecurityLevel.Secure);

    private static BodyContent Body(
        string key, string system = "system_origin", double radiusKm = 637.1) =>
        new(key, key, system, BodyKind.Planet, SecurityLevel.Secure, radiusKm);

    private static StationContent Station(string key, string? body) =>
        new(key, key, "system_origin", body, StationKind.TradingHub);

    /// <summary>The four starting bodies every race needs, so a test can add one more problem.</summary>
    private static IReadOnlyList<BodyContent> AllHomeworlds() =>
        [.. Enum.GetValues<Race>().Select(r => Body(Races.HomeBodyKeyFor(r)))];
}
