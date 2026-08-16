using SpaceMMO.Api.Auth;
using SpaceMMO.Data.Gathering;
using SpaceMMO.Data.Industry;

namespace SpaceMMO.Api.Endpoints;

/// <remarks>
/// No station. Gathered material goes into the character's own hands (ADR-0012), so there is nothing
/// to choose — and asking for one meant the client invented a fixed answer, which put every player's
/// ore in station 1 whatever planet they were standing on.
/// </remarks>
public sealed record GatherRequest(int CharacterId, long ResourceNodeId);

/// <summary>
/// Resource extraction — the only place material enters the economy.
/// </summary>
/// <remarks>
/// The request carries no quantity, and that is the whole design. The client asks to gather; the
/// server works out how much wall-clock time has elapsed since this character last did and grants
/// only what that entitles them to. A client calling in a tight loop extracts exactly as much as
/// one calling at the tick interval.
/// </remarks>
public static class GatheringEndpoints
{
    /// <summary>Pre-compiled, because the analyzers require it and this sits on a request path.</summary>
    private static readonly Action<ILogger, string, int, string, Exception?> LogRefusedServiceCall =
        LoggerMessage.Define<string, int, string>(
            LogLevel.Warning,
            new EventId(1, nameof(LogRefusedServiceCall)),
            "Service call refused: presented fingerprint {Presented} ({Length} chars), "
            + "configured {Configured}.");

    public static void MapGatheringEndpoints(this IEndpointRouteBuilder routes)
    {
        RouteGroupBuilder group = routes.MapGroup("/gathering").WithTags("Gathering");

        group.MapPost("/gather", GatherAsync);
    }

    private static async Task<IResult> GatherAsync(
        GatherRequest request,
        HttpContext context,
        Caller caller,
        GatheringService gathering,
        ServiceCredential service,
        ILoggerFactory loggerFactory,
        CancellationToken cancellation)
    {
        OwnershipResult owned =
            await caller.ServiceOrOwnedCharacterAsync(context, request.CharacterId, cancellation);

        if (owned.Status != OwnershipStatus.Owned)
        {
            // A rejected service call is worth explaining. "Header absent" and "header present but
            // holding a different value" both surface as a bare 401, and telling them apart from
            // the outside is guesswork — which is precisely what made this expensive to chase.
            if (owned.Status == OwnershipStatus.Unauthenticated
                && context.Request.Headers.TryGetValue(ServiceCredential.HeaderName, out var presented))
            {
                LogRefusedServiceCall(
                    loggerFactory.CreateLogger("SpaceMMO.Auth"),
                    ServiceCredential.Fingerprint(presented.ToString()),
                    presented.ToString().Length,
                    service.ConfiguredFingerprint,
                    null);
            }

            return owned.ToProblem();
        }

        // Every refusal below was previously an unhandled exception, and so a 500. Mining a deposit
        // above your level is an ordinary thing for a player to try — it is what the level gate is
        // for — and answering it with a server fault both hides the reason from the player and
        // buries a real fault, if one ever happens, in the noise.
        try
        {
            GatherResult result = await gathering.GatherAsync(
                request.CharacterId, request.ResourceNodeId, cancellation);

            // An empty result is a success, not an error: it means not enough time has passed, or
            // the node is spent. Both are ordinary states a client renders rather than reports.
            return Results.Ok(result);
        }
        catch (UnknownResourceNodeException)
        {
            // No id echoed back. Node ids are guessable integers, and confirming which ones exist
            // is the same oracle the ownership checks are careful not to be.
            return Results.NotFound();
        }
        catch (SkillTooLowException ex)
        {
            return Results.Conflict(new { error = ex.Message, reason = "skill_too_low" });
        }
        catch (MissingToolException ex)
        {
            return Results.Conflict(new { error = ex.Message, reason = "missing_tool" });
        }
    }
}
