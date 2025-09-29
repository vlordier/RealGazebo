// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Sub-author: MinKyu Kim
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

using UnrealBuildTool;
using System.IO;

public class RealGazeboBridge : ModuleRules
{
    public RealGazeboBridge(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicIncludePaths.AddRange(new string[]
        {
            Path.Combine(ModuleDirectory, "Public", "Core"),
            Path.Combine(ModuleDirectory, "Public", "Network"),
            Path.Combine(ModuleDirectory, "Public", "Vehicles")
        });

        PrivateIncludePaths.AddRange(new string[]
        {
            Path.Combine(ModuleDirectory, "Private", "Core"),
            Path.Combine(ModuleDirectory, "Private", "Network"),
            Path.Combine(ModuleDirectory, "Private", "Vehicles")
        });

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "Sockets",
            "Networking",
            "RHI",
            "RenderCore"
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
            "REALGAZEBO_BRIDGE_DEBUG=1"
        });

        if (Target.Configuration == UnrealTargetConfiguration.Debug ||
            Target.Configuration == UnrealTargetConfiguration.DebugGame)
        {
            PublicDefinitions.Add("REALGAZEBO_BRIDGE_DEBUG=1");
        }

        OptimizeCode = CodeOptimization.InShippingBuildsOnly;
    }
}