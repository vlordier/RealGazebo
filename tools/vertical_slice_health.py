#!/usr/bin/env python3
"""Health-check the RealGazebo headless vertical slice.

Checks:
- RTSP TCP listener
- browser gateway HTTP endpoint (optional)
- STANAG MPEG-TS UDP packets with sync byte/PID presence

Exit non-zero when a required stage is unhealthy.
"""
from __future__ import annotations

import argparse
import socket
import sys
import time
import urllib.error
import urllib.request


def check_tcp(host: str, port: int, timeout: float) -> tuple[bool, str]:
    try:
        with socket.create_connection((host, port), timeout=timeout):
            return True, f"tcp://{host}:{port} reachable"
    except OSError as exc:
        return False, f"tcp://{host}:{port} unavailable: {exc}"


def check_http(url: str, timeout: float) -> tuple[bool, str]:
    try:
        with urllib.request.urlopen(url, timeout=timeout) as response:
            code = int(response.status)
            if 200 <= code < 500:
                return True, f"{url} HTTP {code}"
            return False, f"{url} HTTP {code}"
    except (urllib.error.URLError, TimeoutError, OSError) as exc:
        return False, f"{url} unavailable: {exc}"


def check_stanag(bind_host: str, port: int, timeout: float) -> tuple[bool, str]:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        sock.bind((bind_host, port))
        sock.settimeout(timeout)
        deadline = time.monotonic() + timeout
        packets = 0
        pids: set[int] = set()
        while time.monotonic() < deadline:
            try:
                data, _addr = sock.recvfrom(65535)
            except socket.timeout:
                break
            # Sink sends one 188-byte TS packet per UDP datagram.
            if len(data) != 188 or data[0] != 0x47:
                continue
            packets += 1
            pid = ((data[1] & 0x1F) << 8) | data[2]
            pids.add(pid)
            if 0x101 in pids and 0x102 in pids:
                return True, f"udp://{bind_host}:{port} MPEG-TS alive ({packets} packets, video+KLV)"
        return False, f"udp://{bind_host}:{port} missing video/KLV TS traffic; packets={packets}, pids={sorted(pids)}"
    except OSError as exc:
        return False, f"udp://{bind_host}:{port} check failed: {exc}"
    finally:
        sock.close()


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--rtsp-host", default="127.0.0.1")
    p.add_argument("--rtsp-port", type=int, default=8554)
    p.add_argument("--stanag-bind", default="127.0.0.1")
    p.add_argument("--stanag-port", type=int, default=5000)
    p.add_argument("--browser-url", default="http://127.0.0.1:1984")
    p.add_argument("--no-browser", action="store_true")
    p.add_argument("--timeout", type=float, default=8.0)
    args = p.parse_args()

    checks = [
        ("RTSP", check_tcp(args.rtsp_host, args.rtsp_port, args.timeout)),
        ("STANAG", check_stanag(args.stanag_bind, args.stanag_port, args.timeout)),
    ]
    if not args.no_browser:
        checks.append(("Browser", check_http(args.browser_url, args.timeout)))

    failed = False
    for name, (ok, detail) in checks:
        print(f"[{'OK' if ok else 'FAIL'}] {name}: {detail}")
        failed |= not ok
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
