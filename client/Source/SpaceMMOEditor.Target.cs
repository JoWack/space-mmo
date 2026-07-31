using UnrealBuildTool;

/// <summary>
/// The editor, which is also what runs the automation tests.
/// </summary>
public class SpaceMMOEditorTarget : TargetRules
{
	public SpaceMMOEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;

		ExtraModuleNames.Add("SpaceMMOCore");
	}
}
