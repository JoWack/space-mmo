using Microsoft.EntityFrameworkCore;
using SpaceMMO.Api.Auth;
using SpaceMMO.Api.Endpoints;
using SpaceMMO.Data;
using SpaceMMO.Data.Content;
using SpaceMMO.Data.Docking;
using SpaceMMO.Data.Gathering;
using SpaceMMO.Data.Industry;
using SpaceMMO.Data.Inventories;
using SpaceMMO.Data.Entities;
using SpaceMMO.Data.Market;
using SpaceMMO.Data.Quests;
using SpaceMMO.Domain.Economy;
using SpaceMMO.Domain.Items;

// The HTTP surface over the M1 backend. Thin on purpose: rules live in SpaceMMO.Domain and
// transactions in SpaceMMO.Data, so nothing in this project decides a game outcome.

WebApplicationBuilder builder = WebApplication.CreateBuilder(args);

builder.Services.AddDbContext<SpaceMmoDbContext>(options =>
    options
        .UseNpgsql(
            builder.Configuration.GetConnectionString("SpaceMmo")
            ?? throw new InvalidOperationException(
                "Connection string 'SpaceMmo' is not configured."))
        // The migrations were generated with this convention, so the tables are snake_case.
        // Without it here, EF quotes "Accounts" and Postgres — which folds unquoted identifiers to
        // lower case but honours quoted ones — reports that the relation does not exist.
        .UseSnakeCaseNamingConvention());

// Scoped, matching the DbContext they wrap: these services run multi-statement transactions with
// row locks, and sharing one across requests would interleave two players' transactions.
builder.Services.AddScoped<MarketService>();
builder.Services.AddScoped<FactionOrderService>();
builder.Services.AddScoped<IndustryService>();
builder.Services.AddScoped<GatheringService>();
builder.Services.AddScoped<DockingService>();
builder.Services.AddScoped<QuestService>();
builder.Services.AddScoped<InventoryService>();
builder.Services.AddScoped<Caller>();

// The Unreal dedicated server's own credential. Absent unless configured, and absent means every
// service call is refused — a missing secret must fail closed.
builder.Services.AddSingleton<ServiceCredential>();

// Announced at startup, with a fingerprint that matches the one the Unreal client logs.
//
// This line exists because its absence was itself the bug once: an API left running from before
// this feature existed served every older endpoint perfectly while refusing every service call,
// which is indistinguishable from a wrong secret. If this line is missing from the log, the
// process is stale and needs restarting — no amount of checking the secret will help.
builder.Services.AddHostedService<ServiceCredentialAnnouncer>();

builder.Services.AddSingleton(_ => new SessionTokens(
    builder.Configuration["Auth:SigningKey"]
    ?? throw new InvalidOperationException(
        "Auth:SigningKey is not configured. Set it in user secrets for local development; "
        + "a token signed with a guessable key is an account takeover.")));

builder.Services.AddProblemDetails();

WebApplication app = builder.Build();

