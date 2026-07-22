#!/usr/bin/env python3
from __future__ import annotations

import unittest

from compare_world_reports import compare_reports


def report(
    *,
    actors: int = 100,
    components: int = 200,
    meshes: int = 20,
    materials: int = 20,
    lights: int = 1,
) -> dict:
    return {
        "world": {
            "actor_count": actors,
            "component_count": components,
            "categories": {
                "directional_lights": [{} for _ in range(lights)],
                "sky_lights": [{}],
                "sky_atmospheres": [{}],
                "volumetric_clouds": [{}],
                "height_fogs": [{}],
                "post_process_volumes": [{}],
                "landscapes": [{}],
                "cameras": [{}],
                "static_mesh_actors": [{} for _ in range(10)],
            },
            "mesh_assets": [f"/Game/Mesh{i}" for i in range(meshes)],
            "material_assets": [f"/Game/Material{i}" for i in range(materials)],
        }
    }


class WorldReportComparisonTests(unittest.TestCase):
    def test_identical_reports_pass(self) -> None:
        baseline = report()
        self.assertEqual(compare_reports(baseline, baseline), [])

    def test_large_actor_loss_is_rejected(self) -> None:
        errors = compare_reports(report(actors=100), report(actors=70))
        self.assertTrue(any("actor_count regressed" in error for error in errors))

    def test_small_actor_loss_is_tolerated(self) -> None:
        errors = compare_reports(report(actors=100), report(actors=95))
        self.assertFalse(any("actor_count regressed" in error for error in errors))

    def test_light_loss_is_rejected(self) -> None:
        errors = compare_reports(report(lights=2), report(lights=1))
        self.assertTrue(any("directional_lights" in error for error in errors))

    def test_material_asset_loss_is_rejected(self) -> None:
        errors = compare_reports(report(materials=20), report(materials=10))
        self.assertTrue(any("material_assets lost" in error for error in errors))

    def test_small_asset_loss_within_threshold_is_allowed(self) -> None:
        baseline = report(meshes=100)
        current = report(meshes=96)
        errors = compare_reports(baseline, current, max_asset_loss_fraction=0.05)
        self.assertFalse(any("mesh_assets lost" in error for error in errors))

    def test_missing_world_object_raises(self) -> None:
        with self.assertRaises(ValueError):
            compare_reports({}, report())


if __name__ == "__main__":
    unittest.main()
