using Microsoft.EntityFrameworkCore.Migrations;

#nullable disable

namespace SpaceMMO.Data.Migrations
{
    /// <inheritdoc />
    public partial class BodyPositions : Migration
    {
        /// <inheritdoc />
        protected override void Up(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.AddColumn<double>(
                name: "system_x",
                table: "bodies",
                type: "double precision",
                nullable: true);

            migrationBuilder.AddColumn<double>(
                name: "system_y",
                table: "bodies",
                type: "double precision",
                nullable: true);

            migrationBuilder.AddColumn<double>(
                name: "system_z",
                table: "bodies",
                type: "double precision",
                nullable: true);
        }

        /// <inheritdoc />
        protected override void Down(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.DropColumn(
                name: "system_x",
                table: "bodies");

            migrationBuilder.DropColumn(
                name: "system_y",
                table: "bodies");

            migrationBuilder.DropColumn(
                name: "system_z",
                table: "bodies");
        }
    }
}
