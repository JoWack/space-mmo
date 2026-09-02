using Microsoft.EntityFrameworkCore.Migrations;

#nullable disable

namespace SpaceMMO.Data.Migrations
{
    /// <inheritdoc />
    public partial class Whereabouts : Migration
    {
        /// <inheritdoc />
        protected override void Up(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.AddColumn<bool>(
                name: "last_seen_flying",
                table: "characters",
                type: "boolean",
                nullable: false,
                defaultValue: false);

            migrationBuilder.AddColumn<double>(
                name: "last_system_x",
                table: "characters",
                type: "double precision",
                nullable: true);

            migrationBuilder.AddColumn<double>(
                name: "last_system_y",
                table: "characters",
                type: "double precision",
                nullable: true);

            migrationBuilder.AddColumn<double>(
                name: "last_system_z",
                table: "characters",
                type: "double precision",
                nullable: true);
        }

        /// <inheritdoc />
        protected override void Down(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.DropColumn(
                name: "last_seen_flying",
                table: "characters");

            migrationBuilder.DropColumn(
                name: "last_system_x",
                table: "characters");

            migrationBuilder.DropColumn(
                name: "last_system_y",
                table: "characters");

            migrationBuilder.DropColumn(
                name: "last_system_z",
                table: "characters");
        }
    }
}
