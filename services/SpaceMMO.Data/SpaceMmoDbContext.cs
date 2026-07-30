using Microsoft.EntityFrameworkCore;
using Microsoft.EntityFrameworkCore.Metadata;
using Microsoft.EntityFrameworkCore.Storage.ValueConversion;
using SpaceMMO.Data.Conversions;
using SpaceMMO.Data.Entities;
using SpaceMMO.Domain.Economy;

namespace SpaceMMO.Data;

/// <summary>
/// The persistence model, per economy-design and ADR-0003.
/// </summary>
/// <remarks>
/// <para>
/// Three conventions applied model-wide, each for a specific reason:
/// </para>
/// <list type="bullet">
/// <item><see cref="Credits"/> stores as <c>bigint</c> minor units.</item>
/// <item>Enums store as strings, so the database is readable during hand-querying.</item>
/// <item>
/// Foreign keys use <see cref="DeleteBehavior.Restrict"/> rather than EF's default cascade. In
/// a database holding player wealth, a cascade is a way to destroy assets by accident.
/// </item>
/// </list>
/// <para>
/// Snake-case table and column naming comes from <c>UseSnakeCaseNamingConvention</c>, so the
/// schema matches the names used in the design documents.
/// </para>
/// </remarks>
public class SpaceMmoDbContext(DbContextOptions<SpaceMmoDbContext> options) : DbContext(options)
{
    /// <summary>Max length for enum-as-string columns. Longest current value is 21 characters.</summary>
    private const int EnumColumnLength = 48;

    private const int KeyLength = 64;
    private const int NameLength = 128;

    public DbSet<Account> Accounts => Set<Account>();

    public DbSet<Character> Characters => Set<Character>();

    public DbSet<Skill> Skills => Set<Skill>();

    public DbSet<CharacterSkill> CharacterSkills => Set<CharacterSkill>();

    public DbSet<ItemDef> ItemDefs => Set<ItemDef>();

    public DbSet<Recipe> Recipes => Set<Recipe>();

    public DbSet<RecipeInput> RecipeInputs => Set<RecipeInput>();

    public DbSet<Inventory> Inventories => Set<Inventory>();

    public DbSet<InventoryItem> InventoryItems => Set<InventoryItem>();

    public DbSet<ItemInstance> ItemInstances => Set<ItemInstance>();

    public DbSet<StarSystem> StarSystems => Set<StarSystem>();

    public DbSet<Body> Bodies => Set<Body>();

    public DbSet<Station> Stations => Set<Station>();

    public DbSet<ResourceNode> ResourceNodes => Set<ResourceNode>();

    public DbSet<MarketOrder> MarketOrders => Set<MarketOrder>();

    public DbSet<Trade> Trades => Set<Trade>();

    public DbSet<IndustryJob> IndustryJobs => Set<IndustryJob>();

    public DbSet<QuestDef> QuestDefs => Set<QuestDef>();

    public DbSet<QuestStep> QuestSteps => Set<QuestStep>();

    public DbSet<CharacterQuest> CharacterQuests => Set<CharacterQuest>();

    public DbSet<Bounty> Bounties => Set<Bounty>();

    public DbSet<LedgerEntry> LedgerEntries => Set<LedgerEntry>();

    public DbSet<CharacterFaucetDaily> CharacterFaucetDailies => Set<CharacterFaucetDaily>();

    public DbSet<InsurancePolicy> InsurancePolicies => Set<InsurancePolicy>();

    public DbSet<DeathRecord> DeathRecords => Set<DeathRecord>();

    protected override void ConfigureConventions(ModelConfigurationBuilder configurationBuilder)
    {
        ArgumentNullException.ThrowIfNull(configurationBuilder);

        configurationBuilder.Properties<Credits>().HaveConversion<CreditsConverter>();

        base.ConfigureConventions(configurationBuilder);
    }

    protected override void OnModelCreating(ModelBuilder modelBuilder)
    {
        ArgumentNullException.ThrowIfNull(modelBuilder);

        ConfigureUniverse(modelBuilder);
        ConfigureCharacters(modelBuilder);
        ConfigureItems(modelBuilder);
        ConfigureMarket(modelBuilder);
        ConfigureQuests(modelBuilder);
        ConfigureLedger(modelBuilder);

        ApplyEnumAsStringConvention(modelBuilder);
        ApplyRestrictDeleteConvention(modelBuilder);

        base.OnModelCreating(modelBuilder);
    }

