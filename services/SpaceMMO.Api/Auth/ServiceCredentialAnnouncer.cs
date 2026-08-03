namespace SpaceMMO.Api.Auth;

/// <summary>
/// Says at startup whether the game server's credential is configured, and which one it is.
/// </summary>
/// <remarks>
/// Purely diagnostic, and worth its own type anyway. A stale API process — one started before the
/// service credential existed — serves every older endpoint correctly while refusing every service
/// call with a bare 401, which looks exactly like a mismatched secret and sends you off checking
/// files that were right all along. The presence or absence of this one log line separates those
/// two cases immediately.
///
/// The fingerprint format matches the Unreal client's, so the two logs can be compared by eye.
/// </remarks>
public sealed class ServiceCredentialAnnouncer(
    ServiceCredential credential, ILogger<ServiceCredentialAnnouncer> logger) : IHostedService
{
    private static readonly Action<ILogger, string, Exception?> LogConfigured =
        LoggerMessage.Define<string>(
            LogLevel.Information,
            new EventId(1, nameof(LogConfigured)),
            "Service credential configured (fingerprint {Fingerprint}).");

    private static readonly Action<ILogger, Exception?> LogAbsent =
        LoggerMessage.Define(
            LogLevel.Warning,
            new EventId(2, nameof(LogAbsent)),
            "No service credential configured; the game server cannot gather. "
            + "Run scripts/init-secrets.ps1.");

    public Task StartAsync(CancellationToken cancellationToken)
    {
        if (credential.IsConfigured)
        {
            LogConfigured(logger, credential.ConfiguredFingerprint, null);
        }
        else
        {
            LogAbsent(logger, null);
        }

        return Task.CompletedTask;
    }

    public Task StopAsync(CancellationToken cancellationToken) => Task.CompletedTask;
}
