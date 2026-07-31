using Microsoft.EntityFrameworkCore;
using SpaceMMO.Api.Auth;
using SpaceMMO.Api.Endpoints;
using SpaceMMO.Data;
using SpaceMMO.Data.Gathering;
using SpaceMMO.Data.Industry;
using SpaceMMO.Data.Inventories;
using SpaceMMO.Data.Market;
using SpaceMMO.Data.Quests;

// The HTTP surface over the M1 backend. Thin on purpose: rules live in SpaceMMO.Domain and
// transactions in SpaceMMO.Data, so nothing in this project decides a game outcome.

WebApplicationBuilder builder = WebApplication.CreateBuilder(args);

builder.Services.AddDbContext<SpaceMmoDbContext>(options =>
    options.UseNpgsql(
        builder.Configuration.GetConnectionString("SpaceMmo")
        ?? throw new InvalidOperationException(
            "Connection string 'SpaceMmo' is not configured.")));

// Scoped, matching the DbContext they wrap: these services run multi-statement transactions with
// row locks, and sharing one across requests would interleave two players' transactions.
builder.Services.AddScoped<MarketService>();
builder.Services.AddScoped<IndustryService>();
builder.Services.AddScoped<GatheringService>();
builder.Services.AddScoped<QuestService>();
builder.Services.AddScoped<InventoryService>();
builder.Services.AddScoped<Caller>();

builder.Services.AddSingleton(_ => new SessionTokens(
    builder.Configuration["Auth:SigningKey"]
    ?? throw new InvalidOperationException(
        "Auth:SigningKey is not configured. Set it in user secrets for local development; "
        + "a token signed with a guessable key is an account takeover.")));

builder.Services.AddProblemDetails();

WebApplication app = builder.Build();

app.UseExceptionHandler();
app.UseStatusCodePages();

app.MapGet("/health", () => Results.Ok(new { status = "ok" }));

app.MapAccountEndpoints();
app.MapCharacterEndpoints();
app.MapGatheringEndpoints();
app.MapIndustryEndpoints();
app.MapMarketEndpoints();
app.MapQuestEndpoints();

await app.RunAsync();

/// <summary>
/// Exposed so integration tests can drive the real application through
/// <c>WebApplicationFactory</c> rather than a stand-in.
/// </summary>
public partial class Program;
