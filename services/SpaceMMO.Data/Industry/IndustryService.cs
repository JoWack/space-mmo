using Microsoft.EntityFrameworkCore;
using SpaceMMO.Data.Content;
using SpaceMMO.Data.Entities;
using SpaceMMO.Data.Progression;
using SpaceMMO.Data.Inventories;
using SpaceMMO.Data.Market;
using SpaceMMO.Data.Quests;
using SpaceMMO.Domain.Economy;
using SpaceMMO.Domain.Industry;
using SpaceMMO.Domain.Items;
using SpaceMMO.Domain.Progression;
using SpaceMMO.Domain.Quests;

namespace SpaceMMO.Data.Industry;

/// <summary>Thrown when a character's skill is too low for a recipe.</summary>
public sealed class SkillTooLowException(string skillKey, int required, int actual)
    : InvalidOperationException($"Requires {skillKey} level {required} but character is {actual}.")
{
    public string SkillKey { get; } = skillKey;

    public int Required { get; } = required;

    public int Actual { get; } = actual;
}

/// <summary>Thrown when every industry slot for a skill is already occupied.</summary>
public sealed class NoFreeJobSlotException(string skillKey, int slots)
    : InvalidOperationException(
        $"All {slots} {skillKey} job slots are in use. Claim or cancel a job first.")
{
    public string SkillKey { get; } = skillKey;

    public int Slots { get; } = slots;
}

/// <summary>Thrown when a recipe or a deposit needs a tool the character does not have equipped.</summary>
/// <remarks>"Equipped" in the words since task 193: a player holding the laser in their pockets is otherwise
/// told they need something they are carrying.</remarks>
public sealed class MissingToolException(string toolKey, string? toolName = null)
    : InvalidOperationException($"You need a {toolName ?? toolKey} equipped to do this.")
{
    /// <summary>The item's key, for anything deciding what to do about it.</summary>
    public string ToolKey { get; } = toolKey;

    /// <summary>
    /// Wording is neutral because this is thrown from two places that are not the same activity.
    /// It said "This recipe requires a ..." until deposits could require tools too, at which point
    /// mining a rock started telling players about a recipe they were not making. The name is used
    /// in preference to the key, since the message is read by a person and "crude_mining_laser" is
    /// not how anyone refers to a mining laser.
    /// </summary>
    public string ToolName { get; } = toolName ?? toolKey;
}

/// <summary>The outcome of starting a job.</summary>
/// <param name="JobId">The running job.</param>
/// <param name="CompletesAt">When outputs become claimable.</param>
/// <param name="Fee">Charged at start; never refunded.</param>
/// <param name="InputCostBasis">What the consumed materials cost, carried into the output.</param>
public readonly record struct StartJobResult(
    long JobId,
    DateTimeOffset CompletesAt,
    Credits Fee,
    Credits InputCostBasis);

/// <summary>The outcome of claiming a completed job.</summary>
/// <param name="ItemDefId">What was produced.</param>
/// <param name="Quantity">How many units.</param>
/// <param name="XpAwarded">Skill XP granted — at claim only, never at start.</param>
/// <param name="InstanceIds">Item instance ids, for non-stackable outputs. Empty otherwise.</param>
public readonly record struct ClaimJobResult(
    int ItemDefId,
    int Quantity,
    long XpAwarded,
    IReadOnlyList<long> InstanceIds);

/// <summary>
/// Runs time-gated manufacturing jobs, per design-bible §6.
/// </summary>
/// <remarks>
/// <para>
/// Inputs are consumed at start and outputs created at claim. Consuming late would let one pile
/// of ore seed several jobs; creating early would hand over goods before the time cost was paid.
/// </para>
/// <para>
/// <strong>XP is awarded at claim and never at start</strong>, or start-and-cancel would be an XP
/// farm costing only the job fee.
/// </para>
/// <para>
/// Completion is measured against the server clock, so jobs finish whether or not the player is
/// connected. That is what makes long durations compatible with having a life.
/// </para>
/// </remarks>
public sealed class IndustryService(SpaceMmoDbContext database)
{
    private readonly SpaceMmoDbContext _database =
        database ?? throw new ArgumentNullException(nameof(database));

