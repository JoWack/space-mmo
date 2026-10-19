using System;
using Microsoft.EntityFrameworkCore.Migrations;
using Npgsql.EntityFrameworkCore.PostgreSQL.Metadata;

#nullable disable

namespace SpaceMMO.Data.Migrations
{
    /// <inheritdoc />
    public partial class DeathCosts : Migration
    {
        /// <inheritdoc />
        protected override void Up(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.DropTable(
                name: "insurance_policies");

            migrationBuilder.DropColumn(
                name: "resolution_seed",
                table: "death_records");

            migrationBuilder.AddColumn<DateTimeOffset>(
                name: "costs_applied_at",
                table: "death_records",
                type: "timestamp with time zone",
                nullable: true);

            // Every death before this cost nothing, because nothing took it, and is settled as it stands. Left owing,
            // the next respawn or death of each character would take their pockets for a fight long over: the dev
            // database has the deaths of 194's and 198's playtests.
            migrationBuilder.Sql("UPDATE death_records SET costs_applied_at = occurred_at;");

            migrationBuilder.CreateTable(
                name: "death_losses",
                columns: table => new
                {
                    death_record_id = table.Column<long>(type: "bigint", nullable: false),
                    item_def_id = table.Column<int>(type: "integer", nullable: false),
                    style = table.Column<string>(type: "character varying(48)", maxLength: 48, nullable: false),
                    dropped = table.Column<int>(type: "integer", nullable: false),
                    destroyed = table.Column<int>(type: "integer", nullable: false)
                },
                constraints: table =>
                {
                    table.PrimaryKey("pk_death_losses", x => new { x.death_record_id, x.item_def_id, x.style });
                    table.CheckConstraint("ck_death_losses_destroyed_non_negative", "destroyed >= 0");
                    table.CheckConstraint("ck_death_losses_dropped_non_negative", "dropped >= 0");
                    table.CheckConstraint("ck_death_losses_something_lost", "dropped + destroyed > 0");
                    table.ForeignKey(
                        name: "fk_death_losses_death_records_death_record_id",
                        column: x => x.death_record_id,
                        principalTable: "death_records",
                        principalColumn: "id",
                        onDelete: ReferentialAction.Restrict);
                    table.ForeignKey(
                        name: "fk_death_losses_item_defs_item_def_id",
                        column: x => x.item_def_id,
                        principalTable: "item_defs",
                        principalColumn: "id",
                        onDelete: ReferentialAction.Restrict);
                });

            migrationBuilder.CreateTable(
                name: "loot_containers",
                columns: table => new
                {
                    id = table.Column<long>(type: "bigint", nullable: false)
                        .Annotation("Npgsql:ValueGenerationStrategy", NpgsqlValueGenerationStrategy.IdentityByDefaultColumn),
                    death_record_id = table.Column<long>(type: "bigint", nullable: false),
                    dropped_by_character_id = table.Column<int>(type: "integer", nullable: false),
                    star_system_id = table.Column<int>(type: "integer", nullable: false),
                    system_x = table.Column<double>(type: "double precision", nullable: false),
                    system_y = table.Column<double>(type: "double precision", nullable: false),
                    system_z = table.Column<double>(type: "double precision", nullable: false),
                    created_at = table.Column<DateTimeOffset>(type: "timestamp with time zone", nullable: false),
                    expires_at = table.Column<DateTimeOffset>(type: "timestamp with time zone", nullable: false)
                },
                constraints: table =>
                {
                    table.PrimaryKey("pk_loot_containers", x => x.id);
                    table.CheckConstraint("ck_loot_containers_expires_after_created", "expires_at > created_at");
                    table.ForeignKey(
                        name: "fk_loot_containers_characters_dropped_by_character_id",
                        column: x => x.dropped_by_character_id,
                        principalTable: "characters",
                        principalColumn: "id",
                        onDelete: ReferentialAction.Restrict);
                    table.ForeignKey(
                        name: "fk_loot_containers_death_records_death_record_id",
                        column: x => x.death_record_id,
                        principalTable: "death_records",
                        principalColumn: "id",
                        onDelete: ReferentialAction.Restrict);
                    table.ForeignKey(
                        name: "fk_loot_containers_star_systems_star_system_id",
                        column: x => x.star_system_id,
                        principalTable: "star_systems",
                        principalColumn: "id",
                        onDelete: ReferentialAction.Restrict);
                });

            migrationBuilder.CreateTable(
                name: "loot_container_items",
                columns: table => new
                {
                    loot_container_id = table.Column<long>(type: "bigint", nullable: false),
                    item_def_id = table.Column<int>(type: "integer", nullable: false),
                    style = table.Column<string>(type: "character varying(48)", maxLength: 48, nullable: false),
                    quantity = table.Column<int>(type: "integer", nullable: false),
                    cost_basis = table.Column<long>(type: "bigint", nullable: false)
                },
                constraints: table =>
                {
                    table.PrimaryKey("pk_loot_container_items", x => new { x.loot_container_id, x.item_def_id, x.style });
                    table.CheckConstraint("ck_loot_container_items_cost_non_negative", "cost_basis >= 0");
                    table.CheckConstraint("ck_loot_container_items_quantity_positive", "quantity > 0");
                    table.ForeignKey(
                        name: "fk_loot_container_items_item_defs_item_def_id",
                        column: x => x.item_def_id,
                        principalTable: "item_defs",
                        principalColumn: "id",
                        onDelete: ReferentialAction.Restrict);
                    table.ForeignKey(
                        name: "fk_loot_container_items_loot_containers_loot_container_id",
                        column: x => x.loot_container_id,
                        principalTable: "loot_containers",
                        principalColumn: "id",
                        onDelete: ReferentialAction.Restrict);
                });

            migrationBuilder.CreateIndex(
                name: "ix_death_records_costs_owed",
                table: "death_records",
                column: "character_id",
                filter: "costs_applied_at IS NULL");

            migrationBuilder.CreateIndex(
                name: "ix_death_losses_item_def_id",
                table: "death_losses",
                column: "item_def_id");

            migrationBuilder.CreateIndex(
                name: "ix_loot_container_items_item_def_id",
                table: "loot_container_items",
                column: "item_def_id");

            migrationBuilder.CreateIndex(
                name: "ix_loot_containers_death_record_id",
                table: "loot_containers",
                column: "death_record_id",
                unique: true);

            migrationBuilder.CreateIndex(
                name: "ix_loot_containers_dropped_by_character_id",
                table: "loot_containers",
                column: "dropped_by_character_id");

            migrationBuilder.CreateIndex(
                name: "ix_loot_containers_expires_at",
                table: "loot_containers",
                column: "expires_at");

            migrationBuilder.CreateIndex(
                name: "ix_loot_containers_star_system_id",
                table: "loot_containers",
                column: "star_system_id");
        }

