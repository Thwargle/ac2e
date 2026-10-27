using UnrealBuildTool;

public class AC2UnrealTarget : TargetRules
{
    public AC2UnrealTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("AC2Client");
        bAllowEnginePluginsEnabledByDefault = false;
    }
}
