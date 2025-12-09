// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the GNU General Public License v3.0.
// See LICENSE file in the project root for full license information.

using UnrealBuildTool;
using System.IO;

public class RealGazeboStreaming : ModuleRules
{
	public RealGazeboStreaming(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// CRITICAL: Disable Unity builds to prevent BufferedPacket name collision
		// between Live555 (MultiFramedRTPSource.hh) and Unreal (PacketHandler.h)
		bUseUnity = false;

		PublicIncludePaths.AddRange(new string[]
		{
			Path.Combine(ModuleDirectory, "Public", "Core"),
			Path.Combine(ModuleDirectory, "Public", "Camera"),
			Path.Combine(ModuleDirectory, "Public", "Encoder"),
			Path.Combine(ModuleDirectory, "Public", "RTSP"),
			Path.Combine(ModuleDirectory, "Public", "Pipeline")
		});

		PrivateIncludePaths.AddRange(new string[]
		{
			Path.Combine(ModuleDirectory, "Private", "Core"),
			Path.Combine(ModuleDirectory, "Private", "Camera"),
			Path.Combine(ModuleDirectory, "Private", "Encoder"),
			Path.Combine(ModuleDirectory, "Private", "RTSP"),
			Path.Combine(ModuleDirectory, "Private", "Pipeline")
		});

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"RHI",
			"RenderCore",
			"AVEncoder",
			"Live555",
			"RealGazeboBridge" // For FVehicleID, UGazeboBridgeSubsystem integration
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate",
			"SlateCore",
			"DeveloperSettings",
			"CUDA" // For NVENC support
		});

		// Platform-specific encoder dependencies
		if (Target.Platform == UnrealTargetPlatform.Win64 ||
		    Target.Platform == UnrealTargetPlatform.Linux)
		{
			PrivateDependencyModuleNames.Add("VulkanRHI");
			PublicIncludePathModuleNames.Add("Vulkan");
		}

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"D3D11RHI",
				"D3D12RHI"
			});

			AddEngineThirdPartyPrivateStaticDependencies(Target, "DX11", "DX12");
		}

		if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
			PrivateDependencyModuleNames.Add("VulkanRHI");
		}

		// Build tool settings
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

		// Module-specific defines
		PublicDefinitions.AddRange(new string[]
		{
			"REALGAZEBO_STREAMING_ENABLED=1",
			"RTSP_DEFAULT_PORT=8554",
			"MAX_CONCURRENT_STREAMS=32",
			"DEFAULT_FRAME_POOL_SIZE=3"
		});

		// Debug defines
		if (Target.Configuration == UnrealTargetConfiguration.Debug ||
		    Target.Configuration == UnrealTargetConfiguration.DebugGame)
		{
			PublicDefinitions.Add("REALGAZEBO_STREAMING_DEBUG=1");
		}

		// Performance optimizations
		OptimizeCode = CodeOptimization.InShippingBuildsOnly;
		bEnableUndefinedIdentifierWarnings = false;
		bEnableExceptions = false;
	}
}
