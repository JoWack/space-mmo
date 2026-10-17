using System;
using Microsoft.EntityFrameworkCore.Migrations;
using Npgsql.EntityFrameworkCore.PostgreSQL.Metadata;

#nullable disable

namespace SpaceMMO.Data.Migrations
{
    /// <inheritdoc />
    public partial class Combat : Migration
    {
        /// <inheritdoc />
        protected override void Up(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.AddColumn<double>(
                name: "faction_a_direction_x",
                table: "star_systems",
                type: "double precision",
                nullable: true);

            migrationBuilder.AddColumn<double>(
                name: "faction_a_direction_y",
                table: "star_systems",
                type: "double precision",
                nullable: true);

            migrationBuilder.AddColumn<double>(
                name: "faction_a_direction_z",
                table: "star_systems",
                type: "double precision",
                nullable: true);

            migrationBuilder.AddColumn<int>(
                name: "faction_capital_body_id",
                table: "star_systems",
                type: "integer",
                nullable: true);

            migrationBuilder.AddColumn<double>(
                name: "faction_contested_radius_km",
                table: "star_systems",
                type: "double precision",
                nullable: true);

            migrationBuilder.AddColumn<double>(
                name: "faction_retaliation_seconds",
                table: "star_systems",
                type: "double precision",
                nullable: true);

            migrationBuilder.AddColumn<double>(
                name: "faction_safe_radius_km",
                table: "star_systems",
                type: "double precision",
                nullable: true);

            migrationBuilder.AddColumn<string>(
                name: "weapon_family",
                table: "skills",
                type: "character varying(48)",
                maxLength: 48,
                nullable: true);

            migrationBuilder.AddColumn<string>(
                name: "weapon_mount",
                table: "skills",
                type: "character varying(48)",
                maxLength: 48,
                nullable: true);

            migrationBuilder.AddColumn<string>(
                name: "weapon_type",
                table: "skills",
                type: "character varying(48)",
                maxLength: 48,
                nullable: true);

            migrationBuilder.AddColumn<int>(
                name: "xp_per_hundred_damage",
                table: "skills",
                type: "integer",
                nullable: true);

            migrationBuilder.AddColumn<int>(
                name: "weapon_cooldown_ms",
                table: "item_defs",
                type: "integer",
                nullable: true);

            migrationBuilder.AddColumn<int>(
                name: "weapon_damage",
                table: "item_defs",
                type: "integer",
                nullable: true);

            migrationBuilder.AddColumn<int>(
                name: "weapon_range_metres",
                table: "item_defs",
                type: "integer",
                nullable: true);

            migrationBuilder.AddColumn<int>(
                name: "weapon_skill_id",
                table: "item_defs",
                type: "integer",
                nullable: true);

            migrationBuilder.AddColumn<int>(
                name: "health",
                table: "characters",
                type: "integer",
                nullable: true);

            migrationBuilder.AddColumn<DateTimeOffset>(
                name: "last_combat_at",
                table: "characters",
                type: "timestamp with time zone",
                nullable: true);

            migrationBuilder.AddColumn<int>(
                name: "last_docked_station_id",
                table: "characters",
                type: "integer",
                nullable: true);

            // Whoever is docked now last docked there: where they come back if they die before docking again.
            migrationBuilder.Sql(
                "UPDATE characters SET last_docked_station_id = docked_station_id WHERE docked_station_id IS NOT NULL;");

            migrationBuilder.CreateTable(
                name: "attack_records",
                columns: table => new
                {
                    id = table.Column<long>(type: "bigint", nullable: false)
                        .Annotation("Npgsql:ValueGenerationStrategy", NpgsqlValueGenerationStrategy.IdentityByDefaultColumn),
                    attacker_character_id = table.Column<int>(type: "integer", nullable: false),
                    target_character_id = table.Column<int>(type: "integer", nullable: false),
                    attacked_at = table.Column<DateTimeOffset>(type: "timestamp with time zone", nullable: false)
                },
                constraints: table =>
                {
                    table.PrimaryKey("pk_attack_records", x => x.id);
                    table.ForeignKey(
                        name: "fk_attack_records_characters_attacker_character_id",
                        column: x => x.attacker_character_id,
                        principalTable: "characters",
                        principalColumn: "id",
                        onDelete: ReferentialAction.Restrict);
                    table.ForeignKey(
                        name: "fk_attack_records_characters_target_character_id",
                        column: x => x.target_character_id,
                        principalTable: "characters",
                        principalColumn: "id",
                        onDelete: ReferentialAction.Restrict);
                });

            migrationBuilder.CreateIndex(
                name: "ix_item_defs_weapon_skill_id",
                table: "item_defs",
                column: "weapon_skill_id");

            migrationBuilder.CreateIndex(
                name: "ix_characters_last_docked_station_id",
                table: "characters",
                column: "last_docked_station_id");

            migrationBuilder.AddCheckConstraint(
                name: "ck_characters_health_non_negative",
                table: "characters",
                sql: "health IS NULL OR health >= 0");

            migrationBuilder.CreateIndex(
                name: "ix_attack_records_attacker_character_id_target_character_id",
                table: "attack_records",
                columns: new[] { "attacker_character_id", "target_character_id" },
                unique: true);

            migrationBuilder.CreateIndex(
                name: "ix_attack_records_target_character_id",
                table: "attack_records",
                column: "target_character_id");

            migrationBuilder.AddForeignKey(
                name: "fk_characters_stations_last_docked_station_id",
                table: "characters",
                column: "last_docked_station_id",
                principalTable: "stations",
                principalColumn: "id",
                onDelete: ReferentialAction.Restrict);

            migrationBuilder.AddForeignKey(
                name: "fk_item_defs_skills_weapon_skill_id",
                table: "item_defs",
                column: "weapon_skill_id",
                principalTable: "skills",
                principalColumn: "id",
                onDelete: ReferentialAction.Restrict);
        }

