#!/usr/bin/env python3
"""Generate go2rtc config for RealGazebo FPV streams."""
from __future__ import annotations

import argparse
from pathlib import Path


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--count", type=int, default=4)
    p.add_argument("--vehicle-type-name", default="x500")
    p.add_argument("--camera", default="fpv")
    p.add_argument("--rtsp-host", default="127.0.0.1")
    p.add_argument("--rtsp-port", type=int, default=8554)
    p.add_argument("--output", required=True)
    args = p.parse_args()

    if args.count < 1:
        raise SystemExit("--count must be >= 1")

    lines = ["streams:"]
    for i in range(args.count):
        name = f"{args.vehicle_type_name}_{i}_{args.camera}"
        path = f"{args.vehicle_type_name.lower()}_{i}/{args.camera}"
        lines.extend([
            f"  {name}:",
            f"    - rtsp://{args.rtsp_host}:{args.rtsp_port}/{path}",
        ])
    lines.extend([
        "",
        "api:",
        '  listen: ":1984"',
        "",
        "webrtc:",
        '  listen: ":8555/tcp"',
        "  candidates:",
        "    - 127.0.0.1:8555",
        "",
        "log:",
        "  level: info",
        "",
    ])
    Path(args.output).write_text("\n".join(lines), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
