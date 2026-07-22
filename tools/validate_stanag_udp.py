#!/usr/bin/env python3
"""Lightweight RealGazebo STANAG transport smoke validator.

Checks UDP MPEG-TS framing, PAT/PMT presence, H.264 PID traffic, KLV PID traffic,
and the MISB UAS Local Set universal key. This is a transport smoke test, not a
formal STANAG/MISP conformance test.
"""
from __future__ import annotations

import argparse
import socket
import time
from collections import Counter
from collections.abc import Iterable
from dataclasses import dataclass, field

SYNC = 0x47
PAT_PID = 0x0000
PMT_PID = 0x0100
VIDEO_PID = 0x0101
KLV_PID = 0x0102
KLV_KEY = bytes.fromhex("060e2b34020b01010e01030101000000")


@dataclass
class TransportAnalysis:
    counts: Counter[int] = field(default_factory=Counter)
    sync_errors: int = 0
    malformed_packets: int = 0
    klv_buffer: bytearray = field(default_factory=bytearray)

    @property
    def has_klv_key(self) -> bool:
        return KLV_KEY in self.klv_buffer

    @property
    def missing(self) -> list[str]:
        required = {
            PAT_PID: "PAT",
            PMT_PID: "PMT",
            VIDEO_PID: "H.264 video",
            KLV_PID: "KLV metadata",
        }
        return [name for pid, name in required.items() if self.counts[pid] == 0]

    @property
    def ok(self) -> bool:
        return (
            not self.missing
            and self.sync_errors == 0
            and self.malformed_packets == 0
            and self.has_klv_key
        )


def payload(packet: bytes) -> bytes:
    """Return MPEG-TS payload bytes, or empty bytes for malformed/no-payload packets."""
    if len(packet) != 188 or packet[0] != SYNC:
        return b""
    afc = (packet[3] >> 4) & 0x3
    if afc == 1:
        return packet[4:]
    if afc == 3:
        adaptation_length = packet[4]
        start = 5 + adaptation_length
        return packet[start:] if start <= 188 else b""
    return b""


def analyze_datagrams(datagrams: Iterable[bytes]) -> TransportAnalysis:
    """Analyze one or more UDP datagrams containing concatenated 188-byte TS packets."""
    result = TransportAnalysis()
    for data in datagrams:
        if len(data) % 188 != 0:
            result.malformed_packets += 1
        for off in range(0, len(data) - 187, 188):
            pkt = data[off : off + 188]
            if len(pkt) != 188:
                result.malformed_packets += 1
                continue
            if pkt[0] != SYNC:
                result.sync_errors += 1
                continue
            pid = ((pkt[1] & 0x1F) << 8) | pkt[2]
            result.counts[pid] += 1
            if pid == KLV_PID:
                result.klv_buffer.extend(payload(pkt))
                if len(result.klv_buffer) > 1_000_000:
                    del result.klv_buffer[:-100_000]
    return result


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="0.0.0.0")
    ap.add_argument("--port", type=int, default=5000)
    ap.add_argument("--seconds", type=float, default=10.0)
    args = ap.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((args.host, args.port))
    sock.settimeout(0.5)

    datagrams: list[bytes] = []
    deadline = time.monotonic() + args.seconds
    while time.monotonic() < deadline:
        try:
            data, _ = sock.recvfrom(65535)
        except socket.timeout:
            continue
        datagrams.append(data)

    result = analyze_datagrams(datagrams)
    print("PID counts:", {hex(k): v for k, v in sorted(result.counts.items())})
    print("sync errors:", result.sync_errors)
    print("malformed datagrams:", result.malformed_packets)
    print("MISB UAS Local Set key found:", result.has_klv_key)

    if result.missing:
        print("FAIL missing:", ", ".join(result.missing))
        return 1
    if result.sync_errors:
        print("FAIL TS sync errors detected")
        return 1
    if result.malformed_packets:
        print("FAIL malformed UDP datagram length")
        return 1
    if not result.has_klv_key:
        print("FAIL KLV universal key not found")
        return 1
    print("PASS transport smoke test")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
