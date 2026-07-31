using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Mvc.Testing;
using Microsoft.Extensions.Configuration;

namespace SpaceMMO.Api.Tests;

/// <summary>
/// Boots the real application against the test database.
/// </summary>
/// <remarks>
/// Nothing is replaced except configuration. The point of these tests is that the actual
/// endpoints, the actual authorization checks and the actual EF transactions agree with each
/// other, and every substitution made here would be a place that agreement stops being tested.
/// </remarks>
public sealed class ApiFactory(string connectionString) : WebApplicationFactory<Program>
{
    /// <summary>
    /// A signing key for tests only. Long enough to satisfy the 32-byte minimum, and obviously
    /// not a secret — the real one comes from user secrets or the environment.
    /// </summary>
    private const string TestSigningKey = "test-signing-key-that-is-long-enough-to-pass-validation";

    private readonly string _connectionString = connectionString;

    protected override void ConfigureWebHost(IWebHostBuilder builder)
    {
        builder.UseEnvironment("Development");

        builder.ConfigureAppConfiguration((_, configuration) =>
            configuration.AddInMemoryCollection(new Dictionary<string, string?>
            {
                ["ConnectionStrings:SpaceMmo"] = _connectionString,
                ["Auth:SigningKey"] = TestSigningKey,
            }));
    }
}
