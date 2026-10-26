using Microsoft.EntityFrameworkCore.Migrations;

#nullable disable

namespace SpaceMMO.Data.Migrations
{
    /// <inheritdoc />
    public partial class Repair : Migration
    {
        /// <inheritdoc />
        protected override void Up(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.AddColumn<int>(
                name: "max_condition_hundredths",
                table: "item_instances",
                type: "integer",
                nullable: false,

                // Every item there is starts at a new one's maximum: nothing has been repaired before this. The
                // generated 0 would put every item with any condition outside ck_item_instances_condition_within_max.
                defaultValue: 10000);

            migrationBuilder.CreateTable(
                name: "repair_tabs",
                columns: table => new
                {
                    item_instance_id = table.Column<long>(type: "bigint", nullable: false),
                    item_def_id = table.Column<int>(type: "integer", nullable: false),
                    owed_ten_thousandths = table.Column<int>(type: "integer", nullable: false)
                },
                constraints: table =>
                {
                    table.PrimaryKey("pk_repair_tabs", x => new { x.item_instance_id, x.item_def_id });
                    table.CheckConstraint("ck_repair_tabs_owed_under_a_unit", "owed_ten_thousandths BETWEEN 0 AND 9999");
                    table.ForeignKey(
                        name: "fk_repair_tabs_item_defs_item_def_id",
                        column: x => x.item_def_id,
                        principalTable: "item_defs",
                        principalColumn: "id",
                        onDelete: ReferentialAction.Restrict);
                    table.ForeignKey(
                        name: "fk_repair_tabs_item_instances_item_instance_id",
                        column: x => x.item_instance_id,
                        principalTable: "item_instances",
                        principalColumn: "id",
                        onDelete: ReferentialAction.Restrict);
                });

            migrationBuilder.AddCheckConstraint(
                name: "ck_item_instances_condition_within_max",
                table: "item_instances",
                sql: "condition * 100 <= max_condition_hundredths");

            migrationBuilder.AddCheckConstraint(
                name: "ck_item_instances_max_condition_in_range",
                table: "item_instances",
                sql: "max_condition_hundredths BETWEEN 0 AND 10000");

            migrationBuilder.CreateIndex(
                name: "ix_repair_tabs_item_def_id",
                table: "repair_tabs",
                column: "item_def_id");
        }

        /// <inheritdoc />
        protected override void Down(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.DropTable(
                name: "repair_tabs");

            migrationBuilder.DropCheckConstraint(
                name: "ck_item_instances_condition_within_max",
                table: "item_instances");

            migrationBuilder.DropCheckConstraint(
                name: "ck_item_instances_max_condition_in_range",
                table: "item_instances");

            migrationBuilder.DropColumn(
                name: "max_condition_hundredths",
                table: "item_instances");
        }
    }
}
