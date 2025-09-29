// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Sub-author: MinKyu Kim
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

using UnrealBuildTool;
using System.IO;

public class RealGazeboStreaming : ModuleRules
{
    public RealGazeboStreaming(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicIncludePaths.AddRange(new string[]
        {
            Path.Combine(ModuleDirectory, "Public")
        });

        PrivateIncludePaths.AddRange(new string[]
        {
            Path.Combine(ModuleDirectory, "Private")
        });

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "RenderCore",
            "RHI",
            "RHICore",
            "Projects",
            "HTTP",
            "Json",
            "JsonUtilities",
            "RealGazeboBridge",
            "Live555"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "Slate",
            "SlateCore",
            "InputCore",
            "ImageWrapper",
            "ApplicationCore",
            "Renderer",
            "PixelCapture",
            "AVEncoder"
        });

        if (Target.bBuildEditor)
        {
            PublicDependencyModuleNames.AddRange(new string[]
            {
                "EditorStyle",
                "ToolMenus",
                "UnrealEd"
            });

            PrivateDependencyModuleNames.AddRange(new string[]
            {
                "WorkspaceMenuStructure",
                "PropertyEditor",
                "DesktopPlatform",
                "MainFrame"
            });
        }

        if (Target.Platform == UnrealTargetPlatform.Linux)
        {
            PrivateDependencyModuleNames.Add("UnixCommonStartup");
        }
        else if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PrivateDependencyModuleNames.AddRange(new string[]
            {
                "D3D12RHI",
                "D3D11RHI"
            });
        }

        PublicDefinitions.AddRange(new string[]
        {
            "WITH_REALGAZEBO_STREAMING=1",
            "REALGAZEBO_STREAMING_MODULE=1"
        });

        if (Target.Configuration == UnrealTargetConfiguration.Debug ||
            Target.Configuration == UnrealTargetConfiguration.DebugGame)
        {
            PublicDefinitions.Add("REALGAZEBO_STREAMING_DEBUG=1");
        }

        OptimizeCode = CodeOptimization.InShippingBuildsOnly;
    }
}