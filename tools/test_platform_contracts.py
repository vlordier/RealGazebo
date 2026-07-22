#!/usr/bin/env python3
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class PlatformContractsTest(unittest.TestCase):
    def test_mac_live555_build_contract(self) -> None:
        build = (ROOT / "Source/ThirdParty/Live555/Live555.Build.cs").read_text()
        self.assertIn("UnrealTargetPlatform.Mac", build)
        self.assertIn('Path.Combine(Live555Path, "lib", "Mac")', build)
        self.assertIn('Path.Combine(LibraryPath, "include")', build)
        for name in (
            "libliveMedia.a",
            "libgroupsock.a",
            "libBasicUsageEnvironment.a",
            "libUsageEnvironment.a",
        ):
            self.assertIn(name, build)
        self.assertIn("tools/build_live555_macos.sh", build)

    def test_mac_live555_bootstrap_is_pinned(self) -> None:
        script = (ROOT / "tools/build_live555_macos.sh").read_text()
        self.assertIn("LIVE555_VERSION", script)
        self.assertIn("2026.07.08", script)
        self.assertIn("https://download.live555.com/", script)
        self.assertIn("./genMakefiles macosx", script)

    def test_stanag_pid_low_bytes_are_explicit(self) -> None:
        source = (
            ROOT
            / "Source/RealGazeboStreaming/Private/Transport/STANAG4609Sink.cpp"
        ).read_text()
        for pid in ("PmtPid", "VideoPid", "KlvPid"):
            self.assertIn(f"static_cast<uint8>({pid} & 0xFF)", source)


if __name__ == "__main__":
    unittest.main()
