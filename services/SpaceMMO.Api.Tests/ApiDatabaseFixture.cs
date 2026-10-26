using Microsoft.EntityFrameworkCore;
using Npgsql;
using SpaceMMO.Data;
using Xunit;

namespace SpaceMMO.Api.Tests;

/// <summary>
/// Creates and migrates a test database for the API suite.
/// </summary>
/// <remarks>
/// <para>
/// Deliberately <em>not</em> SpaceMMO.Data.Tests' fixture, for two reasons. xUnit requires a
/// collection definition to live in the assembly that uses it, so the collection had to be local
/// regardless. More importantly it uses a <strong>different database</strong>: the two test
/// assemblies can run at the same time, and both fixtures drop and recreate their database on
/// startup, so sharing a name would mean one suite deleting the other's schema mid-run.
/// </para>
/// <para>
/// If Postgres is unreachable these fail loudly rather than skipping. A skipped test that reports
/// success is worse than a failure that says what to start.
/// </para>
/// </remarks>
public sealed class ApiDatabaseFixture : IAsyncLifetime
{
    private const string TestDatabaseName = "spacemmo_api_test";

    // 127.0.0.1, not localhost: infra/docker-compose.yml says why.
    private const string AdminConnectionString =
        "Host=127.0.0.1;Port=5432;Database=postgres;Username=spacemmo;Password=devonly";

    public string ConnectionString { get; } =
        $"Host=127.0.0.1;Port=5432;Database={TestDatabaseName};Username=spacemmo;Password=devonly";

    public async Task InitializeAsync()
    {
        await RecreateDatabaseAsync();

        await using SpaceMmoDbContext context = CreateContext();
        await context.Database.MigrateAsync();
    }

    public Task DisposeAsync() => Task.CompletedTask;

    public SpaceMmoDbContext CreateContext()
    {
        DbContextOptions<SpaceMmoDbContext> options =
            new DbContextOptionsBuilder<SpaceMmoDbContext>()
                .UseNpgsql(ConnectionString)
                .UseSnakeCaseNamingConvention()
                .Options;

        return new SpaceMmoDbContext(options);
    }

    /// <summary>Empties every table so each test starts from a known state.</summary>
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

        // EF1002 warns about interpolation into raw SQL and is normally right. These identifiers
        // come from pg_tables in this same database and are quoted on the way out of the reader;
        // no external input reaches the string, and table names cannot be parameterised anyway.
#pragma warning disable EF1002
        await context.Database.ExecuteSqlRawAsync(
            $"TRUNCATE {string.Join(", ", tables)} RESTART IDENTITY CASCADE");
#pragma warning restore EF1002
    }

    private static async Task RecreateDatabaseAsync()
    {
        try
        {
            NpgsqlConnection.ClearAllPools();

            await using NpgsqlConnection connection = new(AdminConnectionString);
            await connection.OpenAsync();

            // Five minutes, not Npgsql's thirty seconds. On Windows a DROP DATABASE forces an immediate checkpoint,
            // and after a full Data run that checkpoint syncs every file the run's truncations touched: 53,886 files
            // and 35 s on 25 October, which failed every test in the collection as "could not reach Postgres".
            await using (NpgsqlCommand drop = new(
                $"DROP DATABASE IF EXISTS \"{TestDatabaseName}\" WITH (FORCE)", connection) { CommandTimeout = 300 })
            {
                await drop.ExecuteNonQueryAsync();
            }

            await using NpgsqlCommand create = new(
                $"CREATE DATABASE \"{TestDatabaseName}\" TEMPLATE template0 LC_COLLATE 'C' LC_CTYPE 'C'",
                connection);

            await create.ExecuteNonQueryAsync();
        }
        catch (NpgsqlException error)
        {
            // What went wrong, first: this said "could not reach Postgres" for a timeout on a server that was up.
            throw new InvalidOperationException(
                $"Could not recreate the test database: {error.Message} These are integration tests and need the "
                + "local Postgres on 127.0.0.1:5432; docs/setup.md §1 says how to start and check it.",
                error);
        }
    }
}

/// <summary>
/// Shares one <see cref="ApiDatabaseFixture"/> across every test class in this assembly.
/// </summary>
[CollectionDefinition(Name)]
public sealed class SharedApiDatabase : ICollectionFixture<ApiDatabaseFixture>
{
    public const string Name = "api-database";
}
