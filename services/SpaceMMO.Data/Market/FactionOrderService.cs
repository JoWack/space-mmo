using Microsoft.EntityFrameworkCore;
using SpaceMMO.Data.Entities;
using SpaceMMO.Data.Inventories;
using SpaceMMO.Domain.Economy;

namespace SpaceMMO.Data.Market;

/// <summary>Thrown when no faction has a standing order for an item.</summary>
public sealed class NotBoughtByFactionException(int itemDefId)
    : InvalidOperationException($"No faction standing order buys item {itemDefId}.")
{
    public int ItemDefId { get; } = itemDefId;
}

/// <summary>
/// What a sale to a faction standing order actually did.
/// </summary>
/// <param name="QuantitySold">Units taken. Zero if the daily cap left no room.</param>
/// <param name="Paid">Credits actually created and paid.</param>
/// <param name="Withheld">Credits the daily cap refused.</param>
public readonly record struct FactionSaleResult(int QuantitySold, Credits Paid, Credits Withheld)
{
    /// <summary>True if the daily faucet cap reduced this sale.</summary>
    public bool WasCapped => Withheld.IsPositive;
}

/// <summary>
/// The faction standing orders that buy raw material for credits — the faucet of last resort.
/// </summary>
/// <remarks>
/// <para>
/// <strong>This exists so that no character can ever be permanently stuck.</strong> At zero credits
/// a player cannot start an industry job, because jobs charge a fee up front, and cannot place a
/// sell order either, because that charges a broker fee up front. Gathering still worked and
/// produced nothing but material, so a player who spent down to nothing could mine forever without
/// a way back. This is the one transaction that takes goods and pays money with nothing owed first.
/// </para>
/// <para>
/// <strong>It is deliberately the worst deal available.</strong> Prices are authored as a floor, not
/// a valuation. If selling to a faction ever beats selling to another player, players stop trading
/// with each other and the market this game is built around never forms.
/// </para>
/// <para>
/// Credits created here route through the same daily budget as repeatable sidequests
/// (<see cref="FaucetBudget"/>), because deposits respawn and this would otherwise be an income
/// bounded only by how fast somebody can mine.
/// </para>
/// </remarks>
public sealed class FactionOrderService(SpaceMmoDbContext database)
{
    private readonly SpaceMmoDbContext _database =
        database ?? throw new ArgumentNullException(nameof(database));

    private readonly InventoryService _inventories = new(database);

    /// <summary>
    /// Sells material from a character's hangar to the faction standing order.
    /// </summary>
    /// <remarks>
    /// <para>
    /// The whole thing runs in one transaction. Material leaving the economy and credits entering
    /// it are two halves of one exchange, and a crash between them would either destroy a player's
    /// goods for nothing or pay them for goods they kept.
    /// </para>
    /// <para>
    /// When the cap bites, the sale is <em>reduced</em> rather than refused: as many units are sold
    /// as the remaining budget pays for, and the rest stay in the hangar. Taking the material and
    /// withholding the credits would be theft, and refusing outright would leave a player at the cap
    /// unable to sell the one unit they needed.
    /// </para>
    /// </remarks>
    /// <exception cref="NotBoughtByFactionException">If nothing buys that item.</exception>
    /// <exception cref="InsufficientItemsException">If the hangar holds too few.</exception>
    public async Task<FactionSaleResult> SellAsync(
        int characterId,
        int stationId,
        int itemDefId,
        int quantity,
        CancellationToken cancellationToken = default)
    {
        if (quantity <= 0)
        {
            throw new ArgumentOutOfRangeException(
                nameof(quantity), quantity, "Quantity must be positive.");
        }

        ItemDef item = await _database.ItemDefs.SingleAsync(
            d => d.Id == itemDefId, cancellationToken);

        if (item.FactionBuyPrice is not { } unitPrice || !unitPrice.IsPositive)
        {
            throw new NotBoughtByFactionException(itemDefId);
        }

        await using var transaction =
            await _database.Database.BeginTransactionAsync(cancellationToken);

        DateTimeOffset now = DateTimeOffset.UtcNow;

        Inventory hangar = await _inventories.GetOrCreateStationHangarAsync(
            characterId, stationId, cancellationToken);

        // How much of what they asked to sell the budget will actually pay for. Integer division,
        // so a partial unit is never bought: paying for 3.7 units and taking 4 would round against
        // the player, and taking 3 while charging for 4 would round against the economy.
        Credits alreadyGranted = await GrantedTodayAsync(characterId, now, cancellationToken);

        Credits remaining = Credits.Max(
            Credits.Zero, FaucetBudget.DefaultDailyCap - alreadyGranted);

        int affordable = (int)Math.Min(
            quantity, remaining.MinorUnits / unitPrice.MinorUnits);

        if (affordable <= 0)
        {
            // Nothing sold and nothing taken. The material is worth more in the hangar tomorrow
            // than it is gone today.
            return new FactionSaleResult(
                0, Credits.Zero, unitPrice * quantity);
        }

        Credits gross = unitPrice * affordable;

        // Removed first: if the hangar is short this throws before any credits are created, which
        // is the correct direction for the failure to point.
        await _inventories.RemoveAsync(hangar.Id, itemDefId, affordable, cancellationToken);

        await RecordGrantAsync(characterId, gross, now, cancellationToken);

        Character character = await _database.Characters.SingleAsync(
            c => c.Id == characterId, cancellationToken);

        character.Balance += gross;

        _database.LedgerEntries.Add(new LedgerEntry
        {
            CharacterId = characterId,
            DeltaCredits = gross,
            Reason = LedgerReason.FactionPurchase,
            ReferenceId = itemDefId,
            CreatedAt = now,
        });

        await _database.SaveChangesAsync(cancellationToken);
        await transaction.CommitAsync(cancellationToken);

        return new FactionSaleResult(
            affordable, gross, unitPrice * (quantity - affordable));
    }

    private async Task<Credits> GrantedTodayAsync(
        int characterId, DateTimeOffset now, CancellationToken cancellationToken)
    {
        DateOnly utcDate = DateOnly.FromDateTime(now.UtcDateTime);

        CharacterFaucetDaily? today = await _database.CharacterFaucetDailies.FirstOrDefaultAsync(
            d => d.CharacterId == characterId && d.UtcDate == utcDate, cancellationToken);

        return today?.CreditsGranted ?? Credits.Zero;
    }

    /// <summary>
    /// Adds to the character's capped-faucet total for the day.
    /// </summary>
    /// <remarks>
    /// The same row sidequest rewards write to, deliberately. One budget per character per day
    /// covering every farmable faucet is what keeps the aggregate rate bounded; a second counter
    /// would let a player take both in full.
    /// </remarks>
    private async Task RecordGrantAsync(
        int characterId, Credits granted, DateTimeOffset now, CancellationToken cancellationToken)
    {
        DateOnly utcDate = DateOnly.FromDateTime(now.UtcDateTime);

        CharacterFaucetDaily? today = await _database.CharacterFaucetDailies.FirstOrDefaultAsync(
            d => d.CharacterId == characterId && d.UtcDate == utcDate, cancellationToken);

        if (today is null)
        {
            _database.CharacterFaucetDailies.Add(new CharacterFaucetDaily
            {
                CharacterId = characterId,
                UtcDate = utcDate,
                CreditsGranted = granted,
            });

            return;
        }

        today.CreditsGranted += granted;
    }
}
