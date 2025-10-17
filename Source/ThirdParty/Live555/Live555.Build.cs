// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the BSD-3-Clause License.
// See LICENSE file in the project root for full license information.

/*
 * Live555 – LGPL v3 Compliance Notes
 * ----------------------------------
 * This Unreal Engine plugin links against Live555, which is licensed under the GNU LGPL
 * v3. Upstream source: http://www.live555.com/
 * Version integrated here: 2025.09.17
 *
 * LGPL v3 Compliance:
 * - License text: See LICENSE file in this directory
 * - User relinking rights: Supported (standard LGPL v3 rights apply)
 *
 * No Source Modifications:
 * We link against unmodified Live555 libraries as provided by upstream.
 *
 * Attribution:
 * © 1996-2025 Live Networks, Inc. Licensed under GNU LGPL v3.
 */

using System.IO;
using UnrealBuildTool;

public class Live555 : ModuleRules
{
	public Live555(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		string Live555Path = ModuleDirectory;
		string IncludePath = Path.Combine(Live555Path, "Include");

		// Add include paths for each Live555 component
		PublicSystemIncludePaths.Add(Path.Combine(IncludePath, "BasicUsageEnvironment"));
		PublicSystemIncludePaths.Add(Path.Combine(IncludePath, "UsageEnvironment"));
		PublicSystemIncludePaths.Add(Path.Combine(IncludePath, "groupsock"));
		PublicSystemIncludePaths.Add(Path.Combine(IncludePath, "liveMedia"));

		// Platform-specific library configuration
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			string LibraryPath = Path.Combine(Live555Path, "lib", "Win64");

			// Order matters: link in dependency order
			PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "liveMedia.lib"));
			PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "groupsock.lib"));
			PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "BasicUsageEnvironment.lib"));
			PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "UsageEnvironment.lib"));

			// Windows socket library
			PublicSystemLibraries.Add("ws2_32.lib");
		}
		else if (Target.Platform == UnrealTargetPlatform.Linux)
		{
			string LibraryPath = Path.Combine(Live555Path, "lib", "Linux");

			// Order matters: link in dependency order
			PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "libliveMedia.a"));
			PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "libgroupsock.a"));
			PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "libBasicUsageEnvironment.a"));
			PublicAdditionalLibraries.Add(Path.Combine(LibraryPath, "libUsageEnvironment.a"));

			// Linux system libraries
			PublicSystemLibraries.Add("pthread");
		}

		// ===== Live555 Configuration Defines =====

		// Enable Live555 integration
		PublicDefinitions.Add("LIVE555_ENABLED=1");

		// Disable SSL/TLS support (no OpenSSL dependency)
		PublicDefinitions.Add("NO_OPENSSL=1");

		// Disable SRTP
		PublicDefinitions.Add("NO_SRTP=1");

		// Video-only mode
		PublicDefinitions.Add("NO_AUDIO_SUPPORT=1");

		// Optimize for H.264 video streaming
		PublicDefinitions.Add("H264_VIDEO_ONLY=1");

		// Disable unused media formats
		PublicDefinitions.Add("NO_MPEG=1");
		PublicDefinitions.Add("NO_DV=1");
		PublicDefinitions.Add("NO_AC3=1");
		PublicDefinitions.Add("NO_AMR=1");
		PublicDefinitions.Add("NO_ADTS=1");
		PublicDefinitions.Add("NO_MP3=1");
		PublicDefinitions.Add("NO_WAV=1");
		PublicDefinitions.Add("NO_QCELP=1");
		PublicDefinitions.Add("NO_GSM=1");
		PublicDefinitions.Add("NO_VORBIS=1");
		PublicDefinitions.Add("NO_THEORA=1");
		PublicDefinitions.Add("NO_OPUS=1");
		PublicDefinitions.Add("NO_VP8=1");
		PublicDefinitions.Add("NO_VP9=1");

		// Performance optimizations
		PublicDefinitions.Add("FAST_RTP_PROCESSING=1");
		PublicDefinitions.Add("VIDEO_BUFFER_OPTIMIZED=1");

		// Compiler settings
		bEnableUndefinedIdentifierWarnings = false;
		bEnableExceptions = false;

		// Platform-specific optimizations
		if (Target.Platform == UnrealTargetPlatform.Linux)
		{
			PublicDefinitions.Add("LINUX_NETWORKING_OPTIMIZED=1");
			PublicDefinitions.Add("USE_EPOLL=1");
		}
		else if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicDefinitions.Add("USE_IOCP=1");
			PublicDefinitions.Add("WINDOWS_NETWORKING_OPTIMIZED=1");
		}

		// Memory optimizations
		PublicDefinitions.Add("MEMORY_OPTIMIZED_VIDEO_ONLY=1");
		PublicDefinitions.Add("MAX_RTSP_CLIENTS=32");
		PublicDefinitions.Add("ENABLE_FRAME_POOLING=1");
	}
}
