using Microsoft.EntityFrameworkCore;
using Microsoft.EntityFrameworkCore.Design;

namespace SpaceMMO.Data;

/// <summary>
/// Builds a context for <c>dotnet ef</c> at design time.
/// </summary>
/// <remarks>
/// Migration tooling has to construct a context without an application host. This factory
/// exists purely for that, and is never used at runtime — the API configures its own context
/// through dependency injection.
/// </remarks>
public class DesignTimeDbContextFactory : IDesignTimeDbContextFactory<SpaceMmoDbContext>
{
    /// <summary>
    /// Environment variable holding the design-time connection string.
    /// </summary>
    public const string ConnectionStringVariable = "SPACEMMO_DB";

    /// <summary>
    /// Matches the defaults in <c>infra/.env.example</c>, so the tooling works against the
    /// local compose stack with no extra configuration.
    /// </summary>
    private const string LocalDevelopmentConnectionString =
        "Host=localhost;Port=5432;Database=spacemmo;Username=spacemmo;Password=devonly";

    public SpaceMmoDbContext CreateDbContext(string[] args)
    {
        string connectionString =
            Environment.GetEnvironmentVariable(ConnectionStringVariable)
            ?? LocalDevelopmentConnectionString;

        DbContextOptions<SpaceMmoDbContext> options =
            new DbContextOptionsBuilder<SpaceMmoDbContext>()
                .UseNpgsql(connectionString)
                .UseSnakeCaseNamingConvention()
                .Options;

        return new SpaceMmoDbContext(options);
    }
}