    // ── Model-wide conventions ───────────────────────────────────────────────

    /// <summary>
    /// Stores every enum as a string rather than an integer.
    /// </summary>
    /// <remarks>
    /// Done as a sweep rather than per property so it cannot miss one. A single enum column
    /// stored as an integer while the rest are strings would be a confusing inconsistency to
    /// discover months later, mid-query.
    /// </remarks>
    private static void ApplyEnumAsStringConvention(ModelBuilder modelBuilder)
    {
        foreach (IMutableEntityType entityType in modelBuilder.Model.GetEntityTypes())
        {
            foreach (IMutableProperty property in entityType.GetProperties())
            {
                Type type = Nullable.GetUnderlyingType(property.ClrType) ?? property.ClrType;

                if (!type.IsEnum)
                {
                    continue;
                }

                Type converterType = typeof(EnumToStringConverter<>).MakeGenericType(type);
                property.SetValueConverter((ValueConverter)Activator.CreateInstance(converterType)!);
                property.SetMaxLength(EnumColumnLength);
            }
        }
    }

    /// <summary>
    /// Replaces EF's default cascade delete with restrict on every relationship.
    /// </summary>
    /// <remarks>
    /// Deleting a character should not silently erase their ledger history, and deleting an
    /// item definition should fail loudly rather than removing every instance players own.
    /// Anything genuinely worth cascading can opt in explicitly.
    /// </remarks>
    private static void ApplyRestrictDeleteConvention(ModelBuilder modelBuilder)
    {
        foreach (IMutableForeignKey foreignKey in modelBuilder.Model
            .GetEntityTypes()
            .SelectMany(e => e.GetForeignKeys()))
        {
            foreignKey.DeleteBehavior = DeleteBehavior.Restrict;
        }
    }

    // ── Universe ─────────────────────────────────────────────────────────────

    private static void ConfigureUniverse(ModelBuilder modelBuilder)
    {
        modelBuilder.Entity<StarSystem>(entity =>
        {
            entity.Property(e => e.Key).HasMaxLength(KeyLength);
            entity.Property(e => e.Name).HasMaxLength(NameLength);
            entity.HasIndex(e => e.Key).IsUnique();

            // Systems are located by position for nearest-neighbour and warp-range queries.
            entity.HasIndex(e => new { e.GalaxyX, e.GalaxyY, e.GalaxyZ });
        });

        modelBuilder.Entity<Body>(entity =>
        {
            entity.Property(e => e.Key).HasMaxLength(KeyLength);
            entity.Property(e => e.Name).HasMaxLength(NameLength);
            entity.HasIndex(e => e.Key).IsUnique();
            entity.HasIndex(e => e.StarSystemId);

            entity.HasOne(e => e.StarSystem)
                .WithMany(s => s.Bodies)
                .HasForeignKey(e => e.StarSystemId);

            entity.ToTable(t => t.HasCheckConstraint(
                "ck_bodies_radius_positive", "radius_km > 0"));
        });

        modelBuilder.Entity<Station>(entity =>
        {
            entity.Property(e => e.Key).HasMaxLength(KeyLength);
            entity.Property(e => e.Name).HasMaxLength(NameLength);
            entity.HasIndex(e => e.Key).IsUnique();
            entity.HasIndex(e => e.StarSystemId);

            entity.HasOne(e => e.StarSystem)
                .WithMany(s => s.Stations)
                .HasForeignKey(e => e.StarSystemId);

            entity.HasOne(e => e.Body)
                .WithMany(b => b.Stations)
                .HasForeignKey(e => e.BodyId);
        });

        modelBuilder.Entity<ResourceNode>(entity =>
        {
            // The gathering hot path: "what can I mine on this body?"
            entity.HasIndex(e => new { e.BodyId, e.ItemDefId });
            entity.HasIndex(e => e.RespawnAt);

            entity.ToTable(t =>
            {
                t.HasCheckConstraint(
                    "ck_resource_nodes_remaining_in_range",
                    "quantity_remaining >= 0 AND quantity_remaining <= quantity_max");
                t.HasCheckConstraint("ck_resource_nodes_max_positive", "quantity_max > 0");
            });
        });
    }

