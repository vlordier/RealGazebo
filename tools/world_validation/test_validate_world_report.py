#!/usr/bin/env python3
from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from validate_world_report import validate_report


def good_report() -> dict:
    return {
        "world": {
            "actor_count": 10,
            "component_count": 20,
            "categories": {
                "directional_lights": [{}],
                "sky_lights": [{}],
                "sky_atmospheres": [{}],
                "post_process_volumes": [{"unbound": True}],
            },
            "missing_assets": [],
            "null_mesh_components": [],
        },
        "screenshot": {"requested": False},
    }


class WorldReportValidationTests(unittest.TestCase):
    def test_valid_world_passes_without_screenshot_requirement(self) -> None:
        self.assertEqual(validate_report(good_report()), [])

    def test_missing_sky_is_rejected(self) -> None:
        report = good_report()
        report["world"]["categories"]["sky_atmospheres"] = []
        errors = validate_report(report)
        self.assertIn("missing required world category: sky_atmospheres", errors)

    def test_post_process_must_be_unbound(self) -> None:
        report = good_report()
        report["world"]["categories"]["post_process_volumes"] = [{"unbound": False}]
        self.assertIn("no unbound post-process volume", validate_report(report))

    def test_missing_asset_is_rejected(self) -> None:
        report = good_report()
        report["world"]["missing_assets"] = ["/RealGazebo/Missing"]
        self.assertIn("missing referenced assets: 1", validate_report(report))

    def test_screenshot_contract(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            image = Path(temp_dir) / "world.png"
            image.write_bytes(b"x" * 2048)
            report = good_report()
            report["screenshot"] = {"requested": True, "path": str(image)}
            self.assertEqual(validate_report(report, require_screenshot=True), [])

    def test_tiny_screenshot_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            image = Path(temp_dir) / "world.png"
            image.write_bytes(b"x")
            report = good_report()
            report["screenshot"] = {"requested": True, "path": str(image)}
            self.assertIn("screenshot file is unexpectedly small", validate_report(report, require_screenshot=True))


if __name__ == "__main__":
    unittest.main()
