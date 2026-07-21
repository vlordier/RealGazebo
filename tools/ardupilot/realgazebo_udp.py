#!/usr/bin/env python3
"""Pure-Python RealGazebo UDP pose protocol helpers."""
from __future__ import annotations

import math
import struct
from dataclasses import dataclass

POSE_STRUCT = struct.Struct("<BBBfffffff")
POSE_MESSAGE_ID = 1


@dataclass(frozen=True)
class Pose:
    vehicle_num: int
    vehicle_type: int
    north_m: float
    east_m: float
    down_m: float
    roll_rad: float
    pitch_rad: float
    yaw_rad: float


def euler_zyx_to_quaternion(roll: float, pitch: float, yaw: float) -> tuple[float, float, float, float]:
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
    """Convert aerospace NED/FRD attitude to Gazebo ENU/FLU quaternion.

    Gazebo world axes used by RealGazebo: X=east, Y=north, Z=up.
    Gazebo body convention: X=forward, Y=left, Z=up.
    """
    return euler_zyx_to_quaternion(
        roll_ned,
        -pitch_ned,
        math.pi * 0.5 - yaw_ned,
    )


def encode_pose(pose: Pose) -> bytes:
    if not 0 <= pose.vehicle_num <= 255 or not 0 <= pose.vehicle_type <= 255:
        raise ValueError("vehicle num/type must be in 0..255")
    qx, qy, qz, qw = ned_frd_attitude_to_gazebo_quaternion(
        pose.roll_rad, pose.pitch_rad, pose.yaw_rad
    )
    packet = POSE_STRUCT.pack(
        pose.vehicle_num,
        pose.vehicle_type,
        POSE_MESSAGE_ID,
        pose.east_m,
        pose.north_m,
        -pose.down_m,
        qx,
        qy,
        qz,
        qw,
    )
    if len(packet) != 31:
        raise AssertionError(f"unexpected RealGazebo pose size: {len(packet)}")
    return packet


def decode_wire_pose(packet: bytes) -> tuple[int, int, int, tuple[float, ...]]:
    if len(packet) != POSE_STRUCT.size:
        raise ValueError(f"pose packet must be {POSE_STRUCT.size} bytes")
    values = POSE_STRUCT.unpack(packet)
    return values[0], values[1], values[2], tuple(values[3:])