        /// <inheritdoc />
        protected override void Down(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.DropTable(
                name: "death_losses");

            migrationBuilder.DropTable(
                name: "loot_container_items");

            migrationBuilder.DropTable(
                name: "loot_containers");

            migrationBuilder.DropIndex(
                name: "ix_death_records_costs_owed",
                table: "death_records");

            migrationBuilder.DropColumn(
                name: "costs_applied_at",
                table: "death_records");

            migrationBuilder.AddColumn<long>(
                name: "resolution_seed",
                table: "death_records",
                type: "bigint",
                nullable: false,
                defaultValue: 0L);

            migrationBuilder.CreateTable(
                name: "insurance_policies",
                columns: table => new
                {
                    id = table.Column<long>(type: "bigint", nullable: false)
                        .Annotation("Npgsql:ValueGenerationStrategy", NpgsqlValueGenerationStrategy.IdentityByDefaultColumn),
                    character_id = table.Column<int>(type: "integer", nullable: false),
                    item_instance_id = table.Column<long>(type: "bigint", nullable: false),
                    claimed_at = table.Column<DateTimeOffset>(type: "timestamp with time zone", nullable: true),
                    expires_at = table.Column<DateTimeOffset>(type: "timestamp with time zone", nullable: false),
                    insured_value = table.Column<long>(type: "bigint", nullable: false),
                    payout_amount = table.Column<long>(type: "bigint", nullable: true),
                    premium_paid = table.Column<long>(type: "bigint", nullable: false),
                    purchased_at = table.Column<DateTimeOffset>(type: "timestamp with time zone", nullable: false),
                    tier = table.Column<string>(type: "character varying(48)", maxLength: 48, nullable: false)
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
                    table.ForeignKey(
                        name: "fk_insurance_policies_item_instances_item_instance_id",
                        column: x => x.item_instance_id,
                        principalTable: "item_instances",
                        principalColumn: "id",
                        onDelete: ReferentialAction.Restrict);
                });

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
        }
    }
}
