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

        // LGPL v3 compliance - prominent notice (Section 4a)
        PublicDefinitions.AddRange(new string[]
        {
            "LIVE555_LGPL_V3_LICENSED=1",
            "LIVE555_VERSION=\"2025.09.17\"",
            "LIVE555_COPYRIGHT=\"Live555 © 1996-2025 Live Networks, Inc. LGPL v3\"",
            "LIVE555_LICENSE_INFO=\"See LICENSE file for license terms\""
        });

        // Live555 feature configuration
        PublicDefinitions.AddRange(new string[]
        {
            "LIVE555_VIDEO_ONLY=1",
            "LIVE555_MULTIPLE_STREAMS=1",
            "LIVE555_CROSS_PLATFORM=1",
            "NO_SSTREAM=1" // Avoid C++ sstream for compatibility
        });

        // Platform-specific socket definitions
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PublicDefinitions.Add("SOCKLEN_T=int");
            PublicSystemLibraries.AddRange(new string[] { "ws2_32.lib", "Iphlpapi.lib" });
        }
        else if (Target.Platform == UnrealTargetPlatform.Linux)
        {
            PublicDefinitions.Add("SOCKLEN_T=socklen_t");
        }

        // Live555 module paths
        string Live555Path = Path.Combine(ModuleDirectory);
        string IncludePath = Path.Combine(Live555Path, "Include");
        string LibPath = Path.Combine(Live555Path, "lib");

        // Add all include directories
        PublicIncludePaths.AddRange(new string[]
        {
            Path.Combine(IncludePath, "UsageEnvironment"),
            Path.Combine(IncludePath, "BasicUsageEnvironment"),
            Path.Combine(IncludePath, "groupsock"),
            Path.Combine(IncludePath, "liveMedia")
        });

        // Platform-specific library linking
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            string Win64LibPath = Path.Combine(LibPath, "Win64");
            PublicAdditionalLibraries.AddRange(new string[]
            {
                Path.Combine(Win64LibPath, "liveMedia.lib"),
                Path.Combine(Win64LibPath, "groupsock.lib"),
                Path.Combine(Win64LibPath, "BasicUsageEnvironment.lib"),
                Path.Combine(Win64LibPath, "UsageEnvironment.lib")
            });
        }
        else if (Target.Platform == UnrealTargetPlatform.Linux)
        {
            string LinuxLibPath = Path.Combine(LibPath, "Linux");
            PublicAdditionalLibraries.AddRange(new string[]
            {
                Path.Combine(LinuxLibPath, "libliveMedia.a"),
                Path.Combine(LinuxLibPath, "libgroupsock.a"),
                Path.Combine(LinuxLibPath, "libBasicUsageEnvironment.a"),
                Path.Combine(LinuxLibPath, "libUsageEnvironment.a")
            });
        }

        // Video streaming specific definitions
        PublicDefinitions.AddRange(new string[]
        {
            "LIVE555_VIDEO_STREAMING=1",
            "RTSP_CLIENT_ENABLED=1",
            "MULTIPLE_VIDEO_SESSIONS=1",

            // Video format support
            "SUPPORT_H264=1",
            "SUPPORT_H265=1",
            "SUPPORT_MJPEG=1",
            "SUPPORT_MPEG4=1",
            "SUPPORT_RAW_VIDEO=1",

            // Disable audio-related features
            "NO_AUDIO_CODECS=1",
            "DISABLE_MP3=1",
            "DISABLE_AAC=1",
            "DISABLE_AC3=1",
            "DISABLE_AMR=1",
            "DISABLE_GSM=1",
            "DISABLE_QCELP=1",
            "DISABLE_VORBIS=1"
        });

        // Cross-platform compatibility settings
        PublicDefinitions.AddRange(new string[]
        {
            "USE_SIGNALS=0",
            "NO_STD_LIB=0",
            "LIVE555_THREAD_SAFE=1",
            "ALLOW_RTSP_SERVER_PORT_REUSE=1",
            "ALLOW_SERVER_PORT_REUSE=1",
            "REUSE_FOR_TCP=1"
        });

        // Platform-specific compiler settings
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PublicDefinitions.AddRange(new string[]
            {
                "_CRT_SECURE_NO_WARNINGS=1",
                "WIN32_LEAN_AND_MEAN=1",
                "_WINSOCK_DEPRECATED_NO_WARNINGS=1",
                "_SCL_SECURE_NO_WARNINGS=1"
            });
        }
        else if (Target.Platform == UnrealTargetPlatform.Linux)
        {
            PublicDefinitions.Add("_GNU_SOURCE=1");
        }

        // Exclude unnecessary Live555 components
        PublicDefinitions.AddRange(new string[]
        {
            "EXCLUDE_AUDIO_SINKS=1",
            "EXCLUDE_AUDIO_SOURCES=1",
            "EXCLUDE_AUDIO_FILTERS=1",
            "EXCLUDE_TRANSCODING=1",
            "EXCLUDE_FILE_STREAMING=1",
            "EXCLUDE_SIP=1",
            "EXCLUDE_RTCP_RR=1"
        });

        // Debug and logging (disabled for production)
        PublicDefinitions.Add("DEBUG_LEVEL=0");
    }
}