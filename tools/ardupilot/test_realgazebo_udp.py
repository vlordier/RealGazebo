#!/usr/bin/env python3
from __future__ import annotations

import math
import unittest

from realgazebo_udp import POSE_STRUCT, POSE_MESSAGE_ID, Pose, decode_wire_pose, encode_pose


class RealGazeboUdpTests(unittest.TestCase):
    def test_packet_size_and_header(self) -> None:
        packet = encode_pose(Pose(7, 2, 1.0, 2.0, -3.0, 0.0, 0.0, 0.0))
        self.assertEqual(len(packet), 31)
        num, typ, msg, _payload = decode_wire_pose(packet)
        self.assertEqual((num, typ, msg), (7, 2, POSE_MESSAGE_ID))
        self.assertEqual(POSE_STRUCT.size, 31)

    def test_ned_position_becomes_gazebo_enu(self) -> None:
        packet = encode_pose(Pose(0, 0, 10.0, 20.0, -30.0, 0.0, 0.0, 0.0))
        _num, _typ, _msg, payload = decode_wire_pose(packet)
        x, y, z = payload[:3]
        self.assertAlmostEqual(x, 20.0)  # east
        self.assertAlmostEqual(y, 10.0)  # north
        self.assertAlmostEqual(z, 30.0)  # up

    def test_level_north_faces_gazebo_positive_y(self) -> None:
        packet = encode_pose(Pose(0, 0, 0, 0, 0, 0, 0, 0))
        _n, _t, _m, payload = decode_wire_pose(packet)
        qx, qy, qz, qw = payload[3:]
        # +90 degrees yaw in ENU = facing north (+Y).
        self.assertAlmostEqual(qx, 0.0, places=6)
        self.assertAlmostEqual(qy, 0.0, places=6)
        self.assertAlmostEqual(abs(qz), math.sqrt(0.5), places=6)
        self.assertAlmostEqual(abs(qw), math.sqrt(0.5), places=6)

    def test_level_east_is_zero_gazebo_yaw(self) -> None:
        packet = encode_pose(Pose(0, 0, 0, 0, 0, 0, 0, math.pi / 2))
        _n, _t, _m, payload = decode_wire_pose(packet)
        qx, qy, qz, qw = payload[3:]
        self.assertAlmostEqual(qx, 0.0, places=6)
        self.assertAlmostEqual(qy, 0.0, places=6)
        self.assertAlmostEqual(qz, 0.0, places=6)
        self.assertAlmostEqual(abs(qw), 1.0, places=6)


if __name__ == "__main__":
    unittest.main()
