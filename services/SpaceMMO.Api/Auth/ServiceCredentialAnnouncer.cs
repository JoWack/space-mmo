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
    ServiceCredential credential,
    IHostEnvironment environment,
    ILogger<ServiceCredentialAnnouncer> logger) : IHostedService
{
    private static readonly Action<ILogger, string, string, string, Exception?> LogConfigured =
        LoggerMessage.Define<string, string, string>(
            LogLevel.Information,
            new EventId(1, nameof(LogConfigured)),
            "Service credential configured from {Source} (fingerprint {Fingerprint}); "
            + "environment {Environment}.");

    private static readonly Action<ILogger, string, Exception?> LogAbsent =
        LoggerMessage.Define<string>(
            LogLevel.Warning,
            new EventId(2, nameof(LogAbsent)),
            "No service credential configured; the game server cannot gather. Environment is "
            + "{Environment}. Run scripts/init-secrets.ps1.");

    public Task StartAsync(CancellationToken cancellationToken)
    {
        // The environment name is logged either way, because it decides whether user secrets are
        // loaded at all — and a host that is not in Development silently has none, which is
        // otherwise invisible.
        if (credential.IsConfigured)
        {
            LogConfigured(
                logger,
                credential.Source,
                credential.ConfiguredFingerprint,
                environment.EnvironmentName,
                null);
        }
        else
        {
            LogAbsent(logger, environment.EnvironmentName, null);
        }

        return Task.CompletedTask;
    }

    public Task StopAsync(CancellationToken cancellationToken) => Task.CompletedTask;
}
