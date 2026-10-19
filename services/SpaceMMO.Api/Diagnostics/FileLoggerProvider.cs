using System.Globalization;

namespace SpaceMMO.Api.Diagnostics;

/// <summary>
/// The API's log as a file, beside the game's, stamped the way Unreal stamps its own (task 198).
/// </summary>
/// <remarks>
/// <para>
/// <strong>The console is a window nobody can grep.</strong> The question task 198 had to answer -- what the API
/// was doing while the game server waited seconds for a hit -- needs the API's account of a request next to the
/// server's, and the server's is a file. Lines are stamped in UTC as <c>[2026.10.19-01.34.37:584]</c>, the format
/// of <c>client/Saved/Logs</c>, so the two subtract by eye.
/// </para>
/// <para>
/// Written only when <c>Logging:File:Path</c> is set (<c>scripts/api.bat</c> sets it), filtered by
/// <c>Logging:File:LogLevel</c> like any other provider. A file left by the previous run is kept, renamed with the
/// time it was last written, as Unreal keeps its own.
/// </para>
/// </remarks>
[ProviderAlias("File")]
public sealed class FileLoggerProvider : ILoggerProvider
{
    private readonly Lock _gate = new();
    private readonly StreamWriter? _writer;

    /// <remarks>
    /// Built by the logging system from the finished configuration, not in <c>Program.cs</c> as the builder starts,
    /// so a test's configuration is the one it reads, and a test run never writes into the playtest's log.
    /// </remarks>
    public FileLoggerProvider(IConfiguration configuration, IHostEnvironment environment)
    {
        ArgumentNullException.ThrowIfNull(configuration);
        ArgumentNullException.ThrowIfNull(environment);

        if (configuration["Logging:File:Path"] is not { Length: > 0 } configured)
        {
            return;
        }

        string path = System.IO.Path.GetFullPath(configured, environment.ContentRootPath);

        Path = path;
        Directory.CreateDirectory(System.IO.Path.GetDirectoryName(path)!);
        KeepPrevious(path);

        // Shared for reading, so the file can be read while the API runs; flushed every line, so what is read is
        // what has happened.
        _writer = new StreamWriter(new FileStream(path, FileMode.Append, FileAccess.Write, FileShare.ReadWrite))
        {
            AutoFlush = true,
        };

        // Each run marks its start, so a file two runs have appended to still says which line is whose.
        Write(LogLevel.Information, "SpaceMMO.Api", $"Logging from process {Environment.ProcessId}.", null);
        Console.WriteLine($"Logging to {path} as well.");
    }

    /// <summary>Where the log is written, or null if nowhere is configured.</summary>
    public string? Path { get; }

    public ILogger CreateLogger(string categoryName) => new FileLogger(this, categoryName);

    public void Dispose()
    {
        lock (_gate)
        {
            _writer?.Dispose();
        }
    }

    private void Write(LogLevel level, string category, string message, Exception? exception)
    {
        string stamp = DateTime.UtcNow.ToString("[yyyy.MM.dd-HH.mm.ss:fff]", CultureInfo.InvariantCulture);

        lock (_gate)
        {
            _writer?.WriteLine($"{stamp} {level} {category}: {message}");

            if (exception is not null)
            {
                _writer?.WriteLine(exception.ToString());
            }
        }
    }

    private static void KeepPrevious(string path)
    {
        if (!File.Exists(path))
        {
            return;
        }

        string written = File.GetLastWriteTimeUtc(path).ToString("yyyy.MM.dd-HH.mm.ss", CultureInfo.InvariantCulture);
        string kept = System.IO.Path.Combine(
            System.IO.Path.GetDirectoryName(path)!,
            $"{System.IO.Path.GetFileNameWithoutExtension(path)}-backup-{written}{System.IO.Path.GetExtension(path)}");

        try
        {
            File.Move(path, kept, overwrite: true);
        }
        catch (IOException)
        {
            // Another API has it open: append to it instead, which interleaves but loses nothing.
        }
    }

    private sealed class FileLogger(FileLoggerProvider provider, string category) : ILogger
    {
        public IDisposable? BeginScope<TState>(TState state)
            where TState : notnull => null;

        public bool IsEnabled(LogLevel logLevel) => logLevel != LogLevel.None && provider._writer is not null;

        public void Log<TState>(
            LogLevel logLevel, EventId eventId, TState state, Exception? exception,
            Func<TState, Exception?, string> formatter)
        {
            ArgumentNullException.ThrowIfNull(formatter);

            if (IsEnabled(logLevel))
            {
                provider.Write(logLevel, category, formatter(state, exception), exception);
            }
        }
    }
}
