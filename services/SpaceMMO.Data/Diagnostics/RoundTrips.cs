using System.Data.Common;
using Microsoft.EntityFrameworkCore.Diagnostics;

namespace SpaceMMO.Data.Diagnostics;

/// <summary>
/// Every round trip one piece of work made to Postgres, and how long each took (task 198).
/// </summary>
/// <remarks>
/// <para>
/// <strong>Why it exists.</strong> A hit took 17–24 ms with the game closed and 0.6–11 s while two clients fought,
/// and nothing said whether that was a lock queue, each statement slowing down, or the API itself. A tally open
/// around the work answers it: the statements' own times, the connection's, and whatever is left over, which is
/// time the API spent between them.
/// </para>
/// <para>
/// Filled by <see cref="RoundTripInterceptors"/>, which every <see cref="SpaceMmoDbContext"/> carries, into
/// whichever tally is open on the current async flow. With none open, recording is a null check.
/// </para>
/// </remarks>
public sealed class RoundTripTally : IDisposable
{
    private static readonly AsyncLocal<RoundTripTally?> Open = new();

    private readonly RoundTripTally? _outer;
    private readonly List<TimeSpan> _roundTrips = [];

    private RoundTripTally(RoundTripTally? outer) => _outer = outer;

    /// <summary>Each statement and commit sent, in order, as long as it took to answer.</summary>
    public IReadOnlyList<TimeSpan> RoundTrips => _roundTrips;

    /// <summary>Opening the connection: from the pool, or a new one, which is several round trips of its own.</summary>
    public TimeSpan Connecting { get; private set; }

    /// <summary>Starts counting on this async flow until disposed.</summary>
    public static RoundTripTally Begin()
    {
        var tally = new RoundTripTally(Open.Value);
        Open.Value = tally;

        return tally;
    }

    public void Dispose() => Open.Value = _outer;

    internal static void RecordRoundTrip(TimeSpan duration) => Open.Value?._roundTrips.Add(duration);

    internal static void RecordConnecting(TimeSpan duration)
    {
        if (Open.Value is { } tally)
        {
            tally.Connecting += duration;
        }
    }
}

/// <summary>
/// The interceptors that fill <see cref="RoundTripTally"/>: statements, commits and connections.
/// </summary>
/// <remarks>
/// A statement's time is EF's own measure, from sending it to the first of its answer arriving, so a statement
/// that waits for a row lock counts the wait. Npgsql sends <c>BEGIN</c> with the first statement rather than on its
/// own, so a transaction's opening is not a round trip; its commit is.
/// </remarks>
public static class RoundTripInterceptors
{
    public static IReadOnlyList<IInterceptor> All { get; } =
        [new Statements(), new Commits(), new Connections()];

    private sealed class Statements : DbCommandInterceptor
    {
        public override DbDataReader ReaderExecuted(
            DbCommand command, CommandExecutedEventData eventData, DbDataReader result)
        {
            RoundTripTally.RecordRoundTrip(eventData.Duration);

            return result;
        }

        public override ValueTask<DbDataReader> ReaderExecutedAsync(
            DbCommand command, CommandExecutedEventData eventData, DbDataReader result,
            CancellationToken cancellationToken = default)
        {
            RoundTripTally.RecordRoundTrip(eventData.Duration);

            return ValueTask.FromResult(result);
        }

        public override int NonQueryExecuted(DbCommand command, CommandExecutedEventData eventData, int result)
        {
            RoundTripTally.RecordRoundTrip(eventData.Duration);

            return result;
        }

        public override ValueTask<int> NonQueryExecutedAsync(
            DbCommand command, CommandExecutedEventData eventData, int result,
            CancellationToken cancellationToken = default)
        {
            RoundTripTally.RecordRoundTrip(eventData.Duration);

            return ValueTask.FromResult(result);
        }

        public override object? ScalarExecuted(DbCommand command, CommandExecutedEventData eventData, object? result)
        {
            RoundTripTally.RecordRoundTrip(eventData.Duration);

            return result;
        }

        public override ValueTask<object?> ScalarExecutedAsync(
            DbCommand command, CommandExecutedEventData eventData, object? result,
            CancellationToken cancellationToken = default)
        {
            RoundTripTally.RecordRoundTrip(eventData.Duration);

            return ValueTask.FromResult(result);
        }

        public override void CommandFailed(DbCommand command, CommandErrorEventData eventData) =>
            RoundTripTally.RecordRoundTrip(eventData.Duration);

        public override Task CommandFailedAsync(
            DbCommand command, CommandErrorEventData eventData, CancellationToken cancellationToken = default)
        {
            RoundTripTally.RecordRoundTrip(eventData.Duration);

            return Task.CompletedTask;
        }
    }

    private sealed class Commits : DbTransactionInterceptor
    {
        public override void TransactionCommitted(DbTransaction transaction, TransactionEndEventData eventData) =>
            RoundTripTally.RecordRoundTrip(eventData.Duration);

        public override Task TransactionCommittedAsync(
            DbTransaction transaction, TransactionEndEventData eventData, CancellationToken cancellationToken = default)
        {
            RoundTripTally.RecordRoundTrip(eventData.Duration);

            return Task.CompletedTask;
        }
    }

    private sealed class Connections : DbConnectionInterceptor
    {
        public override void ConnectionOpened(DbConnection connection, ConnectionEndEventData eventData) =>
            RoundTripTally.RecordConnecting(eventData.Duration);

        public override Task ConnectionOpenedAsync(
            DbConnection connection, ConnectionEndEventData eventData, CancellationToken cancellationToken = default)
        {
            RoundTripTally.RecordConnecting(eventData.Duration);

            return Task.CompletedTask;
        }
    }
}