// Seeding is an explicit command, not something startup does on its own.
//
//   dotnet run --project services/SpaceMMO.Api -- --seed
//
// A server that migrates and rewrites content every time it boots will eventually do that to a
// production database during an unrelated restart. Making it a separate invocation means applying
// content is always something somebody chose to do.
if (args.Contains("--seed", StringComparer.Ordinal))
{
    await using AsyncServiceScope scope = app.Services.CreateAsyncScope();

    SpaceMmoDbContext database = scope.ServiceProvider.GetRequiredService<SpaceMmoDbContext>();

    await database.Database.MigrateAsync();

    // Walks up from the binary to the repository root. The content lives in the repo, not beside
    // the assembly, so a relative path from the working directory would depend on where it was
    // launched from.
    string contentRoot = Path.Combine(builder.Environment.ContentRootPath, "..", "..", "data");

    var loader = new ContentLoader(database);
    await loader.LoadAsync(Path.GetFullPath(contentRoot));

    Console.WriteLine($"Seeded content from {Path.GetFullPath(contentRoot)}.");

    // Pays the starting stake to characters made before creation paid one.
    //
    // Idempotent by construction: the condition is the absence of a StartingStake ledger entry, not
    // a zero balance, so a character who earned their way to zero is not topped up and nobody is
    // paid twice. That distinction is the whole reason this is keyed on the ledger.
    //
    // Through the ledger and the balance together, exactly as CreateAsync does (ADR-0005). Credits
    // that appear without an entry make the books irreconcilable, and reconciliation is what catches
    // a dupe before players do -- a backfill that quietly broke it would be worse than no backfill.
    List<Character> unpaid = await database.Characters
        .Where(c => !database.LedgerEntries
            .Any(l => l.CharacterId == c.Id && l.Reason == LedgerReason.StartingStake))
        .ToListAsync();

    if (unpaid.Count > 0)
    {
        DateTimeOffset paidAt = DateTimeOffset.UtcNow;

        foreach (Character character in unpaid)
        {
            character.Balance += Economy.StartingStake;

            database.LedgerEntries.Add(new LedgerEntry
            {
                CharacterId = character.Id,
                DeltaCredits = Economy.StartingStake,
                Reason = LedgerReason.StartingStake,
                CreatedAt = paidAt,
            });
        }

        await database.SaveChangesAsync();

        // Named rather than counted, because this moves money and the one thing somebody reading
        // this output wants to know is who it moved to.
        Console.WriteLine(
            $"Paid the {Economy.StartingStake} starting stake to "
            + $"{string.Join(", ", unpaid.Select(c => c.Name))}.");
    }

    // Pockets for characters made before creation granted them, so every character has somewhere
    // to move goods to. Idempotent by construction: it asks for the ones that have none.
    List<int> withoutCarried = await database.Characters
        .Where(c => !database.Inventories
            .Any(i => i.CharacterId == c.Id && i.Kind == InventoryKind.CharacterCarried))
        .Select(c => c.Id)
        .ToListAsync();

    if (withoutCarried.Count > 0)
    {
        var inventories = scope.ServiceProvider.GetRequiredService<InventoryService>();

        foreach (int characterId in withoutCarried)
        {
            await inventories.GetOrCreateCarriedAsync(characterId);
        }

        Console.WriteLine($"Gave {withoutCarried.Count} character(s) a carried inventory.");
    }

    return 0;
}

// Refuse to start against a database that is behind the code.
//
// Not migrating on startup is deliberate, for the reason above. The cost of that choice is that
// somebody eventually adds a migration, restarts the server without seeding, and gets a schema
// the code does not match — at which point every query touching the changed table throws, and the
// player-visible symptom is a 500 on sign-in that reads as "it cannot identify my character".
// That happened, and it cost a session to trace back to a column that was never added.
//
// Saying so and stopping is strictly better than serving errors: the fault is named, the fix is
// printed, and nothing half-works in the meantime.
await using (AsyncServiceScope scope = app.Services.CreateAsyncScope())
{
    SpaceMmoDbContext database = scope.ServiceProvider.GetRequiredService<SpaceMmoDbContext>();

    string[] pending = [.. await database.Database.GetPendingMigrationsAsync()];

    if (pending.Length > 0)
    {
        Console.Error.WriteLine(
            $"Refusing to start: {pending.Length} migration(s) have not been applied to this "
            + $"database ({string.Join(", ", pending)}).");

        Console.Error.WriteLine(
            "Run: dotnet run --project services/SpaceMMO.Api -- --seed");

        return 1;
    }
}

app.UseExceptionHandler();
app.UseStatusCodePages();

app.MapGet("/health", () => Results.Ok(new { status = "ok" }));

app.MapAccountEndpoints();
app.MapCharacterEndpoints();
app.MapGatheringEndpoints();
app.MapDockingEndpoints();
app.MapIndustryEndpoints();
app.MapMarketEndpoints();
app.MapQuestEndpoints();
app.MapWorldEndpoints();

await app.RunAsync();

return 0;

/// <summary>
/// Exposed so integration tests can drive the real application through
/// <c>WebApplicationFactory</c> rather than a stand-in.
/// </summary>
public partial class Program;
