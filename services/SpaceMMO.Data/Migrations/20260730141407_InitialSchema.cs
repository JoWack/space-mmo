using System;
using Microsoft.EntityFrameworkCore.Migrations;
using Npgsql.EntityFrameworkCore.PostgreSQL.Metadata;

#nullable disable

namespace SpaceMMO.Data.Migrations
{
    /// <inheritdoc />
    public partial class InitialSchema : Migration
    {
        /// <inheritdoc />
        protected override void Up(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.CreateTable(
                name: "accounts",
                columns: table => new
                {
                    id = table.Column<int>(type: "integer", nullable: false)
                        .Annotation("Npgsql:ValueGenerationStrategy", NpgsqlValueGenerationStrategy.IdentityByDefaultColumn),
                    email = table.Column<string>(type: "character varying(320)", maxLength: 320, nullable: false),
                    password_hash = table.Column<string>(type: "character varying(256)", maxLength: 256, nullable: false),
                    created_at = table.Column<DateTimeOffset>(type: "timestamp with time zone", nullable: false)
                },
                constraints: table =>
                {
                    table.PrimaryKey("pk_accounts", x => x.id);
                });

            migrationBuilder.CreateTable(
                name: "item_defs",
                columns: table => new
                {
                    id = table.Column<int>(type: "integer", nullable: false)
                        .Annotation("Npgsql:ValueGenerationStrategy", NpgsqlValueGenerationStrategy.IdentityByDefaultColumn),
                    key = table.Column<string>(type: "character varying(64)", maxLength: 64, nullable: false),
                    name = table.Column<string>(type: "character varying(128)", maxLength: 128, nullable: false),
                    category = table.Column<string>(type: "character varying(48)", maxLength: 48, nullable: false),
                    volume_m3 = table.Column<double>(type: "double precision", nullable: false)
                },
                constraints: table =>
                {
                    table.PrimaryKey("pk_item_defs", x => x.id);
                    table.CheckConstraint("ck_item_defs_volume_positive", "volume_m3 > 0");
                });

            migrationBuilder.CreateTable(
                name: "skills",
                columns: table => new
                {
                    id = table.Column<int>(type: "integer", nullable: false)
                        .Annotation("Npgsql:ValueGenerationStrategy", NpgsqlValueGenerationStrategy.IdentityByDefaultColumn),
                    key = table.Column<string>(type: "character varying(64)", maxLength: 64, nullable: false),
                    name = table.Column<string>(type: "character varying(128)", maxLength: 128, nullable: false),
                    category = table.Column<string>(type: "character varying(48)", maxLength: 48, nullable: false)
                },
                constraints: table =>
                {
                    table.PrimaryKey("pk_skills", x => x.id);
                });

            migrationBuilder.CreateTable(
                name: "star_systems",
                columns: table => new
                {
                    id = table.Column<int>(type: "integer", nullable: false)
                        .Annotation("Npgsql:ValueGenerationStrategy", NpgsqlValueGenerationStrategy.IdentityByDefaultColumn),
                    key = table.Column<string>(type: "character varying(64)", maxLength: 64, nullable: false),
                    name = table.Column<string>(type: "character varying(128)", maxLength: 128, nullable: false),
                    galaxy_x = table.Column<long>(type: "bigint", nullable: false),
                    galaxy_y = table.Column<long>(type: "bigint", nullable: false),
                    galaxy_z = table.Column<long>(type: "bigint", nullable: false),
                    seed = table.Column<long>(type: "bigint", nullable: false),
                    generator_version = table.Column<int>(type: "integer", nullable: false),
                    security_level = table.Column<string>(type: "character varying(48)", maxLength: 48, nullable: false)
                },
                constraints: table =>
                {
                    table.PrimaryKey("pk_star_systems", x => x.id);
                });

            migrationBuilder.CreateTable(
                name: "trades",
                columns: table => new
                {
                    id = table.Column<long>(type: "bigint", nullable: false)
                        .Annotation("Npgsql:ValueGenerationStrategy", NpgsqlValueGenerationStrategy.IdentityByDefaultColumn),
                    buy_order_id = table.Column<long>(type: "bigint", nullable: false),
                    sell_order_id = table.Column<long>(type: "bigint", nullable: false),
                    station_id = table.Column<int>(type: "integer", nullable: false),
                    star_system_id = table.Column<int>(type: "integer", nullable: false),
                    item_def_id = table.Column<int>(type: "integer", nullable: false),
                    buyer_character_id = table.Column<int>(type: "integer", nullable: false),
                    seller_character_id = table.Column<int>(type: "integer", nullable: false),
                    quantity = table.Column<int>(type: "integer", nullable: false),
                    price = table.Column<long>(type: "bigint", nullable: false),
                    sales_tax = table.Column<long>(type: "bigint", nullable: false),
                    executed_at = table.Column<DateTimeOffset>(type: "timestamp with time zone", nullable: false)
                },
                constraints: table =>
                {
                    table.PrimaryKey("pk_trades", x => x.id);
                    table.CheckConstraint("ck_trades_price_positive", "price > 0");
                    table.CheckConstraint("ck_trades_quantity_positive", "quantity > 0");
                    table.CheckConstraint("ck_trades_sales_tax_non_negative", "sales_tax >= 0");
                    table.ForeignKey(
                        name: "fk_trades_item_defs_item_def_id",
                        column: x => x.item_def_id,
                        principalTable: "item_defs",
                        principalColumn: "id",
                        onDelete: ReferentialAction.Restrict);
                });

            migrationBuilder.CreateTable(
                name: "quest_defs",
                columns: table => new
                {
                    id = table.Column<int>(type: "integer", nullable: false)
                        .Annotation("Npgsql:ValueGenerationStrategy", NpgsqlValueGenerationStrategy.IdentityByDefaultColumn),
                    key = table.Column<string>(type: "character varying(64)", maxLength: 64, nullable: false),
                    name = table.Column<string>(type: "character varying(128)", maxLength: 128, nullable: false),
                    kind = table.Column<string>(type: "character varying(48)", maxLength: 48, nullable: false),
                    prerequisite_quest_def_id = table.Column<int>(type: "integer", nullable: true),
                    career_key = table.Column<string>(type: "character varying(64)", maxLength: 64, nullable: true),
                    reward_credits = table.Column<long>(type: "bigint", nullable: false),
                    reward_skill_id = table.Column<int>(type: "integer", nullable: true),
                    reward_xp = table.Column<long>(type: "bigint", nullable: false),
                    cooldown_seconds = table.Column<int>(type: "integer", nullable: true)
                },
                constraints: table =>
                {
                    table.PrimaryKey("pk_quest_defs", x => x.id);
                    table.CheckConstraint("ck_quest_defs_cooldown_positive", "cooldown_seconds IS NULL OR cooldown_seconds > 0");
                    table.CheckConstraint("ck_quest_defs_reward_non_negative", "reward_credits >= 0");
                    table.CheckConstraint("ck_quest_defs_xp_non_negative", "reward_xp >= 0");
                    table.ForeignKey(
                        name: "fk_quest_defs_quest_defs_prerequisite_quest_def_id",
                        column: x => x.prerequisite_quest_def_id,
                        principalTable: "quest_defs",
                        principalColumn: "id",
                        onDelete: ReferentialAction.Restrict);
                    table.ForeignKey(
                        name: "fk_quest_defs_skills_reward_skill_id",
                        column: x => x.reward_skill_id,
                        principalTable: "skills",
                        principalColumn: "id",
                        onDelete: ReferentialAction.Restrict);
                });

            migrationBuilder.CreateTable(
                name: "recipes",
                columns: table => new
                {
                    id = table.Column<int>(type: "integer", nullable: false)
                        .Annotation("Npgsql:ValueGenerationStrategy", NpgsqlValueGenerationStrategy.IdentityByDefaultColumn),
                    key = table.Column<string>(type: "character varying(64)", maxLength: 64, nullable: false),
                    output_item_def_id = table.Column<int>(type: "integer", nullable: false),
                    output_quantity = table.Column<int>(type: "integer", nullable: false),
                    skill_id = table.Column<int>(type: "integer", nullable: false),
                    required_level = table.Column<int>(type: "integer", nullable: false),
                    job_seconds = table.Column<int>(type: "integer", nullable: false),
                    xp_per_run = table.Column<long>(type: "bigint", nullable: false),
                    required_tool_item_def_id = table.Column<int>(type: "integer", nullable: true)
                },
                constraints: table =>
                {
                    table.PrimaryKey("pk_recipes", x => x.id);
                    table.CheckConstraint("ck_recipes_job_seconds_positive", "job_seconds > 0");
                    table.CheckConstraint("ck_recipes_level_in_range", "required_level BETWEEN 1 AND 99");
                    table.CheckConstraint("ck_recipes_output_positive", "output_quantity > 0");
                    table.ForeignKey(
                        name: "fk_recipes_item_defs_output_item_def_id",
                        column: x => x.output_item_def_id,
                        principalTable: "item_defs",
                        principalColumn: "id",
                        onDelete: ReferentialAction.Restrict);
                    table.ForeignKey(
                        name: "fk_recipes_item_defs_required_tool_item_def_id",
                        column: x => x.required_tool_item_def_id,
                        principalTable: "item_defs",
                        principalColumn: "id",
                        onDelete: ReferentialAction.Restrict);
                    table.ForeignKey(
                        name: "fk_recipes_skills_skill_id",
                        column: x => x.skill_id,
                        principalTable: "skills",
                        principalColumn: "id",
                        onDelete: ReferentialAction.Restrict);
                });

            migrationBuilder.CreateTable(
                name: "bodies",
                columns: table => new
                {
                    id = table.Column<int>(type: "integer", nullable: false)
                        .Annotation("Npgsql:ValueGenerationStrategy", NpgsqlValueGenerationStrategy.IdentityByDefaultColumn),
                    key = table.Column<string>(type: "character varying(64)", maxLength: 64, nullable: false),
                    name = table.Column<string>(type: "character varying(128)", maxLength: 128, nullable: false),
                    star_system_id = table.Column<int>(type: "integer", nullable: false),
                    kind = table.Column<string>(type: "character varying(48)", maxLength: 48, nullable: false),
                    security_level = table.Column<string>(type: "character varying(48)", maxLength: 48, nullable: false),
                    radius_km = table.Column<double>(type: "double precision", nullable: false)
                },
                constraints: table =>
                {
                    table.PrimaryKey("pk_bodies", x => x.id);
                    table.CheckConstraint("ck_bodies_radius_positive", "radius_km > 0");
                    table.ForeignKey(
                        name: "fk_bodies_star_systems_star_system_id",
                        column: x => x.star_system_id,
                        principalTable: "star_systems",
                        principalColumn: "id",
                        onDelete: ReferentialAction.Restrict);
                });

            migrationBuilder.CreateTable(
                name: "quest_steps",
                columns: table => new
                {
                    id = table.Column<int>(type: "integer", nullable: false)
                        .Annotation("Npgsql:ValueGenerationStrategy", NpgsqlValueGenerationStrategy.IdentityByDefaultColumn),
                    quest_def_id = table.Column<int>(type: "integer", nullable: false),
                    ordinal = table.Column<int>(type: "integer", nullable: false),
                    description = table.Column<string>(type: "character varying(512)", maxLength: 512, nullable: false),
                    objective_type = table.Column<string>(type: "character varying(48)", maxLength: 48, nullable: false),
                    target_key = table.Column<string>(type: "character varying(64)", maxLength: 64, nullable: false),
                    quantity = table.Column<int>(type: "integer", nullable: false)
                },
                constraints: table =>
                {
                    table.PrimaryKey("pk_quest_steps", x => x.id);
                    table.CheckConstraint("ck_quest_steps_ordinal_positive", "ordinal > 0");
                    table.CheckConstraint("ck_quest_steps_quantity_positive", "quantity > 0");
                    table.ForeignKey(
                        name: "fk_quest_steps_quest_defs_quest_def_id",
                        column: x => x.quest_def_id,
                        principalTable: "quest_defs",
                        principalColumn: "id",
                        onDelete: ReferentialAction.Restrict);
                });

            migrationBuilder.CreateTable(
                name: "recipe_inputs",
                columns: table => new
                {
                    recipe_id = table.Column<int>(type: "integer", nullable: false),
                    item_def_id = table.Column<int>(type: "integer", nullable: false),
                    quantity = table.Column<int>(type: "integer", nullable: false)
                },
                constraints: table =>
                {
                    table.PrimaryKey("pk_recipe_inputs", x => new { x.recipe_id, x.item_def_id });
                    table.CheckConstraint("ck_recipe_inputs_quantity_positive", "quantity > 0");
                    table.ForeignKey(
                        name: "fk_recipe_inputs_item_defs_item_def_id",
                        column: x => x.item_def_id,
                        principalTable: "item_defs",
                        principalColumn: "id",
                        onDelete: ReferentialAction.Restrict);
                    table.ForeignKey(
                        name: "fk_recipe_inputs_recipes_recipe_id",
                        column: x => x.recipe_id,
                        principalTable: "recipes",
                        principalColumn: "id",
                        onDelete: ReferentialAction.Restrict);
                });

            migrationBuilder.CreateTable(
                name: "characters",
                columns: table => new
                {
                    id = table.Column<int>(type: "integer", nullable: false)
                        .Annotation("Npgsql:ValueGenerationStrategy", NpgsqlValueGenerationStrategy.IdentityByDefaultColumn),
                    account_id = table.Column<int>(type: "integer", nullable: false),
                    name = table.Column<string>(type: "character varying(128)", maxLength: 128, nullable: false),
                    race = table.Column<string>(type: "character varying(48)", maxLength: 48, nullable: false),
                    home_body_id = table.Column<int>(type: "integer", nullable: false),
                    balance = table.Column<long>(type: "bigint", nullable: false),
                    created_at = table.Column<DateTimeOffset>(type: "timestamp with time zone", nullable: false)
                },
                constraints: table =>
                {
                    table.PrimaryKey("pk_characters", x => x.id);
                    table.ForeignKey(
                        name: "fk_characters_accounts_account_id",
                        column: x => x.account_id,
                        principalTable: "accounts",
                        principalColumn: "id",
                        onDelete: ReferentialAction.Restrict);
                    table.ForeignKey(
                        name: "fk_characters_bodies_home_body_id",
                        column: x => x.home_body_id,
                        principalTable: "bodies",
                        principalColumn: "id",
                        onDelete: ReferentialAction.Restrict);
                });

            migrationBuilder.CreateTable(
                name: "resource_nodes",
                columns: table => new
                {
                    id = table.Column<long>(type: "bigint", nullable: false)
                        .Annotation("Npgsql:ValueGenerationStrategy", NpgsqlValueGenerationStrategy.IdentityByDefaultColumn),
                    star_system_id = table.Column<int>(type: "integer", nullable: false),
                    body_id = table.Column<int>(type: "integer", nullable: false),
                    item_def_id = table.Column<int>(type: "integer", nullable: false),
                    quantity_remaining = table.Column<int>(type: "integer", nullable: false),
                    quantity_max = table.Column<int>(type: "integer", nullable: false),
                    respawn_at = table.Column<DateTimeOffset>(type: "timestamp with time zone", nullable: true)
                },
                constraints: table =>
                {
                    table.PrimaryKey("pk_resource_nodes", x => x.id);
                    table.CheckConstraint("ck_resource_nodes_max_positive", "quantity_max > 0");
                    table.CheckConstraint("ck_resource_nodes_remaining_in_range", "quantity_remaining >= 0 AND quantity_remaining <= quantity_max");
                    table.ForeignKey(
                        name: "fk_resource_nodes_bodies_body_id",
                        column: x => x.body_id,
                        principalTable: "bodies",
                        principalColumn: "id",
                        onDelete: ReferentialAction.Restrict);
                    table.ForeignKey(
                        name: "fk_resource_nodes_item_defs_item_def_id",
                        column: x => x.item_def_id,
                        principalTable: "item_defs",
                        principalColumn: "id",
                        onDelete: ReferentialAction.Restrict);
                    table.ForeignKey(
                        name: "fk_resource_nodes_star_systems_star_system_id",
                        column: x => x.star_system_id,
                        principalTable: "star_systems",
                        principalColumn: "id",
                        onDelete: ReferentialAction.Restrict);
                });

            migrationBuilder.CreateTable(
                name: "stations",
                columns: table => new
                {
                    id = table.Column<int>(type: "integer", nullable: false)
                        .Annotation("Npgsql:ValueGenerationStrategy", NpgsqlValueGenerationStrategy.IdentityByDefaultColumn),
                    key = table.Column<string>(type: "character varying(64)", maxLength: 64, nullable: false),
                    name = table.Column<string>(type: "character varying(128)", maxLength: 128, nullable: false),
                    star_system_id = table.Column<int>(type: "integer", nullable: false),
                    body_id = table.Column<int>(type: "integer", nullable: true),
                    kind = table.Column<string>(type: "character varying(48)", maxLength: 48, nullable: false)
                },
                constraints: table =>
                {
                    table.PrimaryKey("pk_stations", x => x.id);
                    table.ForeignKey(
                        name: "fk_stations_bodies_body_id",
                        column: x => x.body_id,
                        principalTable: "bodies",
                        principalColumn: "id",
                        onDelete: ReferentialAction.Restrict);
                    table.ForeignKey(
                        name: "fk_stations_star_systems_star_system_id",
                        column: x => x.star_system_id,
                        principalTable: "star_systems",
                        principalColumn: "id",
                        onDelete: ReferentialAction.Restrict);
                });

            migrationBuilder.CreateTable(
                name: "bounties",
                columns: table => new
                {
                    id = table.Column<long>(type: "bigint", nullable: false)
                        .Annotation("Npgsql:ValueGenerationStrategy", NpgsqlValueGenerationStrategy.IdentityByDefaultColumn),
                    target_character_id = table.Column<int>(type: "integer", nullable: false),
                    poster_character_id = table.Column<int>(type: "integer", nullable: false),
                    amount = table.Column<long>(type: "bigint", nullable: false),
                    posted_at = table.Column<DateTimeOffset>(type: "timestamp with time zone", nullable: false),
                    claimed_by_character_id = table.Column<int>(type: "integer", nullable: true),
                    claimed_at = table.Column<DateTimeOffset>(type: "timestamp with time zone", nullable: true),
                    claimed_by_death_record_id = table.Column<long>(type: "bigint", nullable: true)
                },
                constraints: table =>
                {
                    table.PrimaryKey("pk_bounties", x => x.id);
                    table.CheckConstraint("ck_bounties_amount_positive", "amount > 0");
                    table.ForeignKey(
                        name: "fk_bounties_characters_target_character_id",
                        column: x => x.target_character_id,
                        principalTable: "characters",
                        principalColumn: "id",
                        onDelete: ReferentialAction.Restrict);
                });

            migrationBuilder.CreateTable(
                name: "character_faucet_daily",
                columns: table => new
                {
                    character_id = table.Column<int>(type: "integer", nullable: false),
                    utc_date = table.Column<DateOnly>(type: "date", nullable: false),
                    credits_granted = table.Column<long>(type: "bigint", nullable: false)
                },
                constraints: table =>
                {
                    table.PrimaryKey("pk_character_faucet_daily", x => new { x.character_id, x.utc_date });
                    table.CheckConstraint("ck_character_faucet_daily_non_negative", "credits_granted >= 0");
                    table.ForeignKey(
                        name: "fk_character_faucet_daily_characters_character_id",
                        column: x => x.character_id,
                        principalTable: "characters",
                        principalColumn: "id",
                        onDelete: ReferentialAction.Restrict);
                });

            migrationBuilder.CreateTable(
                name: "character_quests",
                columns: table => new
                {
                    id = table.Column<long>(type: "bigint", nullable: false)
                        .Annotation("Npgsql:ValueGenerationStrategy", NpgsqlValueGenerationStrategy.IdentityByDefaultColumn),
                    character_id = table.Column<int>(type: "integer", nullable: false),
                    quest_def_id = table.Column<int>(type: "integer", nullable: false),
                    state = table.Column<string>(type: "character varying(48)", maxLength: 48, nullable: false),
                    step_ordinal = table.Column<int>(type: "integer", nullable: false),
                    step_progress = table.Column<int>(type: "integer", nullable: false),
                    started_at = table.Column<DateTimeOffset>(type: "timestamp with time zone", nullable: false),
                    completed_at = table.Column<DateTimeOffset>(type: "timestamp with time zone", nullable: true)
                },
                constraints: table =>
                {
                    table.PrimaryKey("pk_character_quests", x => x.id);
                    table.CheckConstraint("ck_character_quests_progress_non_negative", "step_ordinal > 0 AND step_progress >= 0");
                    table.ForeignKey(
                        name: "fk_character_quests_characters_character_id",
                        column: x => x.character_id,
                        principalTable: "characters",
                        principalColumn: "id",
                        onDelete: ReferentialAction.Restrict);
                    table.ForeignKey(
                        name: "fk_character_quests_quest_defs_quest_def_id",
                        column: x => x.quest_def_id,
                        principalTable: "quest_defs",
                        principalColumn: "id",
                        onDelete: ReferentialAction.Restrict);
                });

            migrationBuilder.CreateTable(
                name: "character_skills",
                columns: table => new
                {
                    character_id = table.Column<int>(type: "integer", nullable: false),
                    skill_id = table.Column<int>(type: "integer", nullable: false),
                    xp = table.Column<long>(type: "bigint", nullable: false)
                },
                constraints: table =>
                {
                    table.PrimaryKey("pk_character_skills", x => new { x.character_id, x.skill_id });
                    table.CheckConstraint("ck_character_skills_xp_non_negative", "xp >= 0");
                    table.ForeignKey(
                        name: "fk_character_skills_characters_character_id",
                        column: x => x.character_id,
                        principalTable: "characters",
                        principalColumn: "id",
                        onDelete: ReferentialAction.Restrict);
                    table.ForeignKey(
                        name: "fk_character_skills_skills_skill_id",
                        column: x => x.skill_id,
                        principalTable: "skills",
                        principalColumn: "id",
                        onDelete: ReferentialAction.Restrict);
                });

            migrationBuilder.CreateTable(
                name: "death_records",
                columns: table => new
                {
                    id = table.Column<long>(type: "bigint", nullable: false)
                        .Annotation("Npgsql:ValueGenerationStrategy", NpgsqlValueGenerationStrategy.IdentityByDefaultColumn),
                    character_id = table.Column<int>(type: "integer", nullable: false),
                    cause = table.Column<string>(type: "character varying(48)", maxLength: 48, nullable: false),
                    resolution_seed = table.Column<long>(type: "bigint", nullable: false),
                    star_system_id = table.Column<int>(type: "integer", nullable: false),
                    body_id = table.Column<int>(type: "integer", nullable: true),
                    killer_character_id = table.Column<int>(type: "integer", nullable: true),
                    occurred_at = table.Column<DateTimeOffset>(type: "timestamp with time zone", nullable: false)
                },
                constraints: table =>
                {
                    table.PrimaryKey("pk_death_records", x => x.id);
                    table.ForeignKey(
                        name: "fk_death_records_characters_character_id",
                        column: x => x.character_id,
                        principalTable: "characters",
                        principalColumn: "id",
                        onDelete: ReferentialAction.Restrict);
                });

            migrationBuilder.CreateTable(
                name: "ledger_entries",
                columns: table => new
                {
                    id = table.Column<long>(type: "bigint", nullable: false)
                        .Annotation("Npgsql:ValueGenerationStrategy", NpgsqlValueGenerationStrategy.IdentityByDefaultColumn),
                    character_id = table.Column<int>(type: "integer", nullable: false),
                    delta_credits = table.Column<long>(type: "bigint", nullable: false),
                    reason = table.Column<string>(type: "character varying(48)", maxLength: 48, nullable: false),
                    reference_id = table.Column<long>(type: "bigint", nullable: true),
                    created_at = table.Column<DateTimeOffset>(type: "timestamp with time zone", nullable: false)
                },
                constraints: table =>
                {
                    table.PrimaryKey("pk_ledger_entries", x => x.id);
                    table.ForeignKey(
                        name: "fk_ledger_entries_characters_character_id",
                        column: x => x.character_id,
                        principalTable: "characters",
                        principalColumn: "id",
                        onDelete: ReferentialAction.Restrict);
                });

            migrationBuilder.CreateTable(
                name: "industry_jobs",
                columns: table => new
                {
                    id = table.Column<long>(type: "bigint", nullable: false)
                        .Annotation("Npgsql:ValueGenerationStrategy", NpgsqlValueGenerationStrategy.IdentityByDefaultColumn),
                    character_id = table.Column<int>(type: "integer", nullable: false),
                    recipe_id = table.Column<int>(type: "integer", nullable: false),
                    station_id = table.Column<int>(type: "integer", nullable: false),
                    runs = table.Column<int>(type: "integer", nullable: false),
                    state = table.Column<string>(type: "character varying(48)", maxLength: 48, nullable: false),
                    started_at = table.Column<DateTimeOffset>(type: "timestamp with time zone", nullable: false),
                    completes_at = table.Column<DateTimeOffset>(type: "timestamp with time zone", nullable: false),
                    claimed_at = table.Column<DateTimeOffset>(type: "timestamp with time zone", nullable: true),
                    fee_paid = table.Column<long>(type: "bigint", nullable: false),
                    input_cost_basis = table.Column<long>(type: "bigint", nullable: false)
                },
                constraints: table =>
                {
                    table.PrimaryKey("pk_industry_jobs", x => x.id);
                    table.CheckConstraint("ck_industry_jobs_fee_non_negative", "fee_paid >= 0");
                    table.CheckConstraint("ck_industry_jobs_input_cost_non_negative", "input_cost_basis >= 0");
                    table.CheckConstraint("ck_industry_jobs_runs_positive", "runs > 0");
                    table.ForeignKey(
                        name: "fk_industry_jobs_characters_character_id",
                        column: x => x.character_id,
                        principalTable: "characters",
                        principalColumn: "id",
                        onDelete: ReferentialAction.Restrict);
                    table.ForeignKey(
                        name: "fk_industry_jobs_recipes_recipe_id",
                        column: x => x.recipe_id,
                        principalTable: "recipes",
                        principalColumn: "id",
                        onDelete: ReferentialAction.Restrict);
                    table.ForeignKey(
                        name: "fk_industry_jobs_stations_station_id",
                        column: x => x.station_id,
                        principalTable: "stations",
                        principalColumn: "id",
                        onDelete: ReferentialAction.Restrict);
                });

            migrationBuilder.CreateTable(
                name: "market_orders",
                columns: table => new
                {
                    id = table.Column<long>(type: "bigint", nullable: false)
                        .Annotation("Npgsql:ValueGenerationStrategy", NpgsqlValueGenerationStrategy.IdentityByDefaultColumn),
                    station_id = table.Column<int>(type: "integer", nullable: false),
                    star_system_id = table.Column<int>(type: "integer", nullable: false),
                    item_def_id = table.Column<int>(type: "integer", nullable: false),
                    character_id = table.Column<int>(type: "integer", nullable: false),
                    side = table.Column<string>(type: "character varying(48)", maxLength: 48, nullable: false),
                    price = table.Column<long>(type: "bigint", nullable: false),
                    quantity_original = table.Column<int>(type: "integer", nullable: false),
                    quantity_remaining = table.Column<int>(type: "integer", nullable: false),
                    escrowed_credits = table.Column<long>(type: "bigint", nullable: false),
                    reserved_quantity = table.Column<int>(type: "integer", nullable: false),
                    reserved_cost_basis = table.Column<long>(type: "bigint", nullable: false),
                    placed_at = table.Column<DateTimeOffset>(type: "timestamp with time zone", nullable: false),
                    expires_at = table.Column<DateTimeOffset>(type: "timestamp with time zone", nullable: false),
                    cancelled_at = table.Column<DateTimeOffset>(type: "timestamp with time zone", nullable: true)
                },
                constraints: table =>
                {
                    table.PrimaryKey("pk_market_orders", x => x.id);
                    table.CheckConstraint("ck_market_orders_escrow_matches_side", "(side = 'Buy' AND reserved_quantity = 0) OR (side = 'Sell' AND escrowed_credits = 0)");
                    table.CheckConstraint("ck_market_orders_escrow_non_negative", "escrowed_credits >= 0");
                    table.CheckConstraint("ck_market_orders_price_positive", "price > 0");
                    table.CheckConstraint("ck_market_orders_quantity_original_positive", "quantity_original > 0");
                    table.CheckConstraint("ck_market_orders_quantity_remaining_in_range", "quantity_remaining >= 0 AND quantity_remaining <= quantity_original");
                    table.CheckConstraint("ck_market_orders_reserved_non_negative", "reserved_quantity >= 0");
                    table.ForeignKey(
                        name: "fk_market_orders_characters_character_id",
                        column: x => x.character_id,
                        principalTable: "characters",
                        principalColumn: "id",
                        onDelete: ReferentialAction.Restrict);
                    table.ForeignKey(
                        name: "fk_market_orders_item_defs_item_def_id",
                        column: x => x.item_def_id,
                        principalTable: "item_defs",
                        principalColumn: "id",
                        onDelete: ReferentialAction.Restrict);
                    table.ForeignKey(
                        name: "fk_market_orders_stations_station_id",
                        column: x => x.station_id,
                        principalTable: "stations",
                        principalColumn: "id",
                        onDelete: ReferentialAction.Restrict);
                });

            migrationBuilder.CreateTable(
                name: "industry_job_inputs",
                columns: table => new
                {
                    industry_job_id = table.Column<long>(type: "bigint", nullable: false),
                    item_def_id = table.Column<int>(type: "integer", nullable: false),
                    quantity = table.Column<int>(type: "integer", nullable: false),
                    cost_basis = table.Column<long>(type: "bigint", nullable: false)
                },
                constraints: table =>
                {
                    table.PrimaryKey("pk_industry_job_inputs", x => new { x.industry_job_id, x.item_def_id });
                    table.CheckConstraint("ck_industry_job_inputs_cost_non_negative", "cost_basis >= 0");
                    table.CheckConstraint("ck_industry_job_inputs_quantity_positive", "quantity > 0");
                    table.ForeignKey(
                        name: "fk_industry_job_inputs_industry_jobs_industry_job_id",
                        column: x => x.industry_job_id,
                        principalTable: "industry_jobs",
                        principalColumn: "id",
                        onDelete: ReferentialAction.Restrict);
                    table.ForeignKey(
                        name: "fk_industry_job_inputs_item_defs_item_def_id",
                        column: x => x.item_def_id,
                        principalTable: "item_defs",
                        principalColumn: "id",
                        onDelete: ReferentialAction.Restrict);
                });

            migrationBuilder.CreateTable(
                name: "insurance_policies",
                columns: table => new
                {
                    id = table.Column<long>(type: "bigint", nullable: false)
                        .Annotation("Npgsql:ValueGenerationStrategy", NpgsqlValueGenerationStrategy.IdentityByDefaultColumn),
                    item_instance_id = table.Column<long>(type: "bigint", nullable: false),
                    character_id = table.Column<int>(type: "integer", nullable: false),
                    tier = table.Column<string>(type: "character varying(48)", maxLength: 48, nullable: false),
                    premium_paid = table.Column<long>(type: "bigint", nullable: false),
                    insured_value = table.Column<long>(type: "bigint", nullable: false),
                    purchased_at = table.Column<DateTimeOffset>(type: "timestamp with time zone", nullable: false),
                    expires_at = table.Column<DateTimeOffset>(type: "timestamp with time zone", nullable: false),
                    claimed_at = table.Column<DateTimeOffset>(type: "timestamp with time zone", nullable: true),
                    payout_amount = table.Column<long>(type: "bigint", nullable: true)
                },
                constraints: table =>
                {
                    table.PrimaryKey("pk_insurance_policies", x => x.id);
                    table.CheckConstraint("ck_insurance_policies_insured_value_non_negative", "insured_value >= 0");
                    table.CheckConstraint("ck_insurance_policies_payout_below_insured_value", "payout_amount IS NULL OR payout_amount < insured_value");
                    table.CheckConstraint("ck_insurance_policies_premium_non_negative", "premium_paid >= 0");
                    table.ForeignKey(
                        name: "fk_insurance_policies_characters_character_id",
                        column: x => x.character_id,
                        principalTable: "characters",
                        principalColumn: "id",
                        onDelete: ReferentialAction.Restrict);
                });

            migrationBuilder.CreateTable(
                name: "inventories",
                columns: table => new
                {
                    id = table.Column<long>(type: "bigint", nullable: false)
                        .Annotation("Npgsql:ValueGenerationStrategy", NpgsqlValueGenerationStrategy.IdentityByDefaultColumn),
                    character_id = table.Column<int>(type: "integer", nullable: false),
                    kind = table.Column<string>(type: "character varying(48)", maxLength: 48, nullable: false),
                    station_id = table.Column<int>(type: "integer", nullable: true),
                    ship_item_instance_id = table.Column<long>(type: "bigint", nullable: true),
                    capacity_m3 = table.Column<double>(type: "double precision", nullable: false)
                },
                constraints: table =>
                {
                    table.PrimaryKey("pk_inventories", x => x.id);
                    table.CheckConstraint("ck_inventories_capacity_non_negative", "capacity_m3 >= 0");
                    table.ForeignKey(
                        name: "fk_inventories_characters_character_id",
                        column: x => x.character_id,
                        principalTable: "characters",
                        principalColumn: "id",
                        onDelete: ReferentialAction.Restrict);
                    table.ForeignKey(
                        name: "fk_inventories_stations_station_id",
                        column: x => x.station_id,
                        principalTable: "stations",
                        principalColumn: "id",
                        onDelete: ReferentialAction.Restrict);
                });

            migrationBuilder.CreateTable(
                name: "inventory_items",
                columns: table => new
                {
                    inventory_id = table.Column<long>(type: "bigint", nullable: false),
                    item_def_id = table.Column<int>(type: "integer", nullable: false),
                    quantity = table.Column<int>(type: "integer", nullable: false),
                    cost_basis = table.Column<long>(type: "bigint", nullable: false)
                },
                constraints: table =>
                {
                    table.PrimaryKey("pk_inventory_items", x => new { x.inventory_id, x.item_def_id });
                    table.CheckConstraint("ck_inventory_items_cost_basis_non_negative", "cost_basis >= 0");
                    table.CheckConstraint("ck_inventory_items_quantity_positive", "quantity > 0");
                    table.ForeignKey(
                        name: "fk_inventory_items_inventories_inventory_id",
                        column: x => x.inventory_id,
                        principalTable: "inventories",
                        principalColumn: "id",
                        onDelete: ReferentialAction.Restrict);
                    table.ForeignKey(
                        name: "fk_inventory_items_item_defs_item_def_id",
                        column: x => x.item_def_id,
                        principalTable: "item_defs",
                        principalColumn: "id",
                        onDelete: ReferentialAction.Restrict);
                });

            migrationBuilder.CreateTable(
                name: "item_instances",
                columns: table => new
                {
                    id = table.Column<long>(type: "bigint", nullable: false)
                        .Annotation("Npgsql:ValueGenerationStrategy", NpgsqlValueGenerationStrategy.IdentityByDefaultColumn),
                    item_def_id = table.Column<int>(type: "integer", nullable: false),
                    inventory_id = table.Column<long>(type: "bigint", nullable: true),
                    condition = table.Column<int>(type: "integer", nullable: false),
                    acquisition_value = table.Column<long>(type: "bigint", nullable: false),
                    created_at = table.Column<DateTimeOffset>(type: "timestamp with time zone", nullable: false),
                    destroyed_at = table.Column<DateTimeOffset>(type: "timestamp with time zone", nullable: true)
                },
                constraints: table =>
                {
                    table.PrimaryKey("pk_item_instances", x => x.id);
                    table.CheckConstraint("ck_item_instances_acquisition_non_negative", "acquisition_value >= 0");
                    table.CheckConstraint("ck_item_instances_condition_in_range", "condition BETWEEN 0 AND 100");
                    table.ForeignKey(
                        name: "fk_item_instances_inventories_inventory_id",
                        column: x => x.inventory_id,
                        principalTable: "inventories",
                        principalColumn: "id",
                        onDelete: ReferentialAction.Restrict);
                    table.ForeignKey(
                        name: "fk_item_instances_item_defs_item_def_id",
                        column: x => x.item_def_id,
                        principalTable: "item_defs",
                        principalColumn: "id",
                        onDelete: ReferentialAction.Restrict);
                });

            migrationBuilder.CreateIndex(
                name: "ix_accounts_email",
                table: "accounts",
                column: "email",
                unique: true);

            migrationBuilder.CreateIndex(
                name: "ix_bodies_key",
                table: "bodies",
                column: "key",
                unique: true);

            migrationBuilder.CreateIndex(
                name: "ix_bodies_star_system_id",
                table: "bodies",
                column: "star_system_id");

            migrationBuilder.CreateIndex(
                name: "ix_bounties_poster_character_id",
                table: "bounties",
                column: "poster_character_id");

            migrationBuilder.CreateIndex(
                name: "ix_bounties_target_character_id",
                table: "bounties",
                column: "target_character_id",
                filter: "claimed_at IS NULL");

            migrationBuilder.CreateIndex(
                name: "ix_character_quests_character_id_quest_def_id_state",
                table: "character_quests",
                columns: new[] { "character_id", "quest_def_id", "state" });

            migrationBuilder.CreateIndex(
                name: "ix_character_quests_quest_def_id",
                table: "character_quests",
                column: "quest_def_id");

            migrationBuilder.CreateIndex(
                name: "ix_character_skills_skill_id",
                table: "character_skills",
                column: "skill_id");

            migrationBuilder.CreateIndex(
                name: "ix_characters_account_id",
                table: "characters",
                column: "account_id");

            migrationBuilder.CreateIndex(
                name: "ix_characters_home_body_id",
                table: "characters",
                column: "home_body_id");

            migrationBuilder.CreateIndex(
                name: "ix_characters_name",
                table: "characters",
                column: "name",
                unique: true);

            migrationBuilder.CreateIndex(
                name: "ix_death_records_character_id_occurred_at",
                table: "death_records",
                columns: new[] { "character_id", "occurred_at" });

            migrationBuilder.CreateIndex(
                name: "ix_death_records_killer_character_id",
                table: "death_records",
                column: "killer_character_id",
                filter: "killer_character_id IS NOT NULL");

            migrationBuilder.CreateIndex(
                name: "ix_industry_job_inputs_item_def_id",
                table: "industry_job_inputs",
                column: "item_def_id");

            migrationBuilder.CreateIndex(
                name: "ix_industry_jobs_character_id_state",
                table: "industry_jobs",
                columns: new[] { "character_id", "state" });

            migrationBuilder.CreateIndex(
                name: "ix_industry_jobs_completes_at",
                table: "industry_jobs",
                column: "completes_at",
                filter: "state = 'Running'");

            migrationBuilder.CreateIndex(
                name: "ix_industry_jobs_recipe_id",
                table: "industry_jobs",
                column: "recipe_id");

            migrationBuilder.CreateIndex(
                name: "ix_industry_jobs_station_id",
                table: "industry_jobs",
                column: "station_id");

            migrationBuilder.CreateIndex(
                name: "ix_insurance_policies_character_id",
                table: "insurance_policies",
                column: "character_id");

            migrationBuilder.CreateIndex(
                name: "ix_insurance_policies_item_instance_id",
                table: "insurance_policies",
                column: "item_instance_id",
                unique: true,
                filter: "claimed_at IS NULL");

            migrationBuilder.CreateIndex(
                name: "ix_inventories_character_id",
                table: "inventories",
                column: "character_id");

            migrationBuilder.CreateIndex(
                name: "ix_inventories_character_id_station_id_kind",
                table: "inventories",
                columns: new[] { "character_id", "station_id", "kind" },
                unique: true);

            migrationBuilder.CreateIndex(
                name: "ix_inventories_ship_item_instance_id",
                table: "inventories",
                column: "ship_item_instance_id");

            migrationBuilder.CreateIndex(
                name: "ix_inventories_station_id",
                table: "inventories",
                column: "station_id");

            migrationBuilder.CreateIndex(
                name: "ix_inventory_items_item_def_id",
                table: "inventory_items",
                column: "item_def_id");

            migrationBuilder.CreateIndex(
                name: "ix_item_defs_key",
                table: "item_defs",
                column: "key",
                unique: true);

            migrationBuilder.CreateIndex(
                name: "ix_item_instances_inventory_id",
                table: "item_instances",
                column: "inventory_id");

            migrationBuilder.CreateIndex(
                name: "ix_item_instances_item_def_id",
                table: "item_instances",
                column: "item_def_id");

            migrationBuilder.CreateIndex(
                name: "ix_ledger_entries_character_id_created_at",
                table: "ledger_entries",
                columns: new[] { "character_id", "created_at" });

            migrationBuilder.CreateIndex(
                name: "ix_ledger_entries_reason_created_at",
                table: "ledger_entries",
                columns: new[] { "reason", "created_at" });

            migrationBuilder.CreateIndex(
                name: "ix_market_orders_character_id",
                table: "market_orders",
                column: "character_id");

            migrationBuilder.CreateIndex(
                name: "ix_market_orders_expires_at",
                table: "market_orders",
                column: "expires_at",
                filter: "cancelled_at IS NULL");

            migrationBuilder.CreateIndex(
                name: "ix_market_orders_item_def_id",
                table: "market_orders",
                column: "item_def_id");

            migrationBuilder.CreateIndex(
                name: "ix_market_orders_station_id_item_def_id_side_price",
                table: "market_orders",
                columns: new[] { "station_id", "item_def_id", "side", "price" },
                filter: "quantity_remaining > 0 AND cancelled_at IS NULL");

            migrationBuilder.CreateIndex(
                name: "ix_quest_defs_key",
                table: "quest_defs",
                column: "key",
                unique: true);

            migrationBuilder.CreateIndex(
                name: "ix_quest_defs_prerequisite_quest_def_id",
                table: "quest_defs",
                column: "prerequisite_quest_def_id");

            migrationBuilder.CreateIndex(
                name: "ix_quest_defs_reward_skill_id",
                table: "quest_defs",
                column: "reward_skill_id");

            migrationBuilder.CreateIndex(
                name: "ix_quest_steps_quest_def_id_ordinal",
                table: "quest_steps",
                columns: new[] { "quest_def_id", "ordinal" },
                unique: true);

            migrationBuilder.CreateIndex(
                name: "ix_recipe_inputs_item_def_id",
                table: "recipe_inputs",
                column: "item_def_id");

            migrationBuilder.CreateIndex(
                name: "ix_recipes_key",
                table: "recipes",
                column: "key",
                unique: true);

            migrationBuilder.CreateIndex(
                name: "ix_recipes_output_item_def_id",
                table: "recipes",
                column: "output_item_def_id");

            migrationBuilder.CreateIndex(
                name: "ix_recipes_required_tool_item_def_id",
                table: "recipes",
                column: "required_tool_item_def_id");

            migrationBuilder.CreateIndex(
                name: "ix_recipes_skill_id",
                table: "recipes",
                column: "skill_id");

            migrationBuilder.CreateIndex(
                name: "ix_resource_nodes_body_id_item_def_id",
                table: "resource_nodes",
                columns: new[] { "body_id", "item_def_id" });

            migrationBuilder.CreateIndex(
                name: "ix_resource_nodes_item_def_id",
                table: "resource_nodes",
                column: "item_def_id");

            migrationBuilder.CreateIndex(
                name: "ix_resource_nodes_respawn_at",
                table: "resource_nodes",
                column: "respawn_at");

            migrationBuilder.CreateIndex(
                name: "ix_resource_nodes_star_system_id",
                table: "resource_nodes",
                column: "star_system_id");

            migrationBuilder.CreateIndex(
                name: "ix_skills_key",
                table: "skills",
                column: "key",
                unique: true);

            migrationBuilder.CreateIndex(
                name: "ix_star_systems_galaxy_x_galaxy_y_galaxy_z",
                table: "star_systems",
                columns: new[] { "galaxy_x", "galaxy_y", "galaxy_z" });

            migrationBuilder.CreateIndex(
                name: "ix_star_systems_key",
                table: "star_systems",
                column: "key",
                unique: true);

            migrationBuilder.CreateIndex(
                name: "ix_stations_body_id",
                table: "stations",
                column: "body_id");

            migrationBuilder.CreateIndex(
                name: "ix_stations_key",
                table: "stations",
                column: "key",
                unique: true);

            migrationBuilder.CreateIndex(
                name: "ix_stations_star_system_id",
                table: "stations",
                column: "star_system_id");

            migrationBuilder.CreateIndex(
                name: "ix_trades_item_def_id_executed_at",
                table: "trades",
                columns: new[] { "item_def_id", "executed_at" });

            migrationBuilder.CreateIndex(
                name: "ix_trades_station_id_executed_at",
                table: "trades",
                columns: new[] { "station_id", "executed_at" });

            migrationBuilder.AddForeignKey(
                name: "fk_insurance_policies_item_instances_item_instance_id",
                table: "insurance_policies",
                column: "item_instance_id",
                principalTable: "item_instances",
                principalColumn: "id",
                onDelete: ReferentialAction.Restrict);

            migrationBuilder.AddForeignKey(
                name: "fk_inventories_item_instances_ship_item_instance_id",
                table: "inventories",
                column: "ship_item_instance_id",
                principalTable: "item_instances",
                principalColumn: "id",
                onDelete: ReferentialAction.Restrict);
        }

