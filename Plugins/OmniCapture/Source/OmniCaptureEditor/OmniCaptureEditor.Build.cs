using UnrealBuildTool;

public class OmniCaptureEditor : ModuleRules
{
    public OmniCaptureEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "Slate",
            "SlateCore",
            "UMG",
            "OmniCapture"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "InputCore",
            "EditorStyle",
            "LevelEditor",
            "Projects",
            "PropertyEditor",
            "ToolMenus",
            "UnrealEd"
        });
    }
}
