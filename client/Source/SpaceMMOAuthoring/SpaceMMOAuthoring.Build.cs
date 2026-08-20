using UnrealBuildTool;

/// <summary>
/// Editor-only tools for authoring what lives in <c>data/</c>, graphically.
/// </summary>
/// <remarks>
/// <para>
/// Task 96. Directions from a body's centre are how deposits and stations are placed, and typing
/// unit vectors into JSON is a poor way to decide where something should stand. This module draws
/// the body, stands a marker on every authored thing, and writes the file back when they move.
/// </para>
/// <para>
/// <b>Editor type, deliberately.</b> Nothing here may reach a packaged client or the dedicated
/// server: an authoring tool that can run in the game is a second writer racing the seeder, which
/// is exactly what task 96 rejected when it decided the capture key would only print. The module
/// type is what enforces that, rather than a comment asking nicely.
/// </para>
/// <para>
/// It depends on SpaceMMOCore for the terrain function, and on nothing that talks to the backend.
/// The file on disk is the authority here — the API is downstream of it, and asking a running
/// server where a deposit is would be asking the copy rather than the original.
/// </para>
/// </remarks>
public class SpaceMMOAuthoring : ModuleRules
{
	public SpaceMMOAuthoring(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",

			// The terrain function, the globe tessellator and the planet configuration. The
			// preview is built by the same code the game builds its planet with, so a marker
			// cannot be standing on ground the game does not have.
			"SpaceMMOCore",

			// UDynamicMeshComponent for the preview globe, exactly as the planet actor does it.
			"GeometryCore",
			"GeometryFramework",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			// The panel, and the menu entry that opens it.
			"Slate",
			"SlateCore",
			"ToolMenus",
			"InputCore",

			// GEditor, the level editing viewport, transactions, and actor selection.
			"UnrealEd",
			"EditorFramework",
			"LevelEditor",
			"WorkspaceMenuStructure",

			// Reading data/universe/origin.json. Writing it back is deliberately not done through
			// a JSON writer -- see FSpaceMMOWorldDocument for why.
			"Json",
		});
	}
}
