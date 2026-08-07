using Microsoft.EntityFrameworkCore.Migrations;

#nullable disable

namespace SpaceMMO.Data.Migrations
{
    /// <inheritdoc />
    public partial class DockedStation : Migration
    {
        /// <inheritdoc />
        protected override void Up(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.AddColumn<int>(
                name: "docked_station_id",
                table: "characters",
                type: "integer",
                nullable: true);

            migrationBuilder.CreateIndex(
                name: "ix_characters_docked_station_id",
                table: "characters",
                column: "docked_station_id");

            migrationBuilder.AddForeignKey(
                name: "fk_characters_stations_docked_station_id",
                table: "characters",
                column: "docked_station_id",
                principalTable: "stations",
                principalColumn: "id",
                onDelete: ReferentialAction.Restrict);
        }

        /// <inheritdoc />
        protected override void Down(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.DropForeignKey(
                name: "fk_characters_stations_docked_station_id",
                table: "characters");

            migrationBuilder.DropIndex(
                name: "ix_characters_docked_station_id",
                table: "characters");

            migrationBuilder.DropColumn(
                name: "docked_station_id",
                table: "characters");
        }
    }
}
