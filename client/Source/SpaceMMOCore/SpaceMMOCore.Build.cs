using UnrealBuildTool;

/// <summary>
/// Core runtime systems: coordinates, physics grids, and flight.
/// </summary>
/// <remarks>
/// Kept free of gameplay so it can be reasoned about — and tested — on its own. Anything that
/// knows what a quest or an item is belongs in a gameplay module, not here.
/// </remarks>
public class SpaceMMOCore : ModuleRules
{
	public SpaceMMOCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",

			// Legacy axis and action bindings live here. See SpaceMMOShipPawn.h for why the
			// project is not on Enhanced Input yet.
			"InputCore",

			// Runtime mesh generation for terrain patches. GeometryFramework carries
			// UDynamicMeshComponent; GeometryCore carries the mesh itself. Both are runtime
			// modules, so they are available in a server build as well — though the server has no
			// reason to build a mesh, and does not.
			"GeometryCore",
			"GeometryFramework",
		});
	}
}
