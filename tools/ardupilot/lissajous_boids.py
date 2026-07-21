#!/usr/bin/env python3
"""Drive an ArduPilot SITL swarm and optionally mirror state into RealGazebo.

One MAVLink connection is opened per vehicle and reused for both control and
state forwarding. This avoids assuming that a SITL TCP endpoint accepts two
independent clients.
"""
from __future__ import annotations

import argparse
import math
import socket
import sys
import time
from dataclasses import dataclass, field
from typing import Iterable

from realgazebo_udp import Pose, encode_pose

try:
    from pymavlink import mavutil
except ImportError as exc:  # pragma: no cover
    raise SystemExit("pymavlink is required: python -m pip install pymavlink") from exc


@dataclass
class Agent:
    index: int
    endpoint: str
    master: object
    vehicle_type: int = 0
    north: float = 0.0
    east: float = 0.0
    down: float = -10.0
    vn: float = 0.0
    ve: float = 0.0
    vd: float = 0.0
    roll: float = 0.0
    pitch: float = 0.0
    yaw: float = 0.0
    have_position: bool = False
    have_attitude: bool = False
    target: list[float] = field(default_factory=lambda: [0.0, 0.0, -10.0])

    def realgazebo_pose(self) -> Pose:
        return Pose(
            self.index,
            self.vehicle_type,
            self.north,
            self.east,
            self.down,
            self.roll,
            self.pitch,
            self.yaw,
        )


def parse_endpoint(spec: str) -> tuple[int, str]:
    try:
        idx_s, endpoint = spec.split("=", 1)
        return int(idx_s), endpoint
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"invalid vehicle {spec!r}; use INDEX=ENDPOINT") from exc


def set_guided(master: object) -> None:
    mapping = master.mode_mapping() or {}
    mode = mapping.get("GUIDED")
    if mode is None:
        raise RuntimeError("GUIDED mode not available")
    master.mav.set_mode_send(
        master.target_system,
        mavutil.mavlink.MAV_MODE_FLAG_CUSTOM_MODE_ENABLED,
        mode,
    )


def arm(master: object) -> None:
    master.mav.command_long_send(
        master.target_system,
        master.target_component,
        mavutil.mavlink.MAV_CMD_COMPONENT_ARM_DISARM,
        0,
        1,
        0,
        0,
        0,
        0,
        0,
        0,
    )


def request_state(master: object, hz: float) -> None:
    interval_us = int(1_000_000 / max(hz, 1.0))
    for message_id in (30, 32):  # ATTITUDE, LOCAL_POSITION_NED
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


def lissajous_reference(
    t: float,
    phase: float,
    radius_n: float,
    radius_e: float,
    altitude: float,
    vertical_amp: float,
    omega: float,
) -> tuple[float, float, float, float, float]:
    a, b, c = 1.0, 2.0, 3.0
    n = radius_n * math.sin(a * omega * t + phase)
    e = radius_e * math.sin(b * omega * t + 0.7 * phase + math.pi / 2.0)
    up = altitude + vertical_amp * math.sin(c * omega * t + 1.3 * phase)
    dn = radius_n * a * omega * math.cos(a * omega * t + phase)
    de = radius_e * b * omega * math.cos(b * omega * t + 0.7 * phase + math.pi / 2.0)
    return n, e, -up, dn, de


def separation_correction(
    agent: Agent,
    agents: list[Agent],
    min_distance: float,
    gain: float,
) -> tuple[float, float, float]:
    cn = ce = cd = 0.0
    for other in agents:
        if other is agent or not other.have_position:
            continue
        dn = agent.north - other.north
        de = agent.east - other.east
        dd = agent.down - other.down
        dist2 = dn * dn + de * de + dd * dd
        if dist2 < 1e-6:
            angle = (agent.index + 1) * 2.399963229728653
            cn += math.cos(angle) * gain
            ce += math.sin(angle) * gain
            continue
        dist = math.sqrt(dist2)
        if dist >= min_distance:
            continue
        strength = gain * (1.0 / dist - 1.0 / min_distance) / dist2
        cn += dn * strength
        ce += de * strength
        cd += dd * strength
    return cn, ce, cd


def cohesion_correction(agent: Agent, agents: list[Agent], gain: float) -> tuple[float, float, float]:
    active = [a for a in agents if a.have_position]
    if len(active) < 2 or gain <= 0.0:
        return 0.0, 0.0, 0.0
    cn = sum(a.north for a in active) / len(active)
    ce = sum(a.east for a in active) / len(active)
    cd = sum(a.down for a in active) / len(active)
    return gain * (cn - agent.north), gain * (ce - agent.east), gain * (cd - agent.down)


def send_position_target(agent: Agent, north: float, east: float, down: float, yaw: float) -> None:
    type_mask = (
        mavutil.mavlink.POSITION_TARGET_TYPEMASK_VX_IGNORE
        | mavutil.mavlink.POSITION_TARGET_TYPEMASK_VY_IGNORE
        | mavutil.mavlink.POSITION_TARGET_TYPEMASK_VZ_IGNORE
        | mavutil.mavlink.POSITION_TARGET_TYPEMASK_AX_IGNORE
        | mavutil.mavlink.POSITION_TARGET_TYPEMASK_AY_IGNORE
        | mavutil.mavlink.POSITION_TARGET_TYPEMASK_AZ_IGNORE
        | mavutil.mavlink.POSITION_TARGET_TYPEMASK_YAW_RATE_IGNORE
    )
    agent.master.mav.set_position_target_local_ned_send(
        int(time.monotonic() * 1000) & 0xFFFFFFFF,
        agent.master.target_system,
        agent.master.target_component,
        mavutil.mavlink.MAV_FRAME_LOCAL_NED,
        type_mask,
        north,
        east,
        down,
        0.0,
        0.0,
        0.0,
        0.0,
        0.0,
        0.0,
        yaw,
        0.0,
    )