    private readonly InventoryService _inventories = new(database);

    private readonly EquipmentService _equipment = new(database);

    /// <summary>
    /// Starts a job: checks the gates, consumes inputs, charges the fee, and sets the clock.
    /// </summary>
    /// <exception cref="ArgumentOutOfRangeException">If runs is not positive.</exception>
    /// <exception cref="SkillTooLowException">If the character's skill is below the requirement.</exception>
    /// <exception cref="NoFreeJobSlotException">If every slot for that skill is occupied.</exception>
    /// <exception cref="MissingToolException">If a required tool is not owned.</exception>
    /// <exception cref="InsufficientItemsException">If materials are short.</exception>
    /// <exception cref="InsufficientFundsException">If the fee cannot be paid.</exception>
    /// <exception cref="ContentRetiredException">If the pack no longer names the recipe (task 188).</exception>
    /// <param name="mainInputStyle">
    /// Which style of the recipe's main input to make it from, and so the output's style (task 187).
    /// Null takes the style held most of. Ignored for a recipe without a main input, and for a raw
    /// main input, whose style is its world's.
    /// </param>
    public async Task<StartJobResult> StartJobAsync(
        int characterId,
        int recipeId,
        int stationId,
        int runs,
        ItemStyle? mainInputStyle = null,
        CancellationToken cancellationToken = default)
    {
        if (runs <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(runs), runs, "Runs must be positive.");
        }

        await using var transaction =
            await _database.Database.BeginTransactionAsync(cancellationToken);

        // Same lock order as MarketService — character first — so the two services can never
        // deadlock against each other.
        Character character = await LockCharacterAsync(characterId, cancellationToken);

        Recipe recipe = await _database.Recipes
            .Include(r => r.Inputs)
            .Include(r => r.Skill)
            .Include(r => r.MainInputItemDef)
            .SingleAsync(r => r.Id == recipeId, cancellationToken);

        // Before any gate, so the answer is about the recipe rather than about the player: no skill,
        // tool or material would ever let them start it again.
        if (recipe.RetiredAt is not null)
        {
            throw new ContentRetiredException("recipe", recipe.Key);
        }

        int level = await SkillLevelAsync(characterId, recipe.SkillId, cancellationToken);

        if (level < recipe.RequiredLevel)
        {
            throw new SkillTooLowException(recipe.Skill!.Key, recipe.RequiredLevel, level);
        }

        await GuardFreeSlotAsync(characterId, recipe, level, cancellationToken);
        await GuardToolAsync(characterId, recipe, cancellationToken);

        Inventory hangar = await _inventories.GetOrCreateStationHangarAsync(
            characterId, stationId, cancellationToken);

        Credits fee = IndustryFees.ForJob(runs);

        if (character.Balance < fee)
        {
            throw new InsufficientFundsException(characterId, fee, character.Balance);
        }

        // Bring what is missing across from the player's own hands before consuming anything.
        //
        // Crafting draws on the hangar and on what you are carrying, which is what a player means
        // by "I have the materials" — they are standing in the station holding the ore. Doing it
        // here rather than making them drag it across first keeps the rule Joe asked for (transfer,
        // then craft) while removing the chore: the transfer still happens, and still leaves an
        // honest record of where the goods went, it is just not a job for the player.
        //
        // <strong>Only while docked here.</strong> Without that, starting a job at a station a
        // character is nowhere near would haul goods out of their pockets and across the system,
        // which is the same hole task 114 describes and worse, because it would move things.
        //
        // Through TransferAsync so the cost basis travels with the material (task 99). Material
        // that arrived as though it had cost nothing pays nothing when it is lost (ADR-0006).
        bool docked = character.DockedStationId == stationId;

        Inventory? carried = docked
            ? await _inventories.GetOrCreateCarriedAsync(characterId, cancellationToken)
            : null;

        // Decided before anything moves, from the hangar and the pockets together: they are what
        // a player standing here means by "what I have" (task 187).
        MainInputDraw draw = await DecideMainInputAsync(
            recipe, hangar, carried, mainInputStyle, cancellationToken);

