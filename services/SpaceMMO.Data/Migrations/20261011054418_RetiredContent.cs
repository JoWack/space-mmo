using System;
using Microsoft.EntityFrameworkCore.Migrations;

#nullable disable

namespace SpaceMMO.Data.Migrations
{
    /// <inheritdoc />
    public partial class RetiredContent : Migration
    {
        /// <inheritdoc />
        protected override void Up(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.AddColumn<DateTimeOffset>(
                name: "retired_at",
                table: "stations",
                type: "timestamp with time zone",
                nullable: true);

            migrationBuilder.AddColumn<DateTimeOffset>(
                name: "retired_at",
                table: "recipes",
                type: "timestamp with time zone",
                nullable: true);

            migrationBuilder.AddColumn<DateTimeOffset>(
                name: "retired_at",
                table: "quest_defs",
                type: "timestamp with time zone",
                nullable: true);

            migrationBuilder.AddColumn<DateTimeOffset>(
                name: "retired_at",
                table: "item_defs",
                type: "timestamp with time zone",
                nullable: true);
        }

        /// <inheritdoc />
        protected override void Down(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.DropColumn(
                name: "retired_at",
                table: "stations");

            migrationBuilder.DropColumn(
                name: "retired_at",
                table: "recipes");

            migrationBuilder.DropColumn(
                name: "retired_at",
                table: "quest_defs");

            migrationBuilder.DropColumn(
                name: "retired_at",
                table: "item_defs");
        }
    }
}
