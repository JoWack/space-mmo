using Microsoft.EntityFrameworkCore.Migrations;

#nullable disable

namespace SpaceMMO.Data.Migrations
{
    /// <inheritdoc />
    public partial class BodyAppearance : Migration
    {
        /// <inheritdoc />
        protected override void Up(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.AddColumn<double>(
                name: "height_from",
                table: "bodies",
                type: "double precision",
                nullable: true);

            migrationBuilder.AddColumn<double>(
                name: "height_to",
                table: "bodies",
                type: "double precision",
                nullable: true);

            migrationBuilder.AddColumn<string>(
                name: "high_colour",
                table: "bodies",
                type: "text",
                nullable: true);

            migrationBuilder.AddColumn<string>(
                name: "low_colour",
                table: "bodies",
                type: "text",
                nullable: true);

            migrationBuilder.AddColumn<string>(
                name: "rock_colour",
                table: "bodies",
                type: "text",
                nullable: true);

            migrationBuilder.AddColumn<double>(
                name: "slope_from",
                table: "bodies",
                type: "double precision",
                nullable: true);

            migrationBuilder.AddColumn<double>(
                name: "slope_to",
                table: "bodies",
                type: "double precision",
                nullable: true);
        }

        /// <inheritdoc />
        protected override void Down(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.DropColumn(
                name: "height_from",
                table: "bodies");

            migrationBuilder.DropColumn(
                name: "height_to",
                table: "bodies");

            migrationBuilder.DropColumn(
                name: "high_colour",
                table: "bodies");

            migrationBuilder.DropColumn(
                name: "low_colour",
                table: "bodies");

            migrationBuilder.DropColumn(
                name: "rock_colour",
                table: "bodies");

            migrationBuilder.DropColumn(
                name: "slope_from",
                table: "bodies");

            migrationBuilder.DropColumn(
                name: "slope_to",
                table: "bodies");
        }
    }
}