    // ── Characters and progression ───────────────────────────────────────────

    private static void ConfigureCharacters(ModelBuilder modelBuilder)
    {
        modelBuilder.Entity<Account>(entity =>
        {
            entity.Property(e => e.Email).HasMaxLength(320);
            entity.Property(e => e.PasswordHash).HasMaxLength(256);

            // Case-insensitive uniqueness: the database is C-collated, so fold explicitly
            // rather than relying on collation behaviour.
            entity.HasIndex(e => e.Email).IsUnique();
        });

        modelBuilder.Entity<Character>(entity =>
        {
            entity.Property(e => e.Name).HasMaxLength(NameLength);
            entity.HasIndex(e => e.Name).IsUnique();
            entity.HasIndex(e => e.AccountId);

            entity.HasOne(e => e.Account)
                .WithMany(a => a.Characters)
                .HasForeignKey(e => e.AccountId);

            // Faction is intentionally absent from the entity, not merely unmapped: it is
            // derived from Race via Races.FactionFor, so no row can contradict it.
            entity.HasOne(e => e.HomeBody)
                .WithMany()
                .HasForeignKey(e => e.HomeBodyId);
        });

        modelBuilder.Entity<Skill>(entity =>
        {
            entity.Property(e => e.Key).HasMaxLength(KeyLength);
            entity.Property(e => e.Name).HasMaxLength(NameLength);
            entity.HasIndex(e => e.Key).IsUnique();
        });

        modelBuilder.Entity<CharacterSkill>(entity =>
        {
            entity.HasKey(e => new { e.CharacterId, e.SkillId });

            entity.HasOne(e => e.Character)
                .WithMany(c => c.Skills)
                .HasForeignKey(e => e.CharacterId);

            entity.HasOne(e => e.Skill)
                .WithMany()
                .HasForeignKey(e => e.SkillId);

            // Level is derived from XP (ADR-0004), so negative XP is not a low level — it is
            // a bug that would throw the moment anything read it.
            entity.ToTable(t => t.HasCheckConstraint("ck_character_skills_xp_non_negative", "xp >= 0"));
        });
    }

    // ── Items, recipes, inventories ──────────────────────────────────────────

