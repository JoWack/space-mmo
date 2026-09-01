using Microsoft.EntityFrameworkCore.Migrations;

#nullable disable

namespace SpaceMMO.Data.Migrations
{
    /// <inheritdoc />
    public partial class ActiveShip : Migration
    {
        /// <inheritdoc />
        protected override void Up(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.AddColumn<long>(
                name: "active_ship_item_instance_id",
                table: "characters",
                type: "bigint",
                nullable: true);

            migrationBuilder.CreateIndex(
                name: "ix_characters_active_ship_item_instance_id",
                table: "characters",
                column: "active_ship_item_instance_id");

            migrationBuilder.AddForeignKey(
                name: "fk_characters_item_instances_active_ship_item_instance_id",
                table: "characters",
                column: "active_ship_item_instance_id",
                principalTable: "item_instances",
                principalColumn: "id",
                onDelete: ReferentialAction.Restrict);
        }

        /// <inheritdoc />
        protected override void Down(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.DropForeignKey(
                name: "fk_characters_item_instances_active_ship_item_instance_id",
                table: "characters");

            migrationBuilder.DropIndex(
                name: "ix_characters_active_ship_item_instance_id",
                table: "characters");

            migrationBuilder.DropColumn(
                name: "active_ship_item_instance_id",
                table: "characters");
        }
    }
}
