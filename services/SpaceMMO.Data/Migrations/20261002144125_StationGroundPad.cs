using Microsoft.EntityFrameworkCore.Migrations;

#nullable disable

namespace SpaceMMO.Data.Migrations
{
    /// <inheritdoc />
    public partial class StationGroundPad : Migration
    {
        /// <inheritdoc />
        protected override void Up(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.AddColumn<double>(
                name: "pad_blend_km",
                table: "stations",
                type: "double precision",
                nullable: true);

            migrationBuilder.AddColumn<double>(
                name: "pad_elevation_km",
                table: "stations",
                type: "double precision",
                nullable: true);

            migrationBuilder.AddColumn<double>(
                name: "pad_flat_radius_km",
                table: "stations",
                type: "double precision",
                nullable: true);
        }

        /// <inheritdoc />
        protected override void Down(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.DropColumn(
                name: "pad_blend_km",
                table: "stations");

            migrationBuilder.DropColumn(
                name: "pad_elevation_km",
                table: "stations");

            migrationBuilder.DropColumn(
                name: "pad_flat_radius_km",
                table: "stations");
        }
    }
}
