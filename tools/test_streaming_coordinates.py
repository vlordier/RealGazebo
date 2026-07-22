#!/usr/bin/env python3
"""Regression tests for RealGazebo's Gazebo->Unreal->ENU convention.

The ArduPilot adapter emits Gazebo X=East, Y=North, Z=Up. RealGazebo's existing
DataStreamProcessor maps Gazebo (X,Y,Z) to Unreal (X,-Y,Z), so the resulting
Unreal world is X=East, Y=-North (South), Z=Up.
"""

import unittest


def gazebo_to_unreal(east_m: float, north_m: float, up_m: float):
    return east_m * 100.0, -north_m * 100.0, up_m * 100.0


def unreal_to_enu(x_cm: float, y_cm: float, z_cm: float):
    return x_cm / 100.0, -y_cm / 100.0, z_cm / 100.0


def unreal_yaw_to_heading(yaw_deg: float) -> float:
    # Unreal yaw 0 points +X = East (heading 90). Positive yaw rotates toward
    # +Y = South, so true heading increases with yaw.
    return (90.0 + yaw_deg) % 360.0


class CoordinateConventionTests(unittest.TestCase):
    def test_round_trip(self):
        for enu in [(0.0, 0.0, 0.0), (12.5, -33.0, 7.2), (-5.0, 18.0, 100.0)]:
            self.assertEqual(tuple(round(v, 8) for v in unreal_to_enu(*gazebo_to_unreal(*enu))), enu)

    def test_east_moves_positive_unreal_x(self):
        x, y, _ = gazebo_to_unreal(10.0, 0.0, 0.0)
        self.assertGreater(x, 0)
        self.assertEqual(y, 0)

    def test_north_moves_negative_unreal_y(self):
        x, y, _ = gazebo_to_unreal(0.0, 10.0, 0.0)
        self.assertEqual(x, 0)
        self.assertLess(y, 0)

    def test_true_heading(self):
        self.assertEqual(unreal_yaw_to_heading(0.0), 90.0)     # East
        self.assertEqual(unreal_yaw_to_heading(90.0), 180.0)  # South
        self.assertEqual(unreal_yaw_to_heading(-90.0), 0.0)   # North
        self.assertEqual(unreal_yaw_to_heading(180.0), 270.0) # West


if __name__ == "__main__":
    unittest.main()
