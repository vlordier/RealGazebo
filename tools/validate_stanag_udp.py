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

SYNC = 0x47
PAT_PID = 0x0000
PMT_PID = 0x0100
VIDEO_PID = 0x0101
KLV_PID = 0x0102
KLV_KEY = bytes.fromhex("060e2b34020b01010e01030101000000")


def payload(packet: bytes) -> bytes:
    afc = (packet[3] >> 4) & 0x3
    if afc == 1:
        return packet[4:]
    if afc == 3:
        n = packet[4]
        start = 5 + n
        return packet[start:] if start <= 188 else b""
    return b""


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="0.0.0.0")
    ap.add_argument("--port", type=int, default=5000)
    ap.add_argument("--seconds", type=float, default=10.0)
    args = ap.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((args.host, args.port))
    sock.settimeout(0.5)

    counts: Counter[int] = Counter()
    sync_errors = 0
    klv_buffer = bytearray()
    deadline = time.monotonic() + args.seconds

    while time.monotonic() < deadline:
        try:
            data, _ = sock.recvfrom(65535)
        except socket.timeout:
            continue
        for off in range(0, len(data) - 187, 188):
            pkt = data[off : off + 188]
            if pkt[0] != SYNC:
                sync_errors += 1
                continue
            pid = ((pkt[1] & 0x1F) << 8) | pkt[2]
            counts[pid] += 1
            if pid == KLV_PID:
                klv_buffer.extend(payload(pkt))
                if len(klv_buffer) > 1_000_000:
                    del klv_buffer[:-100_000]

    required = {
        PAT_PID: "PAT",
        PMT_PID: "PMT",
        VIDEO_PID: "H.264 video",
        KLV_PID: "KLV metadata",
    }
    missing = [name for pid, name in required.items() if counts[pid] == 0]
    has_klv_key = KLV_KEY in klv_buffer

    print("PID counts:", {hex(k): v for k, v in sorted(counts.items())})
    print("sync errors:", sync_errors)
    print("MISB UAS Local Set key found:", has_klv_key)

    if missing:
        print("FAIL missing:", ", ".join(missing))
        return 1
    if sync_errors:
        print("FAIL TS sync errors detected")
        return 1
    if not has_klv_key:
        print("FAIL KLV universal key not found")
        return 1
    print("PASS transport smoke test")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
