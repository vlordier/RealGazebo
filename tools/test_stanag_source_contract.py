#!/usr/bin/env python3
from __future__ import annotations

import re
import unittest
from pathlib import Path

from validate_stanag_udp import KLV_KEY, KLV_PID, PMT_PID, VIDEO_PID

ROOT = Path(__file__).resolve().parents[1]
CPP = ROOT / "Source/RealGazeboStreaming/Private/Transport/STANAG4609Sink.cpp"


def parse_cpp_uint16(name: str, source: str) -> int:
    match = re.search(
        rf"constexpr\s+uint16\s+{re.escape(name)}\s*=\s*(0x[0-9A-Fa-f]+|\d+)\s*;",
        source,
    )
    if not match:
        raise AssertionError(f"missing C++ constant {name}")
    return int(match.group(1), 0)


def parse_cpp_klv_key(source: str) -> bytes:
    match = re.search(
        r"static\s+const\s+uint8\s+Key\[16\]\s*=\s*\{([^}]+)\}",
        source,
    )
    if not match:
        raise AssertionError("missing C++ KLV key")
    values = [int(token.strip(), 0) for token in match.group(1).split(",")]
    if len(values) != 16:
        raise AssertionError(f"expected 16 KLV key bytes, got {len(values)}")
    return bytes(values)


class StanagSourceContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = CPP.read_text(encoding="utf-8")

    def test_pid_constants_match_validator(self) -> None:
        self.assertEqual(parse_cpp_uint16("PmtPid", self.source), PMT_PID)
        self.assertEqual(parse_cpp_uint16("VideoPid", self.source), VIDEO_PID)
        self.assertEqual(parse_cpp_uint16("KlvPid", self.source), KLV_PID)

    def test_klv_key_matches_validator(self) -> None:
        self.assertEqual(parse_cpp_klv_key(self.source), KLV_KEY)


if __name__ == "__main__":
    unittest.main()
