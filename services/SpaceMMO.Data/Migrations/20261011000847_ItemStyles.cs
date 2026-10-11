using Microsoft.EntityFrameworkCore.Migrations;

#nullable disable

namespace SpaceMMO.Data.Migrations
{
    /// <inheritdoc />
    public partial class ItemStyles : Migration
    {
        /// <inheritdoc />
        protected override void Up(MigrationBuilder migrationBuilder)
        {
            // Task 187. Every new style column defaults to 'Common', not the empty string EF
            // generates, which no enum member parses from: every stack, instance, job and trade that
            // exists before this was made before anything recorded what it was made from, and Common
            // is the honest answer (Joe, 10 October 2026). Raw materials' stacks stay Common for good;
            // their world is the item's, which --seed writes onto item_defs.style.
            migrationBuilder.DropPrimaryKey(
                name: "pk_inventory_items",
                table: "inventory_items");

            migrationBuilder.DropPrimaryKey(
                name: "pk_industry_job_inputs",
                table: "industry_job_inputs");

            migrationBuilder.AddColumn<string>(
                name: "style",
                table: "trades",
                type: "character varying(48)",
                maxLength: 48,
                nullable: false,
                defaultValue: "Common");

            migrationBuilder.AddColumn<int>(
                name: "main_input_item_def_id",
                table: "recipes",
                type: "integer",
                nullable: true);

            migrationBuilder.AddColumn<string>(
                name: "style",
                table: "market_orders",
                type: "character varying(48)",
                maxLength: 48,
                nullable: true);

            migrationBuilder.AddColumn<string>(
                name: "style",
                table: "item_instances",
                type: "character varying(48)",
                maxLength: 48,
                nullable: false,
                defaultValue: "Common");

            migrationBuilder.AddColumn<string>(
                name: "style",
                table: "item_defs",
                type: "character varying(48)",
                maxLength: 48,
                nullable: false,
                defaultValue: "Common");

            migrationBuilder.AddColumn<string>(
                name: "style",
                table: "inventory_items",
                type: "character varying(48)",
                maxLength: 48,
                nullable: false,
                defaultValue: "Common");

            migrationBuilder.AddColumn<string>(
                name: "output_style",
                table: "industry_jobs",
                type: "character varying(48)",
                maxLength: 48,
                nullable: false,
                defaultValue: "Common");

            migrationBuilder.AddColumn<string>(
                name: "style",
                table: "industry_job_inputs",
                type: "character varying(48)",
                maxLength: 48,
                nullable: false,
                defaultValue: "Common");

            migrationBuilder.AddPrimaryKey(
                name: "pk_inventory_items",
                table: "inventory_items",
                columns: new[] { "inventory_id", "item_def_id", "style" });

            migrationBuilder.AddPrimaryKey(
                name: "pk_industry_job_inputs",
                table: "industry_job_inputs",
                columns: new[] { "industry_job_id", "item_def_id", "style" });

            migrationBuilder.CreateIndex(
                name: "ix_recipes_main_input_item_def_id",
                table: "recipes",
                column: "main_input_item_def_id");

            // Resting sell orders hold goods from a stack that is now Common, and the constraint
            // below requires a sell order to say which.
            migrationBuilder.Sql("UPDATE market_orders SET style = 'Common' WHERE side = 'Sell';");

            migrationBuilder.AddCheckConstraint(
                name: "ck_market_orders_style_matches_side",
                table: "market_orders",
                sql: "(side = 'Buy' AND style IS NULL) OR (side = 'Sell' AND style IS NOT NULL)");

            migrationBuilder.AddForeignKey(
                name: "fk_recipes_item_defs_main_input_item_def_id",
                table: "recipes",
                column: "main_input_item_def_id",
                principalTable: "item_defs",
                principalColumn: "id",
                onDelete: ReferentialAction.Restrict);
        }

        /// <inheritdoc />
        protected override void Down(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.DropForeignKey(
                name: "fk_recipes_item_defs_main_input_item_def_id",
                table: "recipes");

            migrationBuilder.DropIndex(
                name: "ix_recipes_main_input_item_def_id",
                table: "recipes");

            migrationBuilder.DropCheckConstraint(
                name: "ck_market_orders_style_matches_side",
                table: "market_orders");

            migrationBuilder.DropPrimaryKey(
                name: "pk_inventory_items",
                table: "inventory_items");

            migrationBuilder.DropPrimaryKey(
                name: "pk_industry_job_inputs",
                table: "industry_job_inputs");

            migrationBuilder.DropColumn(
                name: "style",
                table: "trades");

            migrationBuilder.DropColumn(
                name: "main_input_item_def_id",
                table: "recipes");

            migrationBuilder.DropColumn(
                name: "style",
                table: "market_orders");

            migrationBuilder.DropColumn(
                name: "style",
                table: "item_instances");

            migrationBuilder.DropColumn(
                name: "style",
                table: "item_defs");

            migrationBuilder.DropColumn(
                name: "style",
                table: "inventory_items");

            migrationBuilder.DropColumn(
                name: "output_style",
                table: "industry_jobs");

            migrationBuilder.DropColumn(
                name: "style",
                table: "industry_job_inputs");

            migrationBuilder.AddPrimaryKey(
                name: "pk_inventory_items",
                table: "inventory_items",
                columns: new[] { "inventory_id", "item_def_id" });

            migrationBuilder.AddPrimaryKey(
                name: "pk_industry_job_inputs",
                table: "industry_job_inputs",
                columns: new[] { "industry_job_id", "item_def_id" });
        }
    }
}
