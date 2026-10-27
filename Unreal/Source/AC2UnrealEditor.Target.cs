using UnrealBuildTool;

public class AC2UnrealEditorTarget : TargetRules
{
    public AC2UnrealEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("AC2Client");
    }
}
