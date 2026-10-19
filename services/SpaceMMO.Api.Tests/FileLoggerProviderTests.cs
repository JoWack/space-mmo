using System.Text.RegularExpressions;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.FileProviders;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using SpaceMMO.Api.Diagnostics;
using Xunit;

namespace SpaceMMO.Api.Tests;

/// <summary>
/// The API's log file (task 198): read beside the game's logs, so it has to be stamped the way they are and must not
/// lose the run before it.
/// </summary>
public sealed class FileLoggerProviderTests : IDisposable
{
    private static readonly Action<ILogger, string, Exception?> Say =
        LoggerMessage.Define<string>(LogLevel.Information, new EventId(1, nameof(Say)), "{Line}");

    private readonly string _folder = Path.Combine(Path.GetTempPath(), $"spacemmo-log-{Guid.NewGuid():N}");

    public void Dispose() => Directory.Delete(_folder, recursive: true);

    [Fact]
    public void Lines_are_stamped_in_UTC_as_Unreal_stamps_them()
    {
        DateTime before = DateTime.UtcNow;

        using (FileLoggerProvider provider = Provider("Api.log"))
        {
            Say(provider.CreateLogger("SpaceMMO.Combat"), "Hit by 1 on 2, landed.", null);
        }

        string line = File.ReadAllLines(Path.Combine(_folder, "Api.log"))[^1];
        Match stamp = Regex.Match(line, @"^\[(?<at>\d{4}\.\d{2}\.\d{2}-\d{2}\.\d{2}\.\d{2}:\d{3})\] ");

        Assert.True(stamp.Success, line);
        Assert.EndsWith("] Information SpaceMMO.Combat: Hit by 1 on 2, landed.", line, StringComparison.Ordinal);

        // UTC, as client/Saved/Logs is: read as local time on a machine west of Greenwich, it would be hours out.
        DateTime at = DateTime.ParseExact(
            stamp.Groups["at"].Value, "yyyy.MM.dd-HH.mm.ss:fff", System.Globalization.CultureInfo.InvariantCulture);

        Assert.InRange(at, before.AddSeconds(-1), DateTime.UtcNow.AddSeconds(1));
    }

    [Fact]
    public void The_last_run_is_kept_beside_the_new_one()
    {
        using (FileLoggerProvider first = Provider("Api.log"))
        {
            Say(first.CreateLogger("SpaceMMO.Combat"), "The first run.", null);
        }

        using (FileLoggerProvider second = Provider("Api.log"))
        {
            Say(second.CreateLogger("SpaceMMO.Combat"), "The second run.", null);
        }

        string kept = Assert.Single(Directory.GetFiles(_folder, "Api-backup-*.log"));

        Assert.Contains("The first run.", File.ReadAllText(kept), StringComparison.Ordinal);
        Assert.DoesNotContain("The first run.", File.ReadAllText(Path.Combine(_folder, "Api.log")), StringComparison.Ordinal);
    }

    [Fact]
    public void With_no_path_nothing_is_written()
    {
        using var provider = new FileLoggerProvider(
            new ConfigurationBuilder().Build(), new ContentRoot(_folder));

        Directory.CreateDirectory(_folder);

        Assert.Null(provider.Path);
        Assert.False(provider.CreateLogger("SpaceMMO.Combat").IsEnabled(LogLevel.Critical));
    }

    private FileLoggerProvider Provider(string name) =>
        new(
            new ConfigurationBuilder()
                .AddInMemoryCollection(new Dictionary<string, string?> { ["Logging:File:Path"] = name })
                .Build(),
            new ContentRoot(_folder));

    /// <summary>A content root, which a relative path is taken from.</summary>
    private sealed class ContentRoot(string root) : IHostEnvironment
    {
        public string EnvironmentName { get; set; } = "Development";

        public string ApplicationName { get; set; } = "SpaceMMO.Api";

        public string ContentRootPath { get; set; } = root;

        public IFileProvider ContentRootFileProvider { get; set; } = new NullFileProvider();
    }
}
