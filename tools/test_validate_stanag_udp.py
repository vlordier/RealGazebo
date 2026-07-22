#!/usr/bin/env python3
from __future__ import annotations

import unittest

from validate_stanag_udp import (
    KLV_KEY,
    KLV_PID,
    PAT_PID,
    PMT_PID,
    VIDEO_PID,
    analyze_datagrams,
    payload,
)


def ts_packet(pid: int, body: bytes, *, payload_start: bool = True, adaptation: int = 0) -> bytes:
    if adaptation < 0 or adaptation > 182:
        raise ValueError("invalid adaptation length")
    packet = bytearray([0x47, ((0x40 if payload_start else 0) | ((pid >> 8) & 0x1F)), pid & 0xFF, 0])
    if adaptation:
        packet[3] = 0x30
        packet.append(adaptation)
        packet.extend(b"\x00" * adaptation)
    else:
        packet[3] = 0x10
    remaining = 188 - len(packet)
    packet.extend(body[:remaining])
    packet.extend(b"\xff" * (188 - len(packet)))
    return bytes(packet)


class TransportAnalyzerTests(unittest.TestCase):
    def test_payload_plain(self) -> None:
        pkt = ts_packet(VIDEO_PID, b"abc")
        self.assertTrue(payload(pkt).startswith(b"abc"))

    def test_payload_with_adaptation(self) -> None:
        pkt = ts_packet(VIDEO_PID, b"xyz", adaptation=3)
        self.assertTrue(payload(pkt).startswith(b"xyz"))

    def test_payload_rejects_bad_length(self) -> None:
        self.assertEqual(payload(b"\x47" * 10), b"")

    def test_complete_transport_is_accepted(self) -> None:
        datagram = b"".join(
            [
                ts_packet(PAT_PID, b"pat"),
                ts_packet(PMT_PID, b"pmt"),
                ts_packet(VIDEO_PID, b"\x00\x00\x00\x01\x65video"),
                ts_packet(KLV_PID, KLV_KEY + b"metadata"),
            ]
        )
        result = analyze_datagrams([datagram])
        self.assertTrue(result.ok)
        self.assertEqual(result.missing, [])
        self.assertTrue(result.has_klv_key)

    def test_missing_klv_is_reported(self) -> None:
        datagram = b"".join(
            [
                ts_packet(PAT_PID, b"pat"),
                ts_packet(PMT_PID, b"pmt"),
                ts_packet(VIDEO_PID, b"video"),
            ]
        )
        result = analyze_datagrams([datagram])
        self.assertIn("KLV metadata", result.missing)
        self.assertFalse(result.ok)

    def test_bad_sync_is_reported(self) -> None:
        bad = bytearray(ts_packet(PAT_PID, b"pat"))
        bad[0] = 0
        result = analyze_datagrams([bytes(bad)])
        self.assertEqual(result.sync_errors, 1)
        self.assertFalse(result.ok)

    def test_partial_datagram_is_reported(self) -> None:
        result = analyze_datagrams([ts_packet(PAT_PID, b"pat") + b"x"])
        self.assertEqual(result.malformed_packets, 1)
        self.assertFalse(result.ok)


if __name__ == "__main__":
    unittest.main()
