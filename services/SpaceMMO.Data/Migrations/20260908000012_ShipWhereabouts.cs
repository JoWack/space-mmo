using Microsoft.EntityFrameworkCore.Migrations;

#nullable disable

namespace SpaceMMO.Data.Migrations
{
    /// <inheritdoc />
    public partial class ShipWhereabouts : Migration
    {
        /// <inheritdoc />
        protected override void Up(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.AddColumn<double>(
                name: "deployed_system_x",
                table: "item_instances",
                type: "double precision",
                nullable: true);

            migrationBuilder.AddColumn<double>(
                name: "deployed_system_y",
                table: "item_instances",
                type: "double precision",
                nullable: true);

            migrationBuilder.AddColumn<double>(
                name: "deployed_system_z",
                table: "item_instances",
                type: "double precision",
                nullable: true);
        }

        /// <inheritdoc />
        protected override void Down(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.DropColumn(
                name: "deployed_system_x",
                table: "item_instances");

            migrationBuilder.DropColumn(
                name: "deployed_system_y",
                table: "item_instances");

            migrationBuilder.DropColumn(
                name: "deployed_system_z",
                table: "item_instances");
        }
    }
}
