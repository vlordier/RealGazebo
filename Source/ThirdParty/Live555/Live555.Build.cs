// Copyright (c) 2024-2025 SUV Lab, Chungbuk National University
// Author    : Gonapinuwala Lahiru Sandaruwan
// Supervisor: Prof. SungTae Moon - Project lead & research supervision
// Licensed under the GNU General Public License v3.0.
// See LICENSE file in the project root for full license information.

using System.IO;
using UnrealBuildTool;

public class Live555 : ModuleRules
{
    public Live555(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.External;

        string Live555Path = ModuleDirectory;
        string LibraryPath;
        string IncludePath;
        string[] LibraryNames;

        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            IncludePath = Path.Combine(Live555Path, "Include");
            LibraryPath = Path.Combine(Live555Path, "lib", "Win64");
            LibraryNames = new[]
            {
                "liveMedia.lib",
                "groupsock.lib",
                "BasicUsageEnvironment.lib",
                "UsageEnvironment.lib"
            };
            PublicSystemLibraries.Add("ws2_32.lib");
            PublicDefinitions.Add("USE_IOCP=1");
            PublicDefinitions.Add("WINDOWS_NETWORKING_OPTIMIZED=1");
        }
        else if (Target.Platform == UnrealTargetPlatform.Linux)
        {
            IncludePath = Path.Combine(Live555Path, "Include");
            LibraryPath = Path.Combine(Live555Path, "lib", "Linux");
            LibraryNames = new[]
            {
                "libliveMedia.a",
                "libgroupsock.a",
                "libBasicUsageEnvironment.a",
                "libUsageEnvironment.a"
            };
            PublicSystemLibraries.Add("pthread");
            PublicDefinitions.Add("LINUX_NETWORKING_OPTIMIZED=1");
            PublicDefinitions.Add("USE_EPOLL=1");
        }
        else if (Target.Platform == UnrealTargetPlatform.Mac)
        {
            LibraryPath = Path.Combine(Live555Path, "lib", "Mac");
            IncludePath = Path.Combine(LibraryPath, "include");
            LibraryNames = new[]
            {
                "libliveMedia.a",
                "libgroupsock.a",
                "libBasicUsageEnvironment.a",
                "libUsageEnvironment.a"
            };
            PublicSystemLibraries.Add("pthread");
            PublicDefinitions.Add("MACOS_NETWORKING_OPTIMIZED=1");
        }
        else
        {
            throw new BuildException($"Live555 is not configured for target platform {Target.Platform}.");
        }

        string[] IncludeComponents =
        {
            "BasicUsageEnvironment",
            "UsageEnvironment",
            "groupsock",
            "liveMedia"
        };
        foreach (string Component in IncludeComponents)
        {
            string ComponentInclude = Path.Combine(IncludePath, Component);
            if (!Directory.Exists(ComponentInclude))
            {
                string Hint = Target.Platform == UnrealTargetPlatform.Mac
                    ? " Run tools/build_live555_macos.sh from the RealGazebo repository root."
                    : string.Empty;
                throw new BuildException($"Missing Live555 include directory: {ComponentInclude}.{Hint}");
            }
            PublicSystemIncludePaths.Add(ComponentInclude);
        }

        foreach (string LibraryName in LibraryNames)
        {
            string Library = Path.Combine(LibraryPath, LibraryName);
            if (!File.Exists(Library))
            {
                string Hint = Target.Platform == UnrealTargetPlatform.Mac
                    ? " Run tools/build_live555_macos.sh from the RealGazebo repository root."
                    : string.Empty;
                throw new BuildException($"Missing Live555 library: {Library}.{Hint}");
            }
            PublicAdditionalLibraries.Add(Library);
        }

        PublicDefinitions.Add("LIVE555_ENABLED=1");
        PublicDefinitions.Add("NO_OPENSSL=1");
        PublicDefinitions.Add("NO_SRTP=1");
        PublicDefinitions.Add("NO_AUDIO_SUPPORT=1");
        PublicDefinitions.Add("H264_VIDEO_ONLY=1");
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
        PublicDefinitions.Add("FAST_RTP_PROCESSING=1");
        PublicDefinitions.Add("VIDEO_BUFFER_OPTIMIZED=1");
        PublicDefinitions.Add("MEMORY_OPTIMIZED_VIDEO_ONLY=1");
        PublicDefinitions.Add("MAX_RTSP_CLIENTS=32");
        PublicDefinitions.Add("ENABLE_FRAME_POOLING=1");

        UndefinedIdentifierWarningLevel = WarningLevel.Off;
        bEnableExceptions = false;
    }
}
