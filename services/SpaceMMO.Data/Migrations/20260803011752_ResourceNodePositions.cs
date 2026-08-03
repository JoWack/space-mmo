using Microsoft.EntityFrameworkCore.Migrations;

#nullable disable

namespace SpaceMMO.Data.Migrations
{
    /// <inheritdoc />
    public partial class ResourceNodePositions : Migration
    {
        /// <inheritdoc />
        protected override void Up(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.AddColumn<double>(
                name: "direction_x",
                table: "resource_nodes",
                type: "double precision",
                nullable: false,
                defaultValue: 0.0);

            migrationBuilder.AddColumn<double>(
                name: "direction_y",
                table: "resource_nodes",
                type: "double precision",
                nullable: false,
                defaultValue: 0.0);

            migrationBuilder.AddColumn<double>(
                name: "direction_z",
                table: "resource_nodes",
                type: "double precision",
                nullable: false,
                defaultValue: 0.0);

            migrationBuilder.AddColumn<string>(
                name: "key",
                table: "resource_nodes",
                type: "text",
                nullable: false,
                defaultValue: "");

            migrationBuilder.CreateIndex(
                name: "ix_resource_nodes_key",
                table: "resource_nodes",
                column: "key",
                unique: true);

            migrationBuilder.AddCheckConstraint(
                name: "ck_resource_nodes_direction_nonzero",
                table: "resource_nodes",
                sql: "direction_x <> 0 OR direction_y <> 0 OR direction_z <> 0");
        }

        /// <inheritdoc />
        protected override void Down(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.DropIndex(
                name: "ix_resource_nodes_key",
                table: "resource_nodes");

            migrationBuilder.DropCheckConstraint(
                name: "ck_resource_nodes_direction_nonzero",
                table: "resource_nodes");

            migrationBuilder.DropColumn(
                name: "direction_x",
                table: "resource_nodes");

            migrationBuilder.DropColumn(
                name: "direction_y",
                table: "resource_nodes");

            migrationBuilder.DropColumn(
                name: "direction_z",
                table: "resource_nodes");

            migrationBuilder.DropColumn(
                name: "key",
                table: "resource_nodes");
        }
    }
}
