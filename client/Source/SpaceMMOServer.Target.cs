using UnrealBuildTool;

/// <summary>
/// The dedicated server — one process per star system, per ADR-0003.
/// </summary>
/// <remarks>
/// Defined from the very first commit even though only one system exists. The target existing
/// keeps the client from quietly accumulating code that cannot compile without a renderer, which
/// is a genuinely painful thing to discover late.
/// </remarks>
public class SpaceMMOServerTarget : TargetRules
{
	public SpaceMMOServerTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Server;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;

		ExtraModuleNames.Add("SpaceMMOCore");
	}
}