def update_state(agent: Agent) -> None:
    while True:
        msg = agent.master.recv_match(blocking=False)
        if msg is None:
            return
        kind = msg.get_type()
        if kind == "LOCAL_POSITION_NED":
            agent.north = float(msg.x)
            agent.east = float(msg.y)
            agent.down = float(msg.z)
            agent.vn = float(msg.vx)
            agent.ve = float(msg.vy)
            agent.vd = float(msg.vz)
            if not agent.have_position:
                agent.target[:] = [agent.north, agent.east, agent.down]
            agent.have_position = True
        elif kind == "ATTITUDE":
            agent.roll = float(msg.roll)
            agent.pitch = float(msg.pitch)
            agent.yaw = float(msg.yaw)
            agent.have_attitude = True


def main(argv: Iterable[str] | None = None) -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--vehicle", action="append", required=True, metavar="INDEX=MAVLINK_ENDPOINT")
    p.add_argument("--vehicle-type", type=int, default=0)
    p.add_argument("--rate", type=float, default=20.0)
    p.add_argument("--state-rate", type=float, default=60.0)
    p.add_argument("--radius-north", type=float, default=35.0)
    p.add_argument("--radius-east", type=float, default=35.0)
    p.add_argument("--altitude", type=float, default=20.0)
    p.add_argument("--vertical-amplitude", type=float, default=5.0)
    p.add_argument("--period", type=float, default=80.0)
    p.add_argument("--min-separation", type=float, default=7.0)
    p.add_argument("--separation-gain", type=float, default=25.0)
    p.add_argument("--cohesion-gain", type=float, default=0.01)
    p.add_argument("--max-correction", type=float, default=12.0)
    p.add_argument("--smoothing", type=float, default=0.18)
    p.add_argument("--guided", action="store_true")
    p.add_argument("--arm", action="store_true")
    p.add_argument("--realgazebo-host")
    p.add_argument("--realgazebo-port", type=int, default=5005)
    args = p.parse_args(list(argv) if argv is not None else None)

    parsed = sorted((parse_endpoint(v) for v in args.vehicle), key=lambda item: item[0])
    agents: list[Agent] = []
    for idx, endpoint in parsed:
        master = mavutil.mavlink_connection(endpoint, autoreconnect=True)
        print(f"waiting heartbeat agent {idx}: {endpoint}", file=sys.stderr)
        heartbeat = master.wait_heartbeat(timeout=30)
        if heartbeat is None:
            raise RuntimeError(f"no heartbeat from {endpoint}")
        request_state(master, max(args.state_rate, 10.0))
        if args.guided:
            set_guided(master)
        if args.arm:
            arm(master)
        agents.append(Agent(idx, endpoint, master, vehicle_type=args.vehicle_type))

    rg_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM) if args.realgazebo_host else None
    rg_destination = (args.realgazebo_host, args.realgazebo_port) if args.realgazebo_host else None

    omega = 2.0 * math.pi / max(args.period, 1.0)
    dt = 1.0 / max(args.rate, 1.0)
    start = time.monotonic()
    next_tick = start

    while True:
        now = time.monotonic()
        for agent in agents:
            update_state(agent)
            if rg_socket and rg_destination and agent.have_position and agent.have_attitude:
                rg_socket.sendto(encode_pose(agent.realgazebo_pose()), rg_destination)

        if now >= next_tick:
            t = now - start
            count = max(len(agents), 1)
            for order, agent in enumerate(agents):
                if not agent.have_position:
                    continue
                phase = 2.0 * math.pi * order / count
                rn, re, rd, vn_ref, ve_ref = lissajous_reference(
                    t,
                    phase,
                    args.radius_north,
                    args.radius_east,
                    args.altitude,
                    args.vertical_amplitude,
                    omega,
                )
                sn, se, sd = separation_correction(agent, agents, args.min_separation, args.separation_gain)
                cn, ce, cd = cohesion_correction(agent, agents, args.cohesion_gain)
                correction_norm = math.sqrt((sn + cn) ** 2 + (se + ce) ** 2 + (sd + cd) ** 2)
                scale = min(1.0, args.max_correction / max(correction_norm, 1e-9))
                desired = (
                    rn + (sn + cn) * scale,
                    re + (se + ce) * scale,
                    rd + (sd + cd) * scale,
                )
                alpha = min(max(args.smoothing, 0.0), 1.0)
                for axis in range(3):
                    agent.target[axis] += alpha * (desired[axis] - agent.target[axis])
                yaw = math.atan2(ve_ref, vn_ref)
                send_position_target(agent, *agent.target, yaw)

            next_tick += dt
            if next_tick < now - dt:
                next_tick = now + dt
        time.sleep(min(0.002, max(0.0, next_tick - time.monotonic())))


if __name__ == "__main__":
    raise SystemExit(main())
