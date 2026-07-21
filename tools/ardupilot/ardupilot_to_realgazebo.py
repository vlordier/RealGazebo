#!/usr/bin/env python3
"""Bridge ArduPilot MAVLink state into RealGazebo's existing UDP pose protocol."""
from __future__ import annotations

import argparse
import socket
import sys
import time
from dataclasses import dataclass
from typing import Iterable

from realgazebo_udp import Pose, encode_pose

try:
    from pymavlink import mavutil
except ImportError as exc:  # pragma: no cover
    raise SystemExit("pymavlink is required: python -m pip install pymavlink") from exc


@dataclass
class VehicleState:
    vehicle_num: int
    vehicle_type: int
    endpoint: str
    north_m: float = 0.0
    east_m: float = 0.0
    down_m: float = 0.0
    roll_rad: float = 0.0
    pitch_rad: float = 0.0
    yaw_rad: float = 0.0
    have_position: bool = False
    have_attitude: bool = False
    last_update_monotonic: float = 0.0

    def pose(self) -> Pose:
        return Pose(
            self.vehicle_num,
            self.vehicle_type,
            self.north_m,
            self.east_m,
            self.down_m,
            self.roll_rad,
            self.pitch_rad,
            self.yaw_rad,
        )


def parse_vehicle(spec: str, default_type: int) -> VehicleState:
    try:
        lhs, endpoint = spec.split("=", 1)
        if ":" in lhs:
            num_s, type_s = lhs.split(":", 1)
            vehicle_num, vehicle_type = int(num_s), int(type_s)
        else:
            vehicle_num, vehicle_type = int(lhs), default_type
    except ValueError as exc:
        raise argparse.ArgumentTypeError(
            f"invalid vehicle spec {spec!r}; use NUM=ENDPOINT or NUM:TYPE=ENDPOINT"
        ) from exc
    if not 0 <= vehicle_num <= 255 or not 0 <= vehicle_type <= 255:
        raise argparse.ArgumentTypeError("vehicle num/type must be 0..255")
    return VehicleState(vehicle_num, vehicle_type, endpoint)


def request_rates(master: object, hz: float) -> None:
    interval_us = int(1_000_000 / max(hz, 1.0))
    for message_id in (30, 32):
        try:
            master.mav.command_long_send(
                master.target_system,
                master.target_component,
                mavutil.mavlink.MAV_CMD_SET_MESSAGE_INTERVAL,
                0,
                message_id,
                interval_us,
                0,
                0,
                0,
                0,
                0,
            )
        except Exception:
            pass


def main(argv: Iterable[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--vehicle", action="append", required=True, metavar="NUM[:TYPE]=MAVLINK_ENDPOINT")
    parser.add_argument("--vehicle-type", type=int, default=0)
    parser.add_argument("--realgazebo-host", default="127.0.0.1")
    parser.add_argument("--realgazebo-port", type=int, default=5005)
    parser.add_argument("--rate", type=float, default=60.0)
    parser.add_argument("--stale-timeout", type=float, default=2.0)
    args = parser.parse_args(list(argv) if argv is not None else None)

    states = [parse_vehicle(spec, args.vehicle_type) for spec in args.vehicle]
    connections: list[tuple[VehicleState, object]] = []
    for state in states:
        master = mavutil.mavlink_connection(state.endpoint, autoreconnect=True)
        print(f"waiting heartbeat vehicle {state.vehicle_num} on {state.endpoint}", file=sys.stderr)
        master.wait_heartbeat(timeout=30)
        request_rates(master, args.rate)
        connections.append((state, master))

    udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    destination = (args.realgazebo_host, args.realgazebo_port)
    period = 1.0 / max(args.rate, 1.0)
    next_send = time.monotonic()

    while True:
        now = time.monotonic()
        for state, master in connections:
            while True:
                msg = master.recv_match(blocking=False)
                if msg is None:
                    break
                kind = msg.get_type()
                if kind == "LOCAL_POSITION_NED":
                    state.north_m = float(msg.x)
                    state.east_m = float(msg.y)
                    state.down_m = float(msg.z)
                    state.have_position = True
                    state.last_update_monotonic = now
                elif kind == "ATTITUDE":
                    state.roll_rad = float(msg.roll)
                    state.pitch_rad = float(msg.pitch)
                    state.yaw_rad = float(msg.yaw)
                    state.have_attitude = True
                    state.last_update_monotonic = now

        if now >= next_send:
            for state, _master in connections:
                if (
                    state.have_position
                    and state.have_attitude
                    and now - state.last_update_monotonic <= args.stale_timeout
                ):
                    udp.sendto(encode_pose(state.pose()), destination)
            next_send += period
            if next_send < now - period:
                next_send = now + period
        time.sleep(min(0.002, max(0.0, next_send - time.monotonic())))


if __name__ == "__main__":
    raise SystemExit(main())
