using Microsoft.EntityFrameworkCore;
using SpaceMMO.Api.Auth;
using SpaceMMO.Data;
using SpaceMMO.Data.Docking;
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

public sealed record SellToFactionBody(int CharacterId, int StationId, int ItemDefId, int Quantity);

/// <summary>
/// What a faction standing order actually took and paid.
/// </summary>
/// <param name="QuantitySold">
/// Units taken, which may be fewer than asked for. The daily faucet budget reduces the sale rather
/// than refusing it, so a player at the cap can still sell the one unit they need.
/// </param>
/// <param name="WithheldMinorUnits">
/// What the cap refused. Nonzero means the material is still in the hangar, not that it was taken
/// unpaid.
/// </param>
public sealed record FactionSaleResponse(
    int QuantitySold, long PaidMinorUnits, long WithheldMinorUnits, bool WasCapped);

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

        // Separate from /orders because it is not an order. There is no book, no counterparty and
        // no matching: the faction takes the material at a fixed price and the credits are created
        // rather than moved. Folding it into order placement would hide a faucet inside a transfer.
        group.MapPost("/faction-orders/sell", SellToFactionAsync);
    }

    private static async Task<IResult> SellToFactionAsync(
        SellToFactionBody request,
        HttpContext context,
        Caller caller,
        FactionOrderService factionOrders,
        DockingService docking,
        CancellationToken cancellation)
    {
        OwnershipResult owned =
            await caller.OwnedCharacterAsync(context, request.CharacterId, cancellation);

        if (owned.Status != OwnershipStatus.Owned)
        {
            return owned.ToProblem();
        }

        // The faction standing order is a counter at a station like any other. Leaving it open
        // from anywhere would make it the one way to turn goods into credits without ever
        // arriving somewhere — and it is deliberately the worst price in the game precisely
        // because it is always available once you are there.
        if (await RefuseIfNotDockedAsync(
            docking, request.CharacterId, request.StationId, cancellation) is { } refusal)
        {
            return refusal;
        }

        if (request.Quantity <= 0)
        {
            return Results.ValidationProblem(new Dictionary<string, string[]>
            {
                ["quantity"] = ["Quantity must be positive."],
            });
        }

        try
        {
            FactionSaleResult result = await factionOrders.SellAsync(
                request.CharacterId,
                request.StationId,
                request.ItemDefId,
                request.Quantity,
                cancellation);

            return Results.Ok(new FactionSaleResponse(
                result.QuantitySold,
                result.Paid.MinorUnits,
                result.Withheld.MinorUnits,
                result.WasCapped));
        }
        catch (NotBoughtByFactionException ex)
        {
            return Results.Conflict(new { error = ex.Message, reason = "not_bought" });
        }
        catch (InsufficientItemsException ex)
        {
            return Results.Conflict(new { error = ex.Message, reason = "insufficient_items" });
        }
    }

    /// <summary>
    /// Refuses a request whose caller is not docked at the station it names.
    /// </summary>
    /// <remarks>
    /// <para>
    /// The market is a place, and being at it is what entitles you to use it. Without this the
    /// order book is reachable from anywhere in the system, which removes the reason stations
    /// exist and the reason hauling is a profession — a seller on Grimhold could list into the
    /// capital's book without crossing the contested approach at all (ADR-0008).
    /// </para>
    /// <para>
    /// Checked against the station the request names rather than "docked anywhere", because a
    /// character docked at Grimhold has no business on a Terra order book and a laxer check would
    /// let them.
    /// </para>
    /// </remarks>
    private static async Task<IResult?> RefuseIfNotDockedAsync(
        DockingService docking,
        int characterId,
        int stationId,
        CancellationToken cancellation)
    {
        if (await docking.IsDockedAtAsync(characterId, stationId, cancellation))
        {
            return null;
        }

        // Conflict rather than forbidden: nothing about the caller is wrong, they are simply
        // somewhere else, and flying there fixes it.
        return Results.Conflict(new
        {
            error = "You must be docked at this station to trade here.",
            reason = "not_docked",
        });
    }

    private static async Task<IResult> PlaceAsync(
        PlaceOrderBody body,
        HttpContext context,
        Caller caller,
        MarketService market,
        DockingService docking,
        CancellationToken cancellation)
    {
        OwnershipResult owned =
            await caller.OwnedCharacterAsync(context, body.CharacterId, cancellation);

        if (owned.Status != OwnershipStatus.Owned)
        {
            return owned.ToProblem();
        }

        if (await RefuseIfNotDockedAsync(
            docking, body.CharacterId, body.StationId, cancellation) is { } refusal)
        {
            return refusal;
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
