using Microsoft.EntityFrameworkCore.Migrations;

#nullable disable

namespace SpaceMMO.Data.Migrations
{
    /// <inheritdoc />
    public partial class StationPositions : Migration
    {
        /// <inheritdoc />
        protected override void Up(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.AddColumn<double>(
                name: "direction_x",
                table: "stations",
                type: "double precision",
                nullable: true);

            migrationBuilder.AddColumn<double>(
                name: "direction_y",
                table: "stations",
                type: "double precision",
                nullable: true);

            migrationBuilder.AddColumn<double>(
                name: "direction_z",
                table: "stations",
                type: "double precision",
                nullable: true);

            // Five kilometres rather than the generated zero. A zero range is a station nobody can
            // dock at, which is the one value the content validator refuses to accept — and an
            // existing row would have carried it silently until somebody re-seeded, presenting as
            // a broken station rather than as an unmigrated one.
            migrationBuilder.AddColumn<double>(
                name: "docking_range_kilometres",
                table: "stations",
                type: "double precision",
                nullable: false,
                defaultValue: 5.0);

            migrationBuilder.AddColumn<double>(
                name: "system_x",
                table: "stations",
                type: "double precision",
                nullable: true);

            migrationBuilder.AddColumn<double>(
                name: "system_y",
                table: "stations",
                type: "double precision",
                nullable: true);

            migrationBuilder.AddColumn<double>(
                name: "system_z",
                table: "stations",
                type: "double precision",
                nullable: true);
        }

        /// <inheritdoc />
        protected override void Down(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.DropColumn(
                name: "direction_x",
                table: "stations");

            migrationBuilder.DropColumn(
                name: "direction_y",
                table: "stations");

            migrationBuilder.DropColumn(
                name: "direction_z",
                table: "stations");

            migrationBuilder.DropColumn(
                name: "docking_range_kilometres",
                table: "stations");

            migrationBuilder.DropColumn(
                name: "system_x",
                table: "stations");

            migrationBuilder.DropColumn(
                name: "system_y",
                table: "stations");

            migrationBuilder.DropColumn(
                name: "system_z",
                table: "stations");
        }
    }
}