    private static void ConfigureItems(ModelBuilder modelBuilder)
    {
        modelBuilder.Entity<ItemDef>(entity =>
        {
            entity.Property(e => e.Key).HasMaxLength(KeyLength);
            entity.Property(e => e.Name).HasMaxLength(NameLength);
            entity.HasIndex(e => e.Key).IsUnique();

            // Derived from Category; a stored copy could contradict it.
            entity.Ignore(e => e.IsStackable);

            entity.ToTable(t => t.HasCheckConstraint(
                "ck_item_defs_volume_positive", "volume_m3 > 0"));
        });

        modelBuilder.Entity<Recipe>(entity =>
        {
            entity.Property(e => e.Key).HasMaxLength(KeyLength);
            entity.HasIndex(e => e.Key).IsUnique();
            entity.HasIndex(e => e.OutputItemDefId);

            entity.HasOne(e => e.OutputItemDef).WithMany().HasForeignKey(e => e.OutputItemDefId);
            entity.HasOne(e => e.Skill).WithMany().HasForeignKey(e => e.SkillId);
            entity.HasOne(e => e.RequiredToolItemDef).WithMany().HasForeignKey(e => e.RequiredToolItemDefId);

            entity.ToTable(t =>
            {
                t.HasCheckConstraint("ck_recipes_output_positive", "output_quantity > 0");
                t.HasCheckConstraint("ck_recipes_level_in_range", "required_level BETWEEN 1 AND 99");
                t.HasCheckConstraint("ck_recipes_job_seconds_positive", "job_seconds > 0");
            });
        });

        modelBuilder.Entity<RecipeInput>(entity =>
        {
            entity.HasKey(e => new { e.RecipeId, e.ItemDefId });

            entity.HasOne(e => e.Recipe)
                .WithMany(r => r.Inputs)
                .HasForeignKey(e => e.RecipeId);

            entity.HasOne(e => e.ItemDef).WithMany().HasForeignKey(e => e.ItemDefId);

            entity.ToTable(t => t.HasCheckConstraint("ck_recipe_inputs_quantity_positive", "quantity > 0"));
        });

        modelBuilder.Entity<Inventory>(entity =>
        {
            entity.HasIndex(e => e.CharacterId);
            entity.HasIndex(e => e.StationId);

            entity.HasOne(e => e.Character)
                .WithMany(c => c.Inventories)
                .HasForeignKey(e => e.CharacterId);

            entity.HasOne(e => e.Station).WithMany().HasForeignKey(e => e.StationId);

            entity.HasOne(e => e.ShipItemInstance)
                .WithMany()
                .HasForeignKey(e => e.ShipItemInstanceId);

            // One station hangar per character per station. Without this, two concurrent
            // get-or-create calls would each create a hangar and the character's goods would end
            // up split across two containers, one of which nothing would ever look in.
            // Ship holds and carried inventories have a null station id, and Postgres treats
            // NULLs as distinct, so this correctly permits many of those.
            entity.HasIndex(e => new { e.CharacterId, e.StationId, e.Kind }).IsUnique();

            entity.ToTable(t => t.HasCheckConstraint(
                "ck_inventories_capacity_non_negative", "capacity_m3 >= 0"));
        });

        modelBuilder.Entity<InventoryItem>(entity =>
        {
            entity.HasKey(e => new { e.InventoryId, e.ItemDefId });

            entity.HasOne(e => e.Inventory)
                .WithMany(i => i.StackedItems)
                .HasForeignKey(e => e.InventoryId);

            entity.HasOne(e => e.ItemDef).WithMany().HasForeignKey(e => e.ItemDefId);

            // A zero-quantity stack should be deleted, not stored. Enforcing this keeps
            // "does the player have any?" from needing to also ask "but is it more than none?"
            entity.ToTable(t => t.HasCheckConstraint(
                "ck_inventory_items_quantity_positive", "quantity > 0"));
        });

        modelBuilder.Entity<ItemInstance>(entity =>
        {
            entity.HasIndex(e => e.InventoryId);
            entity.HasIndex(e => e.ItemDefId);

            entity.HasOne(e => e.ItemDef).WithMany().HasForeignKey(e => e.ItemDefId);

            entity.HasOne(e => e.Inventory)
                .WithMany(i => i.Instances)
                .HasForeignKey(e => e.InventoryId);

            entity.ToTable(t =>
            {
                t.HasCheckConstraint(
                    "ck_item_instances_condition_in_range", "condition BETWEEN 0 AND 100");

                // Insurance payouts are pegged to this value, so a negative one would be an
                // exploit rather than merely odd data (ADR-0006).
                t.HasCheckConstraint(
                    "ck_item_instances_acquisition_non_negative", "acquisition_value >= 0");
            });
        });
    }

    // ── Market and industry ──────────────────────────────────────────────────