        /// <inheritdoc />
        protected override void Down(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.DropForeignKey(
                name: "fk_bodies_star_systems_star_system_id",
                table: "bodies");

            migrationBuilder.DropForeignKey(
                name: "fk_stations_star_systems_star_system_id",
                table: "stations");

            migrationBuilder.DropForeignKey(
                name: "fk_inventories_characters_character_id",
                table: "inventories");

            migrationBuilder.DropForeignKey(
                name: "fk_stations_bodies_body_id",
                table: "stations");

            migrationBuilder.DropForeignKey(
                name: "fk_item_instances_item_defs_item_def_id",
                table: "item_instances");

            migrationBuilder.DropForeignKey(
                name: "fk_inventories_stations_station_id",
                table: "inventories");

            migrationBuilder.DropForeignKey(
                name: "fk_inventories_item_instances_ship_item_instance_id",
                table: "inventories");

            migrationBuilder.DropTable(
                name: "bounties");

            migrationBuilder.DropTable(
                name: "character_faucet_daily");

            migrationBuilder.DropTable(
                name: "character_quests");

            migrationBuilder.DropTable(
                name: "character_skills");

            migrationBuilder.DropTable(
                name: "death_records");

            migrationBuilder.DropTable(
                name: "industry_job_inputs");

            migrationBuilder.DropTable(
                name: "insurance_policies");

            migrationBuilder.DropTable(
                name: "inventory_items");

            migrationBuilder.DropTable(
                name: "ledger_entries");

            migrationBuilder.DropTable(
                name: "market_orders");

            migrationBuilder.DropTable(
                name: "quest_steps");

            migrationBuilder.DropTable(
                name: "recipe_inputs");

            migrationBuilder.DropTable(
                name: "resource_nodes");

            migrationBuilder.DropTable(
                name: "trades");

            migrationBuilder.DropTable(
                name: "industry_jobs");

            migrationBuilder.DropTable(
                name: "quest_defs");

            migrationBuilder.DropTable(
                name: "recipes");

            migrationBuilder.DropTable(
                name: "skills");

            migrationBuilder.DropTable(
                name: "star_systems");

            migrationBuilder.DropTable(
                name: "characters");

            migrationBuilder.DropTable(
                name: "accounts");

            migrationBuilder.DropTable(
                name: "bodies");

            migrationBuilder.DropTable(
                name: "item_defs");

            migrationBuilder.DropTable(
                name: "stations");

            migrationBuilder.DropTable(
                name: "item_instances");

            migrationBuilder.DropTable(
                name: "inventories");
        }
    }
}
