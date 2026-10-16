using Microsoft.EntityFrameworkCore.Migrations;

#nullable disable

namespace SpaceMMO.Data.Migrations
{
    /// <inheritdoc />
    public partial class Equipment : Migration
    {
        /// <inheritdoc />
        protected override void Up(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.AddColumn<string>(
                name: "equipped_slot",
                table: "item_instances",
                type: "character varying(48)",
                maxLength: 48,
                nullable: true);

            migrationBuilder.AddColumn<string>(
                name: "equips_as",
                table: "item_defs",
                type: "character varying(48)",
                maxLength: 48,
                nullable: true);

            migrationBuilder.CreateIndex(
                name: "ix_item_instances_one_per_slot",
                table: "item_instances",
                columns: new[] { "inventory_id", "equipped_slot" },
                unique: true,
                filter: "equipped_slot IS NOT NULL");
        }

        /// <inheritdoc />
        protected override void Down(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.DropIndex(
                name: "ix_item_instances_one_per_slot",
                table: "item_instances");

            migrationBuilder.DropColumn(
                name: "equipped_slot",
                table: "item_instances");

            migrationBuilder.DropColumn(
                name: "equips_as",
                table: "item_defs");
        }
    }
}
