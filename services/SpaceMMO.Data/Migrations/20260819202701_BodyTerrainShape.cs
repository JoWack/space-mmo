using Microsoft.EntityFrameworkCore.Migrations;

#nullable disable

namespace SpaceMMO.Data.Migrations
{
    /// <inheritdoc />
    public partial class BodyTerrainShape : Migration
    {
        /// <inheritdoc />
        protected override void Up(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.AddColumn<double>(
                name: "base_frequency",
                table: "bodies",
                type: "double precision",
                nullable: true);

            migrationBuilder.AddColumn<double>(
                name: "max_elevation_km",
                table: "bodies",
                type: "double precision",
                nullable: true);

            migrationBuilder.AddColumn<long>(
                name: "terrain_seed",
                table: "bodies",
                type: "bigint",
                nullable: true);
        }

        /// <inheritdoc />
        protected override void Down(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.DropColumn(
                name: "base_frequency",
                table: "bodies");

            migrationBuilder.DropColumn(
                name: "max_elevation_km",
                table: "bodies");

            migrationBuilder.DropColumn(
                name: "terrain_seed",
                table: "bodies");
        }
    }
}
