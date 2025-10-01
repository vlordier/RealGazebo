// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Sub-author: MinKyu Kim
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

using UnrealBuildTool;
using System.IO;

public class RealGazebo : ModuleRules
{
    public RealGazebo(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicIncludePaths.AddRange(new string[]
        {
            Path.Combine(ModuleDirectory, "Public", "Core"),
            Path.Combine(ModuleDirectory, "Public", "Data"),
        });

        PrivateIncludePaths.AddRange(new string[]
        {
            Path.Combine(ModuleDirectory, "Private", "Core"),
        });

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "UMG",
            "RealGazeboBridge",
            "RealGazeboUI"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "Slate",
            "SlateCore",
            "DeveloperSettings"
        });

        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.AddRange(new string[]
            {
                "EditorStyle",
                "ToolMenus",
                "Projects",
                "UnrealEd"
            });
        }

        PublicDefinitions.AddRange(new string[]
        {
            "REALGAZEBO_UNIFIED_MODULE=1"
        });

        if (Target.Configuration == UnrealTargetConfiguration.Debug ||
            Target.Configuration == UnrealTargetConfiguration.DebugGame)
        {
            PublicDefinitions.Add("REALGAZEBO_UNIFIED_DEBUG=1");
        }

        OptimizeCode = CodeOptimization.InShippingBuildsOnly;
    }
}