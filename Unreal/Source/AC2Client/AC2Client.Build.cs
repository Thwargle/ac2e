using UnrealBuildTool;

public class AC2Client : ModuleRules
{
    public AC2Client(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        // Each DAT decoder/test owns file-local helpers. Unity merging must not
        // make their names collide depending on which files Git marks as changed.
        bUseUnity = false;
        PrivateIncludePaths.Add(ModuleDirectory);
        PublicDependencyModuleNames.AddRange(new[] {
            "Core", "CoreUObject", "Engine", "InputCore", "ProceduralMeshComponent"
        });
        // DAT layout trees and bitmap fonts are presented with Slate; local profiles use versioned JSON.
        PrivateDependencyModuleNames.AddRange(new[] { "ApplicationCore", "Json", "RenderCore", "Renderer", "RHI", "ImageWrapper", "ImageCore", "Slate", "SlateCore" });
        if(Target.Platform==UnrealTargetPlatform.Win64)PublicSystemLibraries.AddRange(new[] { "winhttp.lib", "advapi32.lib" });
        if (Target.bBuildEditor) PrivateDependencyModuleNames.AddRange(new[] { "UnrealEd", "UMG" }); // Offscreen Slate regression captures.
    }
}
