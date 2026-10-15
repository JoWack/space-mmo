using SpaceMMO.Data.Content;
using SpaceMMO.Domain.Characters;
using SpaceMMO.Domain.Combat;
using SpaceMMO.Domain.Content;
using SpaceMMO.Domain.Universe;
using Xunit;

namespace SpaceMMO.Data.Tests.Content;

/// <summary>
/// The PvP geometry in <c>data/universe/origin.json</c> fits the planets it is drawn over (task 104).
/// </summary>
/// <remarks>
/// <para>
/// <strong>The test that was missing.</strong> <c>Pvp.CanAttack</c> was built in August with twelve
/// tests, every one over geometry of its own, while the defaults it shipped with made the whole real
/// system anchorage and put Terra on Faction B's side. This reads the shipped pack and asks the rule
/// about the shipped planets.
/// </para>
/// <para>
/// The validator checks homeworld centres. This checks the whole planet, against the radius every body
/// is drawn at: a compiled-in client constant, 20 km
/// (<c>USpaceMMOWorldSubsystem::StartingPlanet()</c>), copied here because the server cannot read it.
/// </para>
/// </remarks>
public sealed class TheShippedFactionSpaceFitsTheSystemTests
{
    /// <summary>The radius every body is drawn at, in system kilometres.</summary>
    private const double DrawnRadiusKm = 20.0;

    private static readonly DateTimeOffset Now = new(2026, 10, 14, 12, 0, 0, TimeSpan.Zero);

    public static IEnumerable<object[]> EveryRace() =>
        Enum.GetValues<Race>().Select(race => new object[] { race });

    [Theory]
    [MemberData(nameof(EveryRace))]
    public async Task Every_homeworld_lies_wholly_in_its_own_factions_space(Race race)
    {
        (ContentPack pack, FactionSpace space) = await ShippedAsync();

        SystemPosition home = PositionOf(pack, Races.HomeBodyKeyFor(race));

        Assert.Equal(Races.FactionFor(race), space.TerritoryAt(home));

        // Clear of the divide by more than its own radius, so no part of the planet is the other side's.
        double clearance = Math.Abs(SignedDistanceToDivide(space, home));

        Assert.True(
            clearance > DrawnRadiusKm,
            $"{race}'s homeworld is {clearance:0.0} km from the divide, less than its {DrawnRadiusKm} km radius.");

        // And its nearest ground is out beyond the contested ring.
        double nearestGround = home.DistanceTo(space.Capital) - DrawnRadiusKm;

        Assert.True(
            nearestGround > space.ContestedRadiusKilometres,
            $"{race}'s homeworld reaches {nearestGround:0} km from the Capital, inside the contested ring.");
    }

    [Fact]
    public async Task Both_places_to_dock_at_the_centre_are_in_the_anchorage()
    {
        (ContentPack pack, FactionSpace space) = await ShippedAsync();

        // The Capital's centre, and its whole drawn surface with it.
        Assert.Equal(PvpZone.Anchorage, space.ZoneAt(space.Capital));
        Assert.True(DrawnRadiusKm < space.SafeRadiusKilometres);

        StationContent deepdock = pack.Stations.Single(s => s.Key == "station_deepdock");
        double[] at = deepdock.SystemPosition!;

        Assert.Equal(PvpZone.Anchorage, space.ZoneAt(new SystemPosition(at[0], at[1], at[2])));
    }

