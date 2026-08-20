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
		ExtraModuleNames.Add("SpaceMMOBackend");

		// Editor-only authoring tools (task 96). Named here as well as in the .uproject so the
		// editor target builds it even when the project's module list is not consulted.
		ExtraModuleNames.Add("SpaceMMOAuthoring");
	}
}
