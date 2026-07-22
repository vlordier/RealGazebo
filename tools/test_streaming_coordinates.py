#!/usr/bin/env python3
"""Regression tests for RealGazebo's Gazebo->Unreal->ENU coordinate convention.

DataStreamProcessor converts Gazebo ENU to Unreal as:
  Unreal X = East_gazebo? No: source Gazebo X/Y are mapped as X, -Y.
For the ArduPilot bridge we intentionally emit Gazebo X=East, Y=North, Z=Up,
therefore the resulting Unreal world is X=East, Y=-North, Z=Up.

The STANAG metadata normalization must invert the actual bridge mapping exactly.
"""

import unittest


def gazebo_to_unreal(east_m: float, north_m: float, up_m: float):
    # Mirrors UDataStreamProcessor::ConvertGazeboPositionToUnreal(X,Y,Z)
    # with the ArduPilot bridge convention Gazebo X=East, Y=North, Z=Up.
    return east_m * 100.0, -north_m * 100.0, up_m * 100.0


def unreal_to_enu(x_cm: float, y_cm: float, z_cm: float):
    # Exact inverse of the above mapping.
    return x_cm / 100.0, -y_cm / 100.0, z_cm / 100.0


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


if __name__ == "__main__":
    unittest.main()