        if (carried is not null)
        {
            foreach (RecipeInput input in recipe.Inputs)
            {
                // The main input tops up in its one style; the rest in any, the output's first, as
                // they will be consumed.
                bool isMain = input.ItemDefId == recipe.MainInputItemDefId;

                await TopUpAsync(
                    carried,
                    hangar,
                    input.ItemDefId,
                    input.Quantity * runs,
                    isMain ? draw.StackStyle : null,
                    draw.OutputStyle,
                    cancellationToken);
            }

            // Flushed before anything reads it back.
            //
            // AddAsync and RemoveAsync mutate tracked entities and leave saving to the caller, and
            // the consumption below finds its stack with a database query. Where the hangar already
            // held some of an input that works by accident -- the query returns the very entity the
            // top-up just changed -- but where it held none, the new row exists only in the change
            // tracker and the query cannot see it. The job then fails for want of material that is
            // sitting in the same transaction.
            //
            // Still atomic: this is inside the transaction opened above, so a later failure rolls
            // the move back with everything else.
            await _database.SaveChangesAsync(cancellationToken);
        }

        // Consume every input first, so a shortfall on the last one aborts before anything is
        // charged. The transaction would roll it back anyway; failing early keeps the error
        // pointing at the actual missing material.
        Credits inputCostBasis = Credits.Zero;
        var consumed = new List<IndustryJobInput>(recipe.Inputs.Count);

        foreach (RecipeInput input in recipe.Inputs)
        {
            int quantity = input.Quantity * runs;

            // The main input from its one style, so the output's style is true of all of it. The
            // rest from any, the output's first: a Terran shuttle takes Terran thrusters while there
            // are any (task 187).
            IReadOnlyList<(ItemStyle Style, int Quantity, Credits Cost)> drawn =
                input.ItemDefId == recipe.MainInputItemDefId
                    ? [(draw.StackStyle, quantity, await _inventories.RemoveAsync(
                        hangar.Id, input.ItemDefId, quantity, draw.StackStyle, cancellationToken))]
                    : await _inventories.RemoveAnyStyleAsync(
                        hangar.Id, input.ItemDefId, quantity, draw.OutputStyle, cancellationToken);

            foreach ((ItemStyle style, int taken, Credits cost) in drawn)
            {
                inputCostBasis += cost;

                consumed.Add(new IndustryJobInput
                {
                    ItemDefId = input.ItemDefId,
                    Style = style,
                    Quantity = taken,
                    CostBasis = cost,
                });
            }
        }

        DateTimeOffset now = DateTimeOffset.UtcNow;
        long totalSeconds = IndustryRefund.TotalJobSeconds(recipe.JobSeconds, runs);

        var job = new IndustryJob
        {
            CharacterId = characterId,
            RecipeId = recipeId,
            StationId = stationId,
            Runs = runs,
            State = IndustryJobState.Running,
            StartedAt = now,
            CompletesAt = now.AddSeconds(totalSeconds),
            FeePaid = fee,
            InputCostBasis = inputCostBasis,
            OutputStyle = draw.OutputStyle,
        };

        _database.IndustryJobs.Add(job);
        await _database.SaveChangesAsync(cancellationToken);

        foreach (IndustryJobInput input in consumed)
        {
            input.IndustryJobId = job.Id;
            _database.Set<IndustryJobInput>().Add(input);
        }

        await _database.SaveChangesAsync(cancellationToken);

        await AdjustBalanceAsync(
            characterId, -fee, LedgerReason.IndustryFee, job.Id, now, cancellationToken);

        await _database.SaveChangesAsync(cancellationToken);
        await transaction.CommitAsync(cancellationToken);

