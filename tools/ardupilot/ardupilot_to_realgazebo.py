#!/usr/bin/env python3
"""Bridge ArduPilot MAVLink state into RealGazebo's existing UDP pose protocol.

Input per vehicle:
  - LOCAL_POSITION_NED (x=north, y=east, z=down; metres)
  - ATTITUDE (roll, pitch, yaw; radians, aerospace NED/FRD convention)

Output RealGazebo MessageID=1 (31 bytes, little-endian):
  uint8 vehicle_num, uint8 vehicle_type, uint8 message_id=1,
  float32 gazebo_x/east, gazebo_y/north, gazebo_z/up,
  float32 quaternion x,y,z,w in Gazebo ENU/FLU convention.

RealGazebo itself performs the final Gazebo -> Unreal conversion, so this tool
must NOT emit Unreal centimetres or left-handed coordinates.
"""
from __future__ import annotations

import argparse
import math
import socket
import struct
import sys
import time
from dataclasses import dataclass
from typing import Iterable

try:
    from pymavlink import mavutil
except ImportError as exc:  # pragma: no cover - runtime dependency guard
    raise SystemExit("pymavlink is required: python -m pip install pymavlink") from exc

POSE_STRUCT = struct.Struct("<BBBfffffff")
POSE_MESSAGE_ID = 1


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


def euler_zyx_to_quaternion(roll: float, pitch: float, yaw: float) -> tuple[float, float, float, float]:
    """Return quaternion x,y,z,w for intrinsic ZYX yaw/pitch/roll."""
    cr, sr = math.cos(roll * 0.5), math.sin(roll * 0.5)
    cp, sp = math.cos(pitch * 0.5), math.sin(pitch * 0.5)
    cy, sy = math.cos(yaw * 0.5), math.sin(yaw * 0.5)
    w = cr * cp * cy + sr * sp * sy
    x = sr * cp * cy - cr * sp * sy
    y = cr * sp * cy + sr * cp * sy
    z = cr * cp * sy - sr * sp * cy
    norm = math.sqrt(x * x + y * y + z * z + w * w)
    if norm <= 1e-12:
        return 0.0, 0.0, 0.0, 1.0
    return x / norm, y / norm, z / norm, w / norm


def ned_frd_attitude_to_gazebo_quaternion(
    roll_ned: float, pitch_ned: float, yaw_ned: float
) -> tuple[float, float, float, float]:
    """Convert aerospace NED/FRD Euler attitude to Gazebo ENU/FLU.

    With Gazebo ENU axes X=east, Y=north, Z=up:
      yaw_gazebo = pi/2 - yaw_ned
      pitch_gazebo = -pitch_ned
      roll_gazebo = roll_ned

    This gives level/north -> +Y, level/east -> +X, while preserving aerospace
    positive roll and converting positive nose-up pitch to the FLU convention.
    """
    return euler_zyx_to_quaternion(
        roll_ned,
        -pitch_ned,
        math.pi * 0.5 - yaw_ned,
    )


def encode_pose(state: VehicleState) -> bytes:
    qx, qy, qz, qw = ned_frd_attitude_to_gazebo_quaternion(
        state.roll_rad, state.pitch_rad, state.yaw_rad
    )
    return POSE_STRUCT.pack(
        state.vehicle_num,
        state.vehicle_type,
        POSE_MESSAGE_ID,
        state.east_m,   # Gazebo X = East
        state.north_m,  # Gazebo Y = North
        -state.down_m,  # Gazebo Z = Up
        qx,
        qy,
        qz,
        qw,
    )


def parse_vehicle(spec: str, default_type: int) -> VehicleState:
    """Parse NUM=ENDPOINT or NUM:TYPE=ENDPOINT."""
    try:
        lhs, endpoint = spec.split("=", 1)
        if ":" in lhs:
            num_s, type_s = lhs.split(":", 1)
            vehicle_num = int(num_s)
            vehicle_type = int(type_s)
        else:
            vehicle_num = int(lhs)
            vehicle_type = default_type
    except ValueError as exc:
        raise argparse.ArgumentTypeError(
            f"invalid vehicle spec {spec!r}; use NUM=ENDPOINT or NUM:TYPE=ENDPOINT"
        ) from exc
    if not 0 <= vehicle_num <= 255 or not 0 <= vehicle_type <= 255:
        raise argparse.ArgumentTypeError("vehicle num/type must be 0..255")
    return VehicleState(vehicle_num, vehicle_type, endpoint)


def request_rates(master: object, hz: float) -> None:
    """Best-effort request of MAVLink state messages at the desired rate."""
    interval_us = int(1_000_000 / max(hz, 1.0))
    for message_id in (30, 32):  # ATTITUDE, LOCAL_POSITION_NED
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
    parser.add_argument(
        "--vehicle",
        action="append",
        required=True,
        metavar="NUM[:TYPE]=MAVLINK_ENDPOINT",
        help="e.g. 0=udp:127.0.0.1:14550 or 1:2=tcp:127.0.0.1:5762",
    )
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
                fresh = now - state.last_update_monotonic <= args.stale_timeout
                if state.have_position and state.have_attitude and fresh:
                    packet = encode_pose(state)
                    assert len(packet) == 31
                    udp.sendto(packet, destination)
            next_send += period
            if next_send < now - period:
                next_send = now + period
        time.sleep(min(0.002, max(0.0, next_send - time.monotonic())))


if __name__ == "__main__":
    raise SystemExit(main())
