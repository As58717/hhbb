using UnrealBuildTool;

public class OmniCapture : ModuleRules
{
    public OmniCapture(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
        bUseUnity = false;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "RenderCore",
            "RHI",
            "Projects",
            "Slate",
            "SlateCore",
            "UMG",
            "ImageWriteQueue",
            "Json",
            "JsonUtilities"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "ApplicationCore",
            "CinematicCamera",
            "InputCore",
            "HeadMountedDisplay",
            "AudioMixer",
            "AudioExtensions",
            "DeveloperSettings",
            "ImageCore"
        });

        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PrivateDefinitions.Add("WITH_OMNI_NVENC=1");
            PublicDependencyModuleNames.AddRange(new string[]
            {
                "D3D11RHI",
                "D3D12RHI",
                "NVENC",
                "AVEncoder"
            });
        }
        else
        {
            PrivateDefinitions.Add("WITH_OMNI_NVENC=0");
        }
    }
}