        return new StartJobResult(job.Id, job.CompletesAt, fee, inputCostBasis);
    }

    /// <summary>
    /// Claims a completed job: delivers outputs and awards XP.
    /// </summary>
    /// <exception cref="InvalidOperationException">If the job is not running or not yet complete.</exception>
    /// <exception cref="UnauthorizedAccessException">If the job belongs to someone else.</exception>
    public async Task<ClaimJobResult> ClaimJobAsync(
        long jobId, int characterId, CancellationToken cancellationToken = default)
    {
        await using var transaction =
            await _database.Database.BeginTransactionAsync(cancellationToken);

        IndustryJob job = await LockJobAsync(jobId, characterId, cancellationToken);

        DateTimeOffset now = DateTimeOffset.UtcNow;

        // The server clock is the only authority. A client claiming early is simply refused.
        if (now < job.CompletesAt)
        {
            throw new InvalidOperationException(
                $"Job {jobId} completes at {job.CompletesAt:O}; it is {now:O}.");
        }

        Recipe recipe = await _database.Recipes
            .SingleAsync(r => r.Id == job.RecipeId, cancellationToken);

        ItemCategory category = await _database.ItemDefs
            .Where(d => d.Id == recipe.OutputItemDefId)
            .Select(d => d.Category)
            .SingleAsync(cancellationToken);

        int quantity = recipe.OutputQuantity * job.Runs;

        Inventory hangar = await _inventories.GetOrCreateStationHangarAsync(
            characterId, job.StationId, cancellationToken);

        // What the output genuinely cost: the materials that went in, plus the fee paid to make
        // it. This is the figure insurance pegs to, and it is why cost basis is tracked at all
        // (ADR-0006).
        Credits outputCost = job.InputCostBasis + job.FeePaid;

        var instanceIds = new List<long>();

        if (category.IsStackable())
        {
            await _inventories.AddAsync(
                hangar.Id, recipe.OutputItemDefId, quantity, outputCost, job.OutputStyle,
                cancellationToken);
        }
        else
        {
            // Non-stackable outputs become tracked instances, each carrying an equal share of the
            // production cost.
            Credits perUnit = Credits.FromMinorUnits(outputCost.MinorUnits / quantity);
            Credits remainder = outputCost - (perUnit * quantity);

            for (int i = 0; i < quantity; i++)
            {
                var instance = new ItemInstance
                {
                    ItemDefId = recipe.OutputItemDefId,
                    InventoryId = hangar.Id,
                    Condition = 100,
                    Style = job.OutputStyle,

                    // The rounding remainder goes to the first unit so the instances sum back to
                    // exactly what the job cost.
                    AcquisitionValue = i == 0 ? perUnit + remainder : perUnit,
                    CreatedAt = now,
                };

                _database.ItemInstances.Add(instance);

                // Straight into its slot if that slot is empty (Joe, 16 October), so the opening's mining
                // laser is ready to mine with the moment it is claimed; otherwise the hangar, as before.
                await _equipment.EquipIfSlotEmptyAsync(characterId, instance, cancellationToken);

                await _database.SaveChangesAsync(cancellationToken);

                instanceIds.Add(instance.Id);
            }
        }

        long xp = recipe.XpPerRun * job.Runs;

        if (xp > 0)
        {
            await AwardXpAsync(characterId, recipe.SkillId, xp, cancellationToken);
        }

        job.State = IndustryJobState.Claimed;
        job.ClaimedAt = now;

        // Reported at claim, not at start. Inputs are consumed when a job begins but nothing has
        // been produced yet, and a quest that counted the start would credit a player for output
        // they could still cancel out from under.
        //
        // Refine and Craft are the same operation with different words for the player — the
        // objective enum says so — but they are matched exactly, so reporting the wrong verb makes
        // a step silently refuse to advance. The recipe's skill decides which, because that is what
        // content already uses to distinguish them.
        string outputKey = await _database.ItemDefs
            .Where(d => d.Id == recipe.OutputItemDefId)
            .Select(d => d.Key)
            .SingleAsync(cancellationToken);

        string skillKey = await _database.Skills
            .Where(s => s.Id == recipe.SkillId)
            .Select(s => s.Key)
            .SingleAsync(cancellationToken);

        ObjectiveType verb = string.Equals(skillKey, "refining", StringComparison.Ordinal)
            ? ObjectiveType.Refine
            : ObjectiveType.Craft;

        await new QuestService(_database).RecordProgressAsync(
            characterId, new ObjectiveEvent(verb, outputKey, quantity), cancellationToken);

        await _database.SaveChangesAsync(cancellationToken);
        await transaction.CommitAsync(cancellationToken);

        return new ClaimJobResult(recipe.OutputItemDefId, quantity, xp, instanceIds);
    }

    /// <summary>
    /// Cancels a running job, refunding inputs in proportion to the time remaining.
    /// </summary>
    /// <remarks>
    /// The fee is not refunded, and no XP is awarded. Together those are what stop
    /// start-and-cancel being either free or a way to farm progression.
    /// </remarks>
    /// <returns>True if cancelled; false if the job was already finished or cancelled.</returns>
    public async Task<bool> CancelJobAsync(
        long jobId, int characterId, CancellationToken cancellationToken = default)
    {
        await using var transaction =
            await _database.Database.BeginTransactionAsync(cancellationToken);

        IndustryJob job = await LockJobAsync(jobId, characterId, cancellationToken, requireRunning: false);

        if (job.State != IndustryJobState.Running)
        {
            return false;
        }

        DateTimeOffset now = DateTimeOffset.UtcNow;

        // Duration comes from the recipe, but the materials come from what the job actually
        // recorded consuming — a recipe rebalanced mid-job must not change the refund.
        int jobSeconds = await _database.Recipes
            .Where(r => r.Id == job.RecipeId)
            .Select(r => r.JobSeconds)
            .SingleAsync(cancellationToken);

        long totalSeconds = IndustryRefund.TotalJobSeconds(jobSeconds, job.Runs);
        long elapsedSeconds = (long)(now - job.StartedAt).TotalSeconds;

        Inventory hangar = await _inventories.GetOrCreateStationHangarAsync(
            characterId, job.StationId, cancellationToken);

        List<IndustryJobInput> consumed = await _database.Set<IndustryJobInput>()
            .Where(i => i.IndustryJobId == job.Id)
            .ToListAsync(cancellationToken);

        foreach (IndustryJobInput input in consumed)
        {
            int refunded = IndustryRefund.RefundedQuantity(
                input.Quantity, elapsedSeconds, totalSeconds);

            if (refunded == 0)
            {
                continue;
            }

            // Materials come back carrying their proportional share of what they cost, so a
            // cancel-and-restart cycle cannot launder cost basis away.
            Credits share = ShareOf(input.CostBasis, refunded, input.Quantity);

            // Back to the stack it came from: Terran plate refunds as Terran plate (task 187).
            await _inventories.AddAsync(
                hangar.Id, input.ItemDefId, refunded, share, input.Style, cancellationToken);
        }

        job.State = IndustryJobState.Cancelled;
        job.ClaimedAt = now;

        await _database.SaveChangesAsync(cancellationToken);
        await transaction.CommitAsync(cancellationToken);

        return true;
    }

    /// <summary>Jobs currently occupying a slot for one skill.</summary>
    public async Task<int> RunningJobCountAsync(
        int characterId, int skillId, CancellationToken cancellationToken = default) =>
        await _database.IndustryJobs
            .CountAsync(
                j => j.CharacterId == characterId
                    && j.State == IndustryJobState.Running
                    && j.Recipe!.SkillId == skillId,
                cancellationToken);

    // ── Style ────────────────────────────────────────────────────────────────

    /// <summary>
    /// How a job draws its main input, and the style that gives its output (task 187).
    /// </summary>
    /// <param name="StackStyle">The stack the main input comes from: Common for a raw material.</param>
    /// <param name="OutputStyle">The style the job's output will have.</param>
    private readonly record struct MainInputDraw(ItemStyle StackStyle, ItemStyle OutputStyle);

    /// <summary>
    /// Decides which style of the main input a job uses, and so its output's style.
    /// </summary>
    /// <remarks>
    /// <para>
    /// <strong>A raw main input decides by itself:</strong> ferrite ore is Terran wherever it is, so
    /// plate refined from it is Terran whatever was asked for.
    /// </para>
    /// <para>
    /// <strong>A crafted one is the player's pick</strong>, or the style held most of when they did
    /// not pick. A pick they hold none of is not corrected to one they do: the consumption refuses it,
    /// naming the shortfall, rather than quietly making something they did not ask for.
    /// </para>
    /// </remarks>
    private async Task<MainInputDraw> DecideMainInputAsync(
        Recipe recipe,
        Inventory hangar,
        Inventory? carried,
        ItemStyle? requested,
        CancellationToken cancellationToken)
    {
        if (recipe.MainInputItemDef is not { } main)
        {
            return new MainInputDraw(ItemStyle.Common, ItemStyles.ForOutput(null));
        }

        if (main.Category == ItemCategory.Raw)
        {
            return new MainInputDraw(ItemStyle.Common, ItemStyles.ForOutput(main.Style));
        }

        ItemStyle? style = requested;

        if (style is null)
        {
            Dictionary<ItemStyle, int> held = await _inventories.QuantitiesByStyleAsync(
                hangar.Id, main.Id, cancellationToken);

            if (carried is not null)
            {
                foreach ((ItemStyle carriedStyle, int quantity) in await _inventories
                    .QuantitiesByStyleAsync(carried.Id, main.Id, cancellationToken))
                {
                    held[carriedStyle] = held.GetValueOrDefault(carriedStyle) + quantity;
                }
            }

            style = ItemStyles.MostHeld(held);
        }

        // Nothing held at all: Common, and the consumption reports the shortfall.
        ItemStyle chosen = style ?? ItemStyle.Common;

        return new MainInputDraw(chosen, ItemStyles.ForOutput(chosen));
    }

    /// <summary>
    /// Moves what is missing from the hangar across from the pockets, in one style or any.
    /// </summary>
    /// <param name="style">The one style to move, or null for any.</param>
    /// <param name="preferred">With any style, which to move first.</param>
    private async Task TopUpAsync(
        Inventory carried,
        Inventory hangar,
        int itemDefId,
        int needed,
        ItemStyle? style,
        ItemStyle preferred,
        CancellationToken cancellationToken)
    {
        Dictionary<ItemStyle, int> here = await _inventories.QuantitiesByStyleAsync(
            hangar.Id, itemDefId, cancellationToken);

        int alreadyHere = style is { } one ? here.GetValueOrDefault(one) : here.Values.Sum();

        if (alreadyHere >= needed)
        {
            return;
        }

        Dictionary<ItemStyle, int> carrying = await _inventories.QuantitiesByStyleAsync(
            carried.Id, itemDefId, cancellationToken);

        IEnumerable<ItemStyle> order = style is { } only
            ? [only]
            : ItemStyles.DrawOrder(carrying, preferred);

        // Whatever closes the gap, or everything carried if that is not enough. Falling short here
        // is not an error: the consumption reports it, against the material actually missing rather
        // than against a transfer.
        int gap = needed - alreadyHere;

        foreach (ItemStyle from in order)
        {
            int moving = Math.Min(gap, carrying.GetValueOrDefault(from));

            if (moving <= 0)
            {
                continue;
            }

            await _inventories.TransferAsync(
                carried.Id, hangar.Id, itemDefId, moving, from, cancellationToken);

            gap -= moving;

            if (gap == 0)
            {
                break;
            }
        }
    }

    // ── Gates ────────────────────────────────────────────────────────────────

    private async Task GuardFreeSlotAsync(
        int characterId, Recipe recipe, int level, CancellationToken cancellationToken)
    {
        int slots = IndustrySlots.MaxConcurrentJobs(level);
        int running = await RunningJobCountAsync(characterId, recipe.SkillId, cancellationToken);

        if (running >= slots)
        {
            throw new NoFreeJobSlotException(recipe.Skill!.Key, slots);
        }
    }

    private async Task GuardToolAsync(
        int characterId, Recipe recipe, CancellationToken cancellationToken)
    {
        if (recipe.RequiredToolItemDefId is not int toolDefId)
        {
            return;
        }

        // Equipped, as gathering's gate asks (task 193): a tool must be in its slot to work. This one used to
        // accept a tool anywhere the character owned it -- a laser in a hangar four worlds away -- which was
        // the sibling of gathering's old rule and outlived it.
        //
        // A broken one does not count -- condition is what the repair loop will eventually act on.
        bool equipped = await _database.ItemInstances.AnyAsync(
            instance => instance.ItemDefId == toolDefId
                && instance.DestroyedAt == null
                && instance.Condition > 0
                && instance.EquippedSlot != null
                && instance.Inventory!.CharacterId == characterId
                && instance.Inventory.Kind == InventoryKind.Equipped,
            cancellationToken);

        if (!equipped)
        {
            // Both, because the message is read by a person and the key is read by code.
            var tool = await _database.ItemDefs
                .Where(d => d.Id == toolDefId)
                .Select(d => new { d.Key, d.Name })
                .SingleAsync(cancellationToken);

            throw new MissingToolException(tool.Key, tool.Name);
        }
    }

    // ── Persistence helpers ──────────────────────────────────────────────────

    private async Task<int> SkillLevelAsync(
        int characterId, int skillId, CancellationToken cancellationToken)
    {
        long xp = await _database.CharacterSkills
            .Where(s => s.CharacterId == characterId && s.SkillId == skillId)
            .Select(s => s.Xp)
            .FirstOrDefaultAsync(cancellationToken);

        return SkillCurve.LevelForXp(xp);
    }

    private Task AwardXpAsync(
        int characterId, int skillId, long xp, CancellationToken cancellationToken) =>
        SkillAwards.AwardAsync(_database, characterId, skillId, xp, cancellationToken);

    private async Task<Character> LockCharacterAsync(
        int characterId, CancellationToken cancellationToken)
    {
        List<Character> locked = await _database.Characters
            .FromSqlInterpolated($"SELECT * FROM characters WHERE id = {characterId} FOR UPDATE")
            .ToListAsync(cancellationToken);

        return locked.Count == 1
            ? locked[0]
            : throw new InvalidOperationException($"Character {characterId} does not exist.");
    }

    private async Task<IndustryJob> LockJobAsync(
        long jobId, int characterId, CancellationToken cancellationToken, bool requireRunning = true)
    {
        List<IndustryJob> locked = await _database.IndustryJobs
            .FromSqlInterpolated($"SELECT * FROM industry_jobs WHERE id = {jobId} FOR UPDATE")
            .ToListAsync(cancellationToken);

        IndustryJob job = locked.Count == 1
            ? locked[0]
            : throw new InvalidOperationException($"Job {jobId} does not exist.");

        if (job.CharacterId != characterId)
        {
            throw new UnauthorizedAccessException(
                $"Job {jobId} belongs to character {job.CharacterId}, not {characterId}.");
        }

        if (requireRunning && job.State != IndustryJobState.Running)
        {
            throw new InvalidOperationException($"Job {jobId} is {job.State}, not Running.");
        }

        return job;
    }

    private async Task AdjustBalanceAsync(
        int characterId,
        Credits delta,
        LedgerReason reason,
        long? referenceId,
        DateTimeOffset at,
        CancellationToken cancellationToken)
    {
        if (delta.IsZero)
        {
            return;
        }

        long minorUnits = delta.MinorUnits;

        await _database.Database.ExecuteSqlInterpolatedAsync(
            $"UPDATE characters SET balance = balance + {minorUnits} WHERE id = {characterId}",
            cancellationToken);

        _database.LedgerEntries.Add(new LedgerEntry
        {
            CharacterId = characterId,
            DeltaCredits = delta,
            Reason = reason,
            ReferenceId = referenceId,
            CreatedAt = at,
        });
    }

    private static Credits ShareOf(Credits basis, int quantity, int totalConsumed)
    {
        if (totalConsumed <= 0 || basis.IsZero)
        {
            return Credits.Zero;
        }

        if (quantity >= totalConsumed)
        {
            return basis;
        }

        return Credits.FromMinorUnits((long)((Int128)basis.MinorUnits * quantity / totalConsumed));
    }
}
