using UnrealBuildTool;

/// <summary>
/// The packaged game client.
/// </summary>
public class SpaceMMOTarget : TargetRules
{
	public SpaceMMOTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;

		ExtraModuleNames.Add("SpaceMMOCore");
		ExtraModuleNames.Add("SpaceMMOBackend");
	}
}
