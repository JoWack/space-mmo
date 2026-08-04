using UnrealBuildTool;

/// <summary>
/// Talks to the SpaceMMO backend over HTTP.
/// </summary>
/// <remarks>
/// A module of its own rather than part of SpaceMMOCore, because Core is deliberately free of
/// gameplay concepts and this is nothing but gameplay concepts — accounts, characters, skills,
/// inventory. Core stays something that can be reasoned about without knowing the game exists.
///
/// The split also keeps HTTP out of the flight and coordinate code entirely. Nothing that runs
/// per-frame should be able to reach for a network request by accident.
/// </remarks>
public class SpaceMMOBackend : ModuleRules
{
	public SpaceMMOBackend(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",

			// Backend depends on Core, never the reverse. Placing a deposit means asking
			// FPlanetTerrain where the ground is, and that maths belongs to Core. Core in turn
			// stays free of any notion that deposits, items, or a server exist.
			"SpaceMMOCore",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			// Private: callers get the subsystem's API, not the transport. Nothing outside this
			// module should be constructing an HTTP request against the game server.
			"HTTP",
			"Json",
		});
	}
}
