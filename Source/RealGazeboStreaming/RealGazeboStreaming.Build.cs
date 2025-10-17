// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.


using UnrealBuildTool;
using System.IO;

public class RealGazeboStreaming : ModuleRules
{
	public RealGazeboStreaming(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// Public include paths (for module interface headers)
		PublicIncludePaths.AddRange(new string[]
		{
			Path.Combine(ModuleDirectory, "Public", "Core"),
			Path.Combine(ModuleDirectory, "Public", "Encoding"),
			Path.Combine(ModuleDirectory, "Public", "RTSP"),
			Path.Combine(ModuleDirectory, "Public", "Capture"),
			Path.Combine(ModuleDirectory, "Public", "Pipeline"),
			Path.Combine(ModuleDirectory, "Public", "Threading"),
			Path.Combine(ModuleDirectory, "Public", "Utils")
		});

		// Private include paths (for implementation files)
		PrivateIncludePaths.AddRange(new string[]
		{
			Path.Combine(ModuleDirectory, "Private", "Core"),
			Path.Combine(ModuleDirectory, "Private", "Encoding"),
			Path.Combine(ModuleDirectory, "Private", "RTSP"),
			Path.Combine(ModuleDirectory, "Private", "Capture"),
			Path.Combine(ModuleDirectory, "Private", "Pipeline"),
			Path.Combine(ModuleDirectory, "Private", "Threading"),
			Path.Combine(ModuleDirectory, "Private", "Utils")
		});

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"RenderCore",
			"RHI",
			"RHICore",
			"Renderer",
			"AVEncoder",       // UE5's hardware video encoding API (NVENC/AMF)
			"Slate",
			"SlateCore",
			"DeveloperSettings",
			"RealGazeboBridge" // For FVehicleID
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Live555",         // RTSP/RTP streaming library
			"VulkanRHI",       // Vulkan RHI (Linux + cross-platform texture interop)
			"CUDA"             // CUDA module for NVENC texture interop
		});

		// Platform-specific RHI and encoding modules
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"D3D11RHI",    // DirectX 11 RHI (Windows)
				"D3D12RHI"     // DirectX 12 RHI (Windows)
			});

			PublicDefinitions.Add("REALGAZEBO_PLATFORM_WINDOWS=1");
		}
		else if (Target.Platform == UnrealTargetPlatform.Linux)
		{
			PublicDefinitions.Add("REALGAZEBO_PLATFORM_LINUX=1");
		}

		// CUDA module for NVENC hardware encoding (optional - checked at runtime)
		// Only add if available to avoid build errors on systems without CUDA SDK
		if (Target.Platform == UnrealTargetPlatform.Win64 ||
		    Target.Platform == UnrealTargetPlatform.Linux)
		{
			// CUDA is optional - will be checked at runtime via FModuleManager::IsModuleLoaded("CUDA")
			PublicDefinitions.Add("REALGAZEBO_CUDA_SUPPORT=1");
		}

		// Enable SIMD optimizations
		PublicDefinitions.Add("REALGAZEBO_SIMD_ENABLED=1");

		// Debug mode settings (always define, 0 or 1)
		PublicDefinitions.Add("REALGAZEBO_STREAMING_DEBUG=" +
			((Target.Configuration == UnrealTargetConfiguration.Debug ||
			  Target.Configuration == UnrealTargetConfiguration.DebugGame) ? "1" : "0"));

		// Enable verbose logging in debug builds
		PublicDefinitions.Add("REALGAZEBO_STREAMING_VERBOSE_LOGGING=" +
			(Target.Configuration == UnrealTargetConfiguration.Debug ? "1" : "0"));
	}
}
