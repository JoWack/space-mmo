using SpaceMMO.Data.Docking;
using SpaceMMO.Data.Entities;
using SpaceMMO.Data.Whereabouts;
using SpaceMMO.Domain.Characters;
using SpaceMMO.Domain.Economy;
using SpaceMMO.Domain.Universe;
using Xunit;

namespace SpaceMMO.Data.Tests.Whereabouts;

/// <summary>
/// Where a character was when the world last saw them (task 147).
/// </summary>
/// <remarks>
/// The tests that matter here are about the absence of a position rather than the presence of one.
/// A stored position that reads back wrong shows up on the first sign-in; a missing one that reads
/// back as <c>(0, 0, 0)</c> looks exactly like a real place, and puts a returning player at the
/// centre of the star system with nothing to say why.
/// </remarks>
[Collection(SharedDatabase.Name)]
public sealed class WhereaboutsServiceTests(DatabaseFixture fixture) : IAsyncLifetime
{
    private readonly DatabaseFixture _fixture = fixture;

    private int _characterId;

    public async Task InitializeAsync()
    {
        await _fixture.ResetAsync();
        await SeedAsync();
    }

    public Task DisposeAsync() => Task.CompletedTask;

    [Fact]
    public async Task Where_they_were_is_where_they_come_back()
    {
        await using (SpaceMmoDbContext context = _fixture.CreateContext())
        {
            await new WhereaboutsService(context).RecordAsync(
                _characterId, new CharacterWhereabouts(12.5, -640.25, 3.125, Flying: true));
        }

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        CharacterWhereabouts? where =
            await new WhereaboutsService(verify).LastKnownAsync(_characterId);

        Assert.NotNull(where);
        Assert.Equal(12.5, where.X);
        Assert.Equal(-640.25, where.Y);
        Assert.Equal(3.125, where.Z);

        // The flag is the half that decides which pawn they wake up in, and it is the half a
        // round-trip test can pass without noticing: storing the position and dropping this would
        // look like a working restore right up until somebody quit in orbit and woke up falling.
        Assert.True(where.Flying);
    }

    [Fact]
    public async Task A_character_who_has_never_been_anywhere_is_nowhere()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        // Not the origin. A new character has no position at all, and the difference between "no
        // answer" and "the middle of the star system" is the difference between a starting point
        // and a teleport into empty space.
        Assert.Null(await new WhereaboutsService(context).LastKnownAsync(_characterId));
    }

    [Fact]
    public async Task Moving_replaces_where_they_were()
    {
        await using (SpaceMmoDbContext context = _fixture.CreateContext())
        {
            await new WhereaboutsService(context).RecordAsync(
                _characterId, new CharacterWhereabouts(1.0, 2.0, 3.0, Flying: true));
        }

        await using (SpaceMmoDbContext context = _fixture.CreateContext())
        {
            await new WhereaboutsService(context).RecordAsync(
                _characterId, new CharacterWhereabouts(4.0, 5.0, 6.0, Flying: false));
        }

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        CharacterWhereabouts? where =
            await new WhereaboutsService(verify).LastKnownAsync(_characterId);

        Assert.NotNull(where);
        Assert.Equal(4.0, where.X);

        // Landing has to clear the flag as well as move the position. A record that only ever
        // turned flying on would put a parked player back into a ship they had climbed out of.
        Assert.False(where.Flying);
    }

    [Theory]
    [InlineData(double.NaN, 0.0, 0.0)]
    [InlineData(0.0, double.PositiveInfinity, 0.0)]
    [InlineData(0.0, 0.0, double.NegativeInfinity)]
    public async Task A_position_that_is_not_a_position_is_refused(double x, double y, double z)
    {
        await using (SpaceMmoDbContext context = _fixture.CreateContext())
        {
            await new WhereaboutsService(context).RecordAsync(
                _characterId, new CharacterWhereabouts(7.0, 8.0, 9.0, Flying: false));
        }

        await using (SpaceMmoDbContext context = _fixture.CreateContext())
        {
            await Assert.ThrowsAsync<ImpossiblePositionException>(
                () => new WhereaboutsService(context).RecordAsync(
                    _characterId, new CharacterWhereabouts(x, y, z, Flying: false)));
        }

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        // The good position survives, which is the point of refusing rather than storing. A NaN
        // written over a real place cannot be recovered, and the player signing in next is the
        // one who finds out.
        CharacterWhereabouts? where =
            await new WhereaboutsService(verify).LastKnownAsync(_characterId);

        Assert.NotNull(where);
        Assert.Equal(7.0, where.X);
    }

    [Fact]
    public async Task Half_a_position_is_no_position()
    {
        // Written past the service, because the service cannot produce this and a database can: a
        // column added by a migration, a partial write, a hand-edited row. Two coordinates and an
        // implied zero is a point in space, and it looks exactly like a real one.
        await using (SpaceMmoDbContext context = _fixture.CreateContext())
        {
            Character character = await context.Characters.FindAsync(_characterId)
                ?? throw new InvalidOperationException("seed failed");

            character.LastSystemX = 5.0;
            character.LastSystemY = 6.0;
            character.LastSystemZ = null;

            await context.SaveChangesAsync();
        }

        await using SpaceMmoDbContext verify = _fixture.CreateContext();

        Assert.Null(await new WhereaboutsService(verify).LastKnownAsync(_characterId));
    }

    [Fact]
    public async Task A_character_that_does_not_exist_is_refused()
    {
        await using SpaceMmoDbContext context = _fixture.CreateContext();

        await Assert.ThrowsAsync<UnknownCharacterException>(
            () => new WhereaboutsService(context).RecordAsync(
                999_999, new CharacterWhereabouts(1.0, 1.0, 1.0, Flying: false)));
    }

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

        var account = new Account
        {
            Email = "wanderer@local.test",
            PasswordHash = "x",
            CreatedAt = DateTimeOffset.UtcNow,
        };

        context.Accounts.Add(account);
        await context.SaveChangesAsync();

        var character = new Character
        {
            AccountId = account.Id,
            Name = "Wanderer",
            Race = Race.Humanoid,
            HomeBodyId = body.Id,
            Balance = Credits.FromWholeCredits(100),
            CreatedAt = DateTimeOffset.UtcNow,
        };

        context.Characters.Add(character);
        await context.SaveChangesAsync();

        _characterId = character.Id;
    }
}