        /// <inheritdoc />
        protected override void Down(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.DropForeignKey(
                name: "fk_characters_stations_last_docked_station_id",
                table: "characters");

            migrationBuilder.DropForeignKey(
                name: "fk_item_defs_skills_weapon_skill_id",
                table: "item_defs");

            migrationBuilder.DropTable(
                name: "attack_records");

            migrationBuilder.DropIndex(
                name: "ix_item_defs_weapon_skill_id",
                table: "item_defs");

            migrationBuilder.DropIndex(
                name: "ix_characters_last_docked_station_id",
                table: "characters");

            migrationBuilder.DropCheckConstraint(
                name: "ck_characters_health_non_negative",
                table: "characters");

            migrationBuilder.DropColumn(
                name: "faction_a_direction_x",
                table: "star_systems");

            migrationBuilder.DropColumn(
                name: "faction_a_direction_y",
                table: "star_systems");

            migrationBuilder.DropColumn(
                name: "faction_a_direction_z",
                table: "star_systems");

            migrationBuilder.DropColumn(
                name: "faction_capital_body_id",
                table: "star_systems");

            migrationBuilder.DropColumn(
                name: "faction_contested_radius_km",
                table: "star_systems");

            migrationBuilder.DropColumn(
                name: "faction_retaliation_seconds",
                table: "star_systems");

            migrationBuilder.DropColumn(
                name: "faction_safe_radius_km",
                table: "star_systems");

            migrationBuilder.DropColumn(
                name: "weapon_family",
                table: "skills");

            migrationBuilder.DropColumn(
                name: "weapon_mount",
                table: "skills");

            migrationBuilder.DropColumn(
                name: "weapon_type",
                table: "skills");

            migrationBuilder.DropColumn(
                name: "xp_per_hundred_damage",
                table: "skills");

            migrationBuilder.DropColumn(
                name: "weapon_cooldown_ms",
                table: "item_defs");

            migrationBuilder.DropColumn(
                name: "weapon_damage",
                table: "item_defs");

            migrationBuilder.DropColumn(
                name: "weapon_range_metres",
                table: "item_defs");

            migrationBuilder.DropColumn(
                name: "weapon_skill_id",
                table: "item_defs");

            migrationBuilder.DropColumn(
                name: "health",
                table: "characters");

            migrationBuilder.DropColumn(
                name: "last_combat_at",
                table: "characters");

            migrationBuilder.DropColumn(
                name: "last_docked_station_id",
                table: "characters");
        }
    }
}