    private static void ConfigureMarket(ModelBuilder modelBuilder)
    {
        modelBuilder.Entity<MarketOrder>(entity =>
        {
            entity.HasOne(e => e.Station).WithMany().HasForeignKey(e => e.StationId);
            entity.HasOne(e => e.ItemDef).WithMany().HasForeignKey(e => e.ItemDefId);
            entity.HasOne(e => e.Character).WithMany().HasForeignKey(e => e.CharacterId);

            // The order book query, and the most performance-critical index in the schema:
            // matching walks this in price order for one station and item.
            entity.HasIndex(e => new { e.StationId, e.ItemDefId, e.Side, e.Price })
                .HasFilter("quantity_remaining > 0 AND cancelled_at IS NULL");

            entity.HasIndex(e => e.CharacterId);
            entity.HasIndex(e => e.ExpiresAt).HasFilter("cancelled_at IS NULL");

            entity.ToTable(t =>
            {
                t.HasCheckConstraint("ck_market_orders_price_positive", "price > 0");
                t.HasCheckConstraint(
                    "ck_market_orders_quantity_original_positive", "quantity_original > 0");
                t.HasCheckConstraint(
                    "ck_market_orders_quantity_remaining_in_range",
                    "quantity_remaining >= 0 AND quantity_remaining <= quantity_original");

                t.HasCheckConstraint(
                    "ck_market_orders_escrow_non_negative", "escrowed_credits >= 0");

                t.HasCheckConstraint(
                    "ck_market_orders_reserved_non_negative", "reserved_quantity >= 0");

                // Escrow belongs to buy orders and reserved goods to sell orders, never the
                // other way round. Enforced here because a sell order holding credits would
                // mean money was locked with nothing to release it.
                t.HasCheckConstraint(
                    "ck_market_orders_escrow_matches_side",
                    "(side = 'Buy' AND reserved_quantity = 0) OR "
                    + "(side = 'Sell' AND escrowed_credits = 0)");
            });
        });

        modelBuilder.Entity<Trade>(entity =>
        {
            entity.HasOne(e => e.ItemDef).WithMany().HasForeignKey(e => e.ItemDefId);

            // Price history for market UI and for EconSim's price-convergence invariant.
            entity.HasIndex(e => new { e.ItemDefId, e.ExecutedAt });
            entity.HasIndex(e => new { e.StationId, e.ExecutedAt });

            entity.ToTable(t =>
            {
                t.HasCheckConstraint("ck_trades_quantity_positive", "quantity > 0");
                t.HasCheckConstraint("ck_trades_price_positive", "price > 0");
                t.HasCheckConstraint("ck_trades_sales_tax_non_negative", "sales_tax >= 0");
            });
        });

        modelBuilder.Entity<IndustryJob>(entity =>
        {
            entity.HasOne(e => e.Character).WithMany().HasForeignKey(e => e.CharacterId);
            entity.HasOne(e => e.Recipe).WithMany().HasForeignKey(e => e.RecipeId);
            entity.HasOne(e => e.Station).WithMany().HasForeignKey(e => e.StationId);

            entity.HasIndex(e => new { e.CharacterId, e.State });

            // Drives the completion sweep, so it is filtered to jobs that can still complete.
            entity.HasIndex(e => e.CompletesAt).HasFilter("state = 'Running'");

            entity.ToTable(t => t.HasCheckConstraint("ck_industry_jobs_runs_positive", "runs > 0"));
        });
    }

    // ── Quests ───────────────────────────────────────────────────────────────

    private static void ConfigureQuests(ModelBuilder modelBuilder)
    {
        modelBuilder.Entity<QuestDef>(entity =>
        {
            entity.Property(e => e.Key).HasMaxLength(KeyLength);
            entity.Property(e => e.Name).HasMaxLength(NameLength);
            entity.Property(e => e.CareerKey).HasMaxLength(KeyLength);
            entity.HasIndex(e => e.Key).IsUnique();

            entity.HasOne(e => e.PrerequisiteQuestDef)
                .WithMany()
                .HasForeignKey(e => e.PrerequisiteQuestDefId);

            entity.HasOne(e => e.RewardSkill).WithMany().HasForeignKey(e => e.RewardSkillId);

            entity.ToTable(t =>
            {
                t.HasCheckConstraint("ck_quest_defs_reward_non_negative", "reward_credits >= 0");
                t.HasCheckConstraint("ck_quest_defs_xp_non_negative", "reward_xp >= 0");
                t.HasCheckConstraint(
                    "ck_quest_defs_cooldown_positive",
                    "cooldown_seconds IS NULL OR cooldown_seconds > 0");
            });
        });

        modelBuilder.Entity<QuestStep>(entity =>
        {
            entity.Property(e => e.Description).HasMaxLength(512);
            entity.Property(e => e.TargetKey).HasMaxLength(KeyLength);

            entity.HasOne(e => e.QuestDef)
                .WithMany(q => q.Steps)
                .HasForeignKey(e => e.QuestDefId);

            entity.HasIndex(e => new { e.QuestDefId, e.Ordinal }).IsUnique();

            entity.ToTable(t =>
            {
                t.HasCheckConstraint("ck_quest_steps_ordinal_positive", "ordinal > 0");
                t.HasCheckConstraint("ck_quest_steps_quantity_positive", "quantity > 0");
            });
        });

        modelBuilder.Entity<CharacterQuest>(entity =>
        {
            entity.HasOne(e => e.Character).WithMany().HasForeignKey(e => e.CharacterId);
            entity.HasOne(e => e.QuestDef).WithMany().HasForeignKey(e => e.QuestDefId);

            // Deliberately NOT unique: repeatable sidequests produce one row per completion,
            // and that history is what enforces the per-quest cooldown.
            entity.HasIndex(e => new { e.CharacterId, e.QuestDefId, e.State });

            entity.ToTable(t => t.HasCheckConstraint(
                "ck_character_quests_progress_non_negative",
                "step_ordinal > 0 AND step_progress >= 0"));
        });

        modelBuilder.Entity<Bounty>(entity =>
        {
            entity.HasOne(e => e.TargetCharacter).WithMany().HasForeignKey(e => e.TargetCharacterId);

            entity.HasIndex(e => e.TargetCharacterId).HasFilter("claimed_at IS NULL");
            entity.HasIndex(e => e.PosterCharacterId);

            entity.ToTable(t => t.HasCheckConstraint("ck_bounties_amount_positive", "amount > 0"));
        });
    }

