using Microsoft.EntityFrameworkCore;
using Npgsql;
using Xunit;

namespace SpaceMMO.Data.Tests;

/// <summary>
/// Creates and migrates a dedicated test database, shared across the whole test run.
/// </summary>
/// <remarks>
/// <para>
/// A <em>separate</em> database from the development one, so running the suite never destroys
/// whatever you were poking at by hand in <c>psql</c>.
/// </para>
/// <para>
/// If Postgres is unreachable these tests fail rather than silently skipping. A skipped test
/// that reports success is worse than a loud failure, and the error message says exactly what
/// to start.
/// </para>
/// </remarks>
public sealed class DatabaseFixture : IAsyncLifetime
{
    private const string TestDatabaseName = "spacemmo_test";

    private const string AdminConnectionString =
        "Host=localhost;Port=5432;Database=postgres;Username=spacemmo;Password=devonly";

    public string ConnectionString { get; } =
        $"Host=localhost;Port=5432;Database={TestDatabaseName};Username=spacemmo;Password=devonly";

    public async Task InitializeAsync()
    {
        await EnsureDatabaseExistsAsync();

        await using SpaceMmoDbContext context = CreateContext();
        await context.Database.MigrateAsync();
    }

    public Task DisposeAsync() => Task.CompletedTask;

    /// <summary>
    /// Builds a context on its own connection.
    /// </summary>
    /// <remarks>
    /// Concurrency tests need genuinely separate connections — two contexts sharing one would
    /// serialise on the client side and prove nothing about database locking.
    /// </remarks>
    public SpaceMmoDbContext CreateContext()
    {
        DbContextOptions<SpaceMmoDbContext> options =
            new DbContextOptionsBuilder<SpaceMmoDbContext>()
                .UseNpgsql(ConnectionString)
                .UseSnakeCaseNamingConvention()
                .Options;

        return new SpaceMmoDbContext(options);
    }

    /// <summary>
    /// Empties every table, so each test starts from a known state.
    /// </summary>
    /// <remarks>
    /// One <c>TRUNCATE</c> across all tables, which sidesteps foreign-key ordering entirely
    /// and is dramatically faster than deleting rows. Identities restart so tests can rely on
    /// predictable ids.
    /// </remarks>
    public async Task ResetAsync()
    {
        await using SpaceMmoDbContext context = CreateContext();

        var tables = new List<string>();

        await using (NpgsqlConnection connection = new(ConnectionString))
        {
            await connection.OpenAsync();

            await using NpgsqlCommand command = new(
                """
                SELECT tablename FROM pg_tables
                WHERE schemaname = 'public' AND tablename <> '__EFMigrationsHistory'
                """,
                connection);

            await using NpgsqlDataReader reader = await command.ExecuteReaderAsync();

            while (await reader.ReadAsync())
            {
                tables.Add($"public.\"{reader.GetString(0)}\"");
            }
        }

        if (tables.Count == 0)
        {
            return;
        }

        // EF1002 warns about interpolation into raw SQL, and is normally right. Here the
        // identifiers come from pg_tables in this same database and are quoted on the way out
        // of the reader — no external input reaches this string, and table names cannot be
        // parameterised in any case.
#pragma warning disable EF1002
        await context.Database.ExecuteSqlRawAsync(
            $"TRUNCATE {string.Join(", ", tables)} RESTART IDENTITY CASCADE");
#pragma warning restore EF1002
    }

    private static async Task EnsureDatabaseExistsAsync()
    {
        try
        {
            await using NpgsqlConnection connection = new(AdminConnectionString);
            await connection.OpenAsync();

            await using NpgsqlCommand exists = new(
                "SELECT 1 FROM pg_database WHERE datname = @name", connection);
            exists.Parameters.AddWithValue("name", TestDatabaseName);

            if (await exists.ExecuteScalarAsync() is not null)
            {
                return;
            }

            // CREATE DATABASE cannot be parameterised, and the name is a compile-time constant.
            await using NpgsqlCommand create = new(
                $"CREATE DATABASE \"{TestDatabaseName}\" TEMPLATE template0 LC_COLLATE 'C' LC_CTYPE 'C'",
                connection);

            await create.ExecuteNonQueryAsync();
        }
        catch (NpgsqlException error)
        {
            throw new InvalidOperationException(
                "Could not reach Postgres. These are integration tests and need the local "
                + "container running:\n\n"
                + "    docker compose -f infra/docker-compose.yml up -d\n\n"
                + "See docs/setup.md if docker is not on your PATH.",
                error);
        }
    }
}

/// <summary>
/// Shares one <see cref="DatabaseFixture"/> across every test class in the collection, so the
/// database is created and migrated once per run rather than once per class.
/// </summary>
/// <remarks>
/// Named <c>SharedDatabase</c> rather than the conventional <c>DatabaseCollection</c> because
/// CA1711 reserves the <c>Collection</c> suffix for actual collection types.
/// </remarks>
[CollectionDefinition(Name)]
public sealed class SharedDatabase : ICollectionFixture<DatabaseFixture>
{
    public const string Name = "database";
}
