using Microsoft.EntityFrameworkCore;
using SpaceMMO.Api.Auth;
using SpaceMMO.Data;
using SpaceMMO.Data.Inventories;
using SpaceMMO.Data.Market;
using SpaceMMO.Domain.Economy;
using SpaceMMO.Domain.Market;

namespace SpaceMMO.Api.Endpoints;

public sealed record PlaceOrderBody(
    int CharacterId,
    int StationId,
    int ItemDefId,
    OrderSide Side,
    long LimitPriceMinorUnits,
    int Quantity,
    int GoodForDays = 30);

public sealed record CancelOrderBody(int CharacterId, long OrderId);

public sealed record BookEntryResponse(long OrderId, OrderSide Side, long PriceMinorUnits, int QuantityRemaining);

/// <summary>
/// The order book: placing, cancelling, and reading it.
/// </summary>
/// <remarks>
/// Prices cross the wire as int64 minor units, never as a decimal or a float. A price that
/// survives a round trip through JSON as <c>12.34</c> is a price that can come back as
/// <c>12.339999999999999</c>, and in a market that difference eventually becomes a real credit.
/// </remarks>
public static class MarketEndpoints
{
    public static void MapMarketEndpoints(this IEndpointRouteBuilder routes)
    {
        RouteGroupBuilder group = routes.MapGroup("/market").WithTags("Market");

        group.MapPost("/orders", PlaceAsync);
        group.MapPost("/orders/cancel", CancelAsync);
        group.MapGet("/book", BookAsync);
    }

    private static async Task<IResult> PlaceAsync(
        PlaceOrderBody body,
        HttpContext context,
        Caller caller,
        MarketService market,
        CancellationToken cancellation)
    {
        OwnershipResult owned =
            await caller.OwnedCharacterAsync(context, body.CharacterId, cancellation);

        if (owned.Status != OwnershipStatus.Owned)
        {
            return owned.ToProblem();
        }

        if (body.Quantity <= 0 || body.LimitPriceMinorUnits <= 0)
        {
            return Results.ValidationProblem(new Dictionary<string, string[]>
            {
                ["order"] = ["Quantity and limit price must both be positive."],
            });
        }

        if (!Enum.IsDefined(body.Side))
        {
            return Results.ValidationProblem(new Dictionary<string, string[]>
            {
                ["side"] = ["Side must be Buy or Sell."],
            });
        }

        var request = new PlaceOrderRequest(
            body.CharacterId,
            body.StationId,
            body.ItemDefId,
            body.Side,
            Credits.FromMinorUnits(body.LimitPriceMinorUnits),
            body.Quantity,
            body.GoodForDays);

        try
        {
            PlaceOrderResult result = await market.PlaceOrderAsync(request, cancellation);

            return Results.Ok(result);
        }
        catch (InsufficientFundsException ex)
        {
            // A buy order locks credits at placement, so "not enough money" is a refusal to accept
            // the order rather than a failure part-way through one.
            return Results.Conflict(new { error = ex.Message, reason = "insufficient_funds" });
        }
        catch (InsufficientItemsException ex)
        {
            return Results.Conflict(new { error = ex.Message, reason = "insufficient_items" });
        }
    }

    private static async Task<IResult> CancelAsync(
        CancelOrderBody body,
        HttpContext context,
        Caller caller,
        MarketService market,
        CancellationToken cancellation)
    {
        OwnershipResult owned =
            await caller.OwnedCharacterAsync(context, body.CharacterId, cancellation);

        if (owned.Status != OwnershipStatus.Owned)
        {
            return owned.ToProblem();
        }

        bool cancelled = await market.CancelOrderAsync(body.OrderId, body.CharacterId, cancellation);

        return cancelled ? Results.Ok(new { cancelled = true }) : Results.NotFound();
    }

    /// <summary>
    /// The resting book for one item at one station.
    /// </summary>
    /// <remarks>
    /// Public and unauthenticated. Market depth is information every player is meant to trade on,
    /// and hiding it behind a login would only mean everyone scrapes it with one.
    /// </remarks>
    private static async Task<IResult> BookAsync(
        int stationId,
        int itemDefId,
        SpaceMmoDbContext database,
        CancellationToken cancellation)
    {
        List<BookEntryResponse> entries = await database.MarketOrders
            .Where(o => o.StationId == stationId
                && o.ItemDefId == itemDefId
                && o.QuantityRemaining > 0)
            .OrderBy(o => o.Side)
            .ThenBy(o => o.Price)
            .Select(o => new BookEntryResponse(
                o.Id, o.Side, o.Price.MinorUnits, o.QuantityRemaining))
            .ToListAsync(cancellation);

        return Results.Ok(entries);
    }
}