    [Fact]
    public async Task The_rule_over_the_real_system_answers_as_ADR_0009_says()
    {
        (ContentPack pack, FactionSpace space) = await ShippedAsync();

        SystemPosition terra = PositionOf(pack, Races.HomeBodyKeyFor(Race.Humanoid));

        // Just above Terra: Faction A's own space.
        SystemPosition overTerra = Toward(terra, space.Capital, DrawnRadiusKm + 1.0);

        // Halfway home: the contested ring.
        SystemPosition halfway = Toward(space.Capital, terra, 80.0);

        Assert.Equal(PvpZone.FactionSpace, space.ZoneAt(overTerra));
        Assert.Equal(PvpZone.Approach, space.ZoneAt(halfway));

        // Over Terra, the local may fire first and the intruder may not.
        Assert.True(Pvp.CanAttack(space, Attempt(Faction.A, Faction.B, overTerra)));
        Assert.False(Pvp.CanAttack(space, Attempt(Faction.B, Faction.A, overTerra)));

        // Halfway, either may.
        Assert.True(Pvp.CanAttack(space, Attempt(Faction.B, Faction.A, halfway)));

        // At the Capital, nobody.
        Assert.False(Pvp.CanAttack(space, Attempt(Faction.B, Faction.A, space.Capital)));

        Assert.Equal(TimeSpan.FromMinutes(5), space.RetaliationWindow);
    }

    [Fact]
    public async Task Faction_space_authored_twice_is_refused_naming_the_file()
    {
        string root = Path.Combine(Path.GetTempPath(), $"spacemmo-factionspace-{Guid.NewGuid():N}");
        Directory.CreateDirectory(root);

        try
        {
            const string Space =
                """{ "factionSpace": { "capital": "body_capital", "safeRadiusKm": 40, "contestedRadiusKm": 120, "factionADirection": [1, 0, 0], "retaliationSeconds": 300 } }""";

            await File.WriteAllTextAsync(Path.Combine(root, "a.json"), Space);
            await File.WriteAllTextAsync(Path.Combine(root, "b.json"), Space);

            InvalidOperationException error =
                await Assert.ThrowsAsync<InvalidOperationException>(() => ContentLoader.ReadAsync(root));

            Assert.Contains("b.json", error.Message, StringComparison.Ordinal);
        }
        finally
        {
            Directory.Delete(root, recursive: true);
        }
    }

    // ── Helpers ──────────────────────────────────────────────────────────────

    private static async Task<(ContentPack Pack, FactionSpace Space)> ShippedAsync()
    {
        ContentPack pack = await ContentLoader.ReadAsync(ContentRoot());

        Assert.NotNull(pack.FactionSpace);

        FactionSpaceContent authored = pack.FactionSpace!;

        return (pack, FactionSpace.FromContent(authored, PositionOf(pack, authored.Capital)));
    }

    private static SystemPosition PositionOf(ContentPack pack, string bodyKey)
    {
        double[] p = pack.Bodies.Single(b => b.Key == bodyKey).SystemPosition!;

        return new SystemPosition(p[0], p[1], p[2]);
    }

    /// <summary>How far a point is from the plane through the Capital, positive on Faction A's side.</summary>
    private static double SignedDistanceToDivide(FactionSpace space, SystemPosition point)
    {
        (double x, double y, double z) = space.FactionADirection;
        double length = Math.Sqrt((x * x) + (y * y) + (z * z));

        return (((point.XKilometres - space.Capital.XKilometres) * x)
            + ((point.YKilometres - space.Capital.YKilometres) * y)
            + ((point.ZKilometres - space.Capital.ZKilometres) * z)) / length;
    }

    /// <summary>A point a distance from one place toward another.</summary>
    private static SystemPosition Toward(SystemPosition from, SystemPosition to, double kilometres)
    {
        double distance = from.DistanceTo(to);
        double t = kilometres / distance;

        return new SystemPosition(
            from.XKilometres + ((to.XKilometres - from.XKilometres) * t),
            from.YKilometres + ((to.YKilometres - from.YKilometres) * t),
            from.ZKilometres + ((to.ZKilometres - from.ZKilometres) * t));
    }

    private static AttackAttempt Attempt(Faction attacker, Faction target, SystemPosition where) => new()
    {
        AttackerFaction = attacker,
        TargetFaction = target,
        Position = where,
        Now = Now,
    };

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
