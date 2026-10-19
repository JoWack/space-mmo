using System.Text.Json;
using Npgsql;
using Xunit;

namespace SpaceMMO.Api.Tests;

/// <summary>
/// The API reaches Postgres by address (task 198).
/// </summary>
/// <remarks>
/// Docker publishes Postgres on <c>127.0.0.1</c> only. <c>localhost</c> resolves to <c>::1</c> first, and Windows
/// takes 2–4 s to give up on the refused IPv6 attempt: every new connection paid it, and a fight's hits open them in
/// bursts. Read from the copy built beside the tests, which is the file the API is shipped with.
/// </remarks>
public sealed class ConnectionStringTests
{
    [Fact]
    public void The_API_connects_to_Postgres_by_address_not_by_localhost()
    {
        using JsonDocument settings = JsonDocument.Parse(
            File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "appsettings.json")));

        string connection = settings.RootElement
            .GetProperty("ConnectionStrings")
            .GetProperty("SpaceMmo")
            .GetString()!;

        Assert.Equal("127.0.0.1", new NpgsqlConnectionStringBuilder(connection).Host);
    }
}
