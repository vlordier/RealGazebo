// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the GNU General Public License v3.0.
// See LICENSE file in the project root for full license information.

using UnrealBuildTool;
using System.IO;

public class RealGazeboUI : ModuleRules
{
    public RealGazeboUI(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicIncludePaths.AddRange(new string[]
        {
            Path.Combine(ModuleDirectory, "Public", "Core"),
            Path.Combine(ModuleDirectory, "Public", "Data"),
            Path.Combine(ModuleDirectory, "Public", "ViewerController"),
            Path.Combine(ModuleDirectory, "Public", "Widgets")
        });

        PrivateIncludePaths.AddRange(new string[]
        {
            Path.Combine(ModuleDirectory, "Private", "Core"),
            Path.Combine(ModuleDirectory, "Private", "Data"),
            Path.Combine(ModuleDirectory, "Private", "ViewerController"),
            Path.Combine(ModuleDirectory, "Private", "Widgets")
        });

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "InputCore",
            "UMG",
            "Slate",
            "SlateCore",
            "RealGazeboBridge"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "ApplicationCore"
        });

        if (Target.bBuildEditor)
        {
            PublicDependencyModuleNames.AddRange(new string[]
            {
                "UnrealEd",
                "ToolMenus"
            });

            PrivateDependencyModuleNames.AddRange(new string[]
            {
                "Projects"
            });
        }

        PublicDefinitions.AddRange(new string[]
        {
            "REALGAZEBO_CameraUI_MODULE=1"
        });

        if (Target.Configuration == UnrealTargetConfiguration.Debug ||
            Target.Configuration == UnrealTargetConfiguration.DebugGame)
        {
            PublicDefinitions.Add("REALGAZEBO_CameraUI_DEBUG=1");
        }

        OptimizeCode = CodeOptimization.InShippingBuildsOnly;
    }
}