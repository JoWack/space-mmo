using Microsoft.EntityFrameworkCore;
using SpaceMMO.Api.Auth;
using SpaceMMO.Api.Endpoints;
using SpaceMMO.Data;
using SpaceMMO.Data.Content;
using SpaceMMO.Data.Gathering;
using SpaceMMO.Data.Industry;
using SpaceMMO.Data.Inventories;
using SpaceMMO.Data.Market;
using SpaceMMO.Data.Quests;

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

    return 0;
}

app.UseExceptionHandler();
app.UseStatusCodePages();

app.MapGet("/health", () => Results.Ok(new { status = "ok" }));

app.MapAccountEndpoints();
app.MapCharacterEndpoints();
app.MapGatheringEndpoints();
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