    // ── Ledger, faucet, insurance, deaths ────────────────────────────────────

    private static void ConfigureLedger(ModelBuilder modelBuilder)
    {
        modelBuilder.Entity<LedgerEntry>(entity =>
        {
            entity.HasOne(e => e.Character).WithMany().HasForeignKey(e => e.CharacterId);

            // Balance reconciliation: sum a character's deltas in time order.
            entity.HasIndex(e => new { e.CharacterId, e.CreatedAt });

            // EconSim's faucet and sink attribution is a GROUP BY over these two columns.
            entity.HasIndex(e => new { e.Reason, e.CreatedAt });
        });

        modelBuilder.Entity<CharacterFaucetDaily>(entity =>
        {
            entity.HasKey(e => new { e.CharacterId, e.UtcDate });

            entity.HasOne(e => e.Character).WithMany().HasForeignKey(e => e.CharacterId);

            // Named explicitly: EF would pluralise this to "character_faucet_dailies", which
            // is both ugly and not the name used in economy-design §2b.
            entity.ToTable("character_faucet_daily", t => t.HasCheckConstraint(
                "ck_character_faucet_daily_non_negative", "credits_granted >= 0"));
        });

        modelBuilder.Entity<InsurancePolicy>(entity =>
        {
            entity.HasOne(e => e.ItemInstance).WithMany().HasForeignKey(e => e.ItemInstanceId);
            entity.HasOne(e => e.Character).WithMany().HasForeignKey(e => e.CharacterId);

            // At most one live policy per hull. A partial unique index expresses this exactly:
            // stacking policies would multiply the payout on a single loss.
            entity.HasIndex(e => e.ItemInstanceId)
                .IsUnique()
                .HasFilter("claimed_at IS NULL");

            entity.HasIndex(e => e.CharacterId);

            entity.ToTable(t =>
            {
                t.HasCheckConstraint(
                    "ck_insurance_policies_premium_non_negative", "premium_paid >= 0");
                t.HasCheckConstraint(
                    "ck_insurance_policies_insured_value_non_negative", "insured_value >= 0");

                // The core anti-fraud invariant, enforced in the database as well as in code:
                // a payout can never exceed the value it was pegged to (ADR-0006).
                t.HasCheckConstraint(
                    "ck_insurance_policies_payout_below_insured_value",
                    "payout_amount IS NULL OR payout_amount < insured_value");
            });
        });

        modelBuilder.Entity<DeathRecord>(entity =>
        {
            entity.HasOne(e => e.Character).WithMany().HasForeignKey(e => e.CharacterId);

            entity.HasIndex(e => new { e.CharacterId, e.OccurredAt });
            entity.HasIndex(e => e.KillerCharacterId).HasFilter("killer_character_id IS NOT NULL");
        });
    }
}
